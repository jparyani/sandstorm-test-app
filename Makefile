CC=clang
CFLAGS=-O2 -Wall
CXX=clang++
CXXFLAGS=-O2 -Wall
CXXFLAGS2=-std=c++1y -Itmp $(CXXFLAGS)

PARALLEL=$(shell nproc)

LIBS=-lpthread

REMOTE_capnproto=https://github.com/sandstorm-io/capnproto.git

.PHONY: default clean

default: server

clean:
	rm -rf server

deps/capnproto:
	@mkdir -p deps
	git clone $(REMOTE_capnproto) deps/capnproto

/usr/local/bin/capnp: deps/capnproto
	cd deps/capnproto/c++ && autoreconf -i && ./configure CXX=$(CXX) CC=$(CC) && make -j && sudo make install

tmp/include:
	mkdir -p tmp/include

tmp/include/test-app.capnp.h: tmp/include src/test-app.capnp /usr/local/bin/capnp
	capnp compile -oc++:tmp/include -I/opt/sandstorm/latest/usr/include --src-prefix=src/ src/test-app.capnp

CAPNP_SCHEMAS = \
	sandstorm/util.capnp \
	sandstorm/powerbox.capnp \
	sandstorm/grain.capnp \
	sandstorm/web-session.capnp \
	sandstorm/activity.capnp \
	sandstorm/identity.capnp

SCHEMA_INCLUDES = $(foreach schema,$(CAPNP_SCHEMAS),tmp/include/$(schema).h)
SCHEMA_SRCS = $(foreach schema,$(CAPNP_SCHEMAS),/opt/sandstorm/latest/usr/include/$(schema))

$(SCHEMA_INCLUDES): tmp/include /usr/local/bin/capnp $(SCHEMA_SRCS)
	capnp compile -oc++:tmp/include --src-prefix=/opt/sandstorm/latest/usr/include $(SCHEMA_SRCS)

server: src/server.c++ tmp/include/test-app.capnp.h tmp/include/sandstorm/web-session.capnp.h
	$(CXX) $(CXXFLAGS2) -Itmp/include -lkj -lkj-async -lcapnp -lcapnp-rpc -o server src/server.c++ tmp/include/test-app.capnp.c++ tmp/include/sandstorm/*.c++
