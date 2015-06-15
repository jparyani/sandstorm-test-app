import os
import socket
import time
import sys
sys.path.insert(0, '/src')
sys.path.insert(0, '/src/sandstorm')
import capnp
import util_capnp
import grain_capnp
import hack_session_capnp
import api_session_capnp
import sandstorm_http_bridge_capnp
import web_session_capnp

import thread
import BaseHTTPServer

import test_capnp

PORT = 10000
session_id = None

class HttpServer(BaseHTTPServer.BaseHTTPRequestHandler):
    def do_GET(self):
      global session_id
      self.send_response(200)
      self.send_header('Content-type','text/html')
      self.end_headers()
      self.wfile.write("Hello World !")
      session_id = self.headers["X-Sandstorm-Session-Id"]
      return

class OngoingNotification(grain_capnp.OngoingNotification.Server):
  def __del__(self):
    print >>sys.stderr, 'deleting notification'

  def cancel(self, **kwargs):
    print >>sys.stderr, 'cancelling notification'
    os._exit(0)

class OngoingNotification(grain_capnp.OngoingNotification.Server):
  def __del__(self):
    print >>sys.stderr, 'deleting notification'

  def cancel(self, **kwargs):
    print >>sys.stderr, 'cancelling notification'
    os._exit(0)

class TestInterface(test_capnp.TestInterface.Server):
  def save(self, **kwargs):
    print >>sys.stderr, "saving..."
  def foo(self, a, **kwargs):
    print >>sys.stderr, "calling foo with: " + a


def start_server():
  httpd = BaseHTTPServer.HTTPServer(("", PORT), HttpServer)

  print "serving at port", PORT
  thread.start_new_thread(lambda: httpd.serve_forever(), ())

  time.sleep(1)

  s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
  s.connect("/tmp/sandstorm-api")
  c = capnp.TwoPartyClient(s)
  print >>sys.stderr, 'test3'
  return c

def test_wakelock():
  c = start_server()
  cap = c.bootstrap().cast_as(sandstorm_http_bridge_capnp.SandstormHttpBridge).getSandstormApi().api.cast_as(grain_capnp.SandstormApi)

  print >>sys.stderr, 'test'
  wakelock = cap.stayAwake(notification=OngoingNotification(), displayInfo={"caption": {"defaultText": "background test"}}).wait()
  print >>sys.stderr, 'test2'
  print >>sys.stderr, wakelock
  time.sleep(10)

  del wakelock
  sys.exit(0)
  capnp.wait_forever()


class Session(web_session_capnp.WebSession.Server):
  def __init__(self, context):
    self._context = context

  def get(self, _context, **kwargs):
    def error(err):
      print >>sys.stderr, "request errored: " + str(err)

    print >>sys.stderr, "get"
    content = _context.results.init("content")
    content.body.bytes = "test"
    return self._context.request().then(lambda args: args.cap.cast_as(test_capnp.TestInterface).foo('test'), error)

class MainView(grain_capnp.MainView.Server):
  def newSession(self, userInfo, context, sessionType, sessionParams, **kwargs):
    print >>sys.stderr, "newSession"
    return Session(context)


  def restore(self, objectId, **kwargs):
    print >>sys.stderr, "restoring..."
    return TestInterface()


class WrappedFd:
  def __init__(self, fileno):
    self._fileno = fileno

  def fileno(self):
    return self._fileno


def test_powerbox():
  view = MainView()
  s = capnp.TwoPartyServer(WrappedFd(3), bootstrap=view)
  view.api = s.bootstrap().cast_as(grain_capnp.SandstormApi)

  capnp.wait_forever()

test_powerbox()
