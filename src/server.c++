// Copyright (c) 2014 Sandstorm Development Group, Inc.
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Hack around stdlib bug with C++14.
#include <initializer_list>  // force libstdc++ to include its config
#undef _GLIBCXX_HAVE_GETS    // correct broken config
// End hack.

#include <kj/main.h>
#include <kj/debug.h>
#include <kj/io.h>
#include <kj/async-io.h>
#include <capnp/rpc-twoparty.h>
#include <capnp/serialize.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

#include <sandstorm/grain.capnp.h>
#include <sandstorm/web-session.capnp.h>
#include <test-app.capnp.h>

namespace {

#if __QTCREATOR
#define KJ_MVCAP(var) var
// QtCreator dosen't understand C++14 syntax yet.
#else
#define KJ_MVCAP(var) var = ::kj::mv(var)
// Capture the given variable by move.  Place this in a lambda capture list.  Requires C++14.
//
// TODO(cleanup):  Move to libkj.
#endif

typedef unsigned int uint;
typedef unsigned char byte;

size_t getFileSize(int fd, kj::StringPtr filename) {
  struct stat stats;
  KJ_SYSCALL(fstat(fd, &stats));
  KJ_REQUIRE(S_ISREG(stats.st_mode), "Not a regular file.", filename);
  return stats.st_size;
}

kj::Maybe<kj::AutoCloseFd> tryOpen(kj::StringPtr name, int flags, mode_t mode = 0666) {
  // Try to open a file, returning an RAII wrapper around the file descriptor, or null if the
  // file doesn't exist. All other errors throw exceptions.

  int fd;

  while ((fd = open(name.cStr(), flags, mode)) < 0) {
    int error = errno;
    if (error == ENOENT) {
      return nullptr;
    } else if (error != EINTR) {
      KJ_FAIL_SYSCALL("open(name)", error, name);
    }
  }

  return kj::AutoCloseFd(fd);
}

class TestInterfaceImpl final: public TestInterface::Server {
  kj::Promise<void> foo(FooContext context) override {
    context.getResults().setB("footest");
    return kj::READY_NOW;
  }
  kj::Promise<void> save(SaveContext context) override {
    return kj::READY_NOW;
  }
};

// =======================================================================================
// WebSession implementation (interface declared in sandstorm/web-session.capnp)

class WebSessionImpl final: public sandstorm::WebSession::Server {
public:
  WebSessionImpl(sandstorm::UserInfo::Reader userInfo,
                 sandstorm::SessionContext::Client context,
                 sandstorm::WebSession::Params::Reader params,
                 sandstorm::SandstormApi<>::Client api) : sessionContext(context), api(api) {
    // Permission #0 is "write". Check if bit 0 in the PermissionSet is set.
    auto permissions = userInfo.getPermissions();
    canWrite = permissions.size() > 0 && (permissions[0] & 1);

    // `UserInfo` is defined in `sandstorm/grain.capnp` and contains info like:
    // - A stable ID for the user, so you can correlate sessions from the same user.
    // - The user's display name, e.g. "Mark Miller", useful for identifying the user to other
    //   users.
    // - The user's permissions (seen above).

    // `WebSession::Params` is defined in `sandstorm/web-session.capnp` and contains info like:
    // - The hostname where the grain was mapped for this user. Every time a user opens a grain,
    //   it is mapped at a new random hostname for security reasons.
    // - The user's User-Agent and Accept-Languages headers.

    // `SessionContext` is defined in `sandstorm/grain.capnp` and implements callbacks for
    // sharing/access control and service publishing/discovery.
  }

  kj::Promise<void> get(GetContext context) override {
    // HTTP GET request.

    KJ_LOG(WARNING, "getting");


    auto path = context.getParams().getPath();
    if (path == "offer" || path == "offer/") {
      auto req = sessionContext.offerRequest();
      req.setCap(kj::heap<TestInterfaceImpl>());
      return req.send().then([context](auto args) mutable {
        auto response = context.getResults();
        auto content = response.initContent();
        content.setMimeType("text/plain");
        content.setStatusCode(sandstorm::WebSession::Response::SuccessCode::OK);
        content.getBody().setBytes(kj::str("success").asBytes());
      }, [](auto err) {
        KJ_LOG(ERROR, "error offering:", err);
      });
    }

    return readFile("index.html", context, "text/html");
  }

  kj::Promise<void> put(PutContext context) override {
    auto results = context.getResults();
    auto content = results.initContent();
    auto params = context.getParams();
    auto path = params.getPath();
    auto data = params.getContent().getContent();
    auto req = api.claimRequestRequest();
    auto requestToken = req.initRequestToken(data.size());
    memcpy(requestToken.begin(), data.begin(), data.size());
    if (path == "claim") {
      KJ_LOG(WARNING, "putting to /claim");
      bool perms[1] = {true};
      req.setRequiredPermissions(perms);
      return req.send().then([this, content](auto response) mutable {
        auto cap = response.getCap().template castAs<TestInterface>();
        return cap.fooRequest().send().then([content](auto response) mutable {
          content.setMimeType("text/html");
          content.setStatusCode(sandstorm::WebSession::Response::SuccessCode::OK);
          content.getBody().setBytes(response.getB().asBytes());
        });
      });
    } else if (path == "claim-and-save") {
      KJ_LOG(WARNING, "putting to /claim-and-save");
      bool perms[1] = {true};
      req.setRequiredPermissions(perms);
      return req.send().then([this, content](auto response) mutable {
        auto cap = response.getCap().template castAs<TestInterface>();
        auto req = api.saveRequest();
        req.setCap(cap);
        return req.send().then([this, content](auto response) mutable {
          auto req = api.restoreRequest();
          req.setToken(response.getToken());
          return req.send().then([content](auto response) mutable {
            auto cap = response.getCap().template castAs<TestInterface>();
            return cap.fooRequest().send().then([content](auto response) mutable {
              content.setMimeType("text/html");
              content.setStatusCode(sandstorm::WebSession::Response::SuccessCode::OK);
              content.getBody().setBytes(response.getB().asBytes());
            });
          });
        });
      });
    } else if (path == "claim-failing-requirements") {
      KJ_LOG(WARNING, "putting to /claim-failing-requirements");
      bool perms[3] = {true, false, true};
      req.setRequiredPermissions(perms);
      return req.send().then([this, content](auto response) mutable {
        content.setMimeType("text/html");
        content.setStatusCode(sandstorm::WebSession::Response::SuccessCode::OK);
        content.getBody().setBytes(kj::str("successfully restored, but shouldn't have!").asBytes());
      }).catch_([](kj::Exception err) {
        KJ_LOG(WARNING, "some error putting", err);
      });
    } else {
      results.initClientError();
      return kj::READY_NOW;
    }
  }

  kj::Promise<void> delete_(DeleteContext context) override {
    KJ_FAIL_REQUIRE("not implemented");
    return kj::READY_NOW;
  }

private:
  bool canWrite;
  // True if the user has write permission.
  sandstorm::SessionContext::Client sessionContext;
  sandstorm::SandstormApi<>::Client api;

  kj::Promise<void> readFile(
      kj::StringPtr filename, GetContext context, kj::StringPtr contentType) {
    KJ_IF_MAYBE(fd, tryOpen(filename, O_RDONLY)) {
      auto size = getFileSize(*fd, filename);
      kj::FdInputStream stream(kj::mv(*fd));
      auto response = context.getResults(capnp::MessageSize { size / sizeof(capnp::word) + 32, 0 });
      auto content = response.initContent();
      content.setStatusCode(sandstorm::WebSession::Response::SuccessCode::OK);
      content.setMimeType(contentType);
      stream.read(content.getBody().initBytes(size).begin(), size);
      return kj::READY_NOW;
    } else {
      auto error = context.getResults().initClientError();
      error.setStatusCode(sandstorm::WebSession::Response::ClientErrorCode::NOT_FOUND);
      return kj::READY_NOW;
    }
  }
};

// =======================================================================================
// UiView implementation (interface declared in sandstorm/grain.capnp)

class UiViewImpl final: public sandstorm::MainView<capnp::Text>::Server {
public:
  explicit UiViewImpl(sandstorm::SandstormApi<>::Client api) : api(api) {}
  kj::Promise<void> getViewInfo(GetViewInfoContext context) override {
    auto viewInfo = context.initResults();

    // Define a "write" permission. People who don't have this will get read-only access.
    //
    auto perms = viewInfo.initPermissions(1);
    perms[0].setName("write");

    return kj::READY_NOW;
  }

  kj::Promise<void> newSession(NewSessionContext context) override {
    auto params = context.getParams();

    KJ_REQUIRE(params.getSessionType() == capnp::typeId<sandstorm::WebSession>(),
               "Unsupported session type.");

    context.getResults().setSession(
        kj::heap<WebSessionImpl>(params.getUserInfo(), params.getContext(),
                                 params.getSessionParams().getAs<sandstorm::WebSession::Params>(), api));

    return kj::READY_NOW;
  }

  kj::Promise<void> restore(RestoreContext context) override {
    KJ_LOG(WARNING, "restoring");
    context.getResults().setCap(kj::heap<TestInterfaceImpl>());
    return kj::READY_NOW;
  }

  sandstorm::SandstormApi<>::Client api;
};

// =======================================================================================
// Program main

class ServerMain {
public:
  ServerMain(kj::ProcessContext& context): context(context), ioContext(kj::setupAsyncIo()) {}

  kj::MainFunc getMain() {
    return kj::MainBuilder(context, "Sandstorm Thin Server",
                           "Intended to be run as the root process of a Sandstorm app.")
        .callAfterParsing(KJ_BIND_METHOD(*this, run))
        .build();
  }

  kj::MainBuilder::Validity run() {
    // Set up RPC on file descriptor 3.

    auto paf = kj::newPromiseAndFulfiller<sandstorm::SandstormApi<>::Client>();
    sandstorm::SandstormApi<>::Client apiCap(kj::mv(paf.promise));

    auto stream = ioContext.lowLevelProvider->wrapSocketFd(3);
    capnp::TwoPartyVatNetwork network(*stream, capnp::rpc::twoparty::Side::CLIENT);
    auto rpcSystem = capnp::makeRpcServer(network, kj::heap<UiViewImpl>(apiCap));

    // Get the SandstormApi default capability from the supervisor.
    {
      capnp::MallocMessageBuilder message;
      auto vatId = message.getRoot<capnp::rpc::twoparty::VatId>();
      vatId.setSide(capnp::rpc::twoparty::Side::SERVER);
      paf.fulfiller->fulfill(rpcSystem.bootstrap(vatId).castAs<sandstorm::SandstormApi<>>());
    }

    kj::NEVER_DONE.wait(ioContext.waitScope);
  }

private:
  kj::ProcessContext& context;
  kj::AsyncIoContext ioContext;
};

}  // anonymous namespace

KJ_MAIN(ServerMain)
