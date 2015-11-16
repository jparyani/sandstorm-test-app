CC=clang
CFLAGS=-O2 -Wall
CXX=clang++
CXXFLAGS=-O2 -Wall
CXXFLAGS2=-std=c++1y -Itmp $(CXXFLAGS)

PARALLEL=$(shell nproc)

LIBS=-lpthread

REMOTE_ekam=https://github.com/sandstorm-io/ekam.git
REMOTE_capnproto=https://github.com/sandstorm-io/capnproto.git
REMOTE_sandstorm=https://github.com/sandstorm-io/sandstorm.git

.PHONY: default clean

default: server

clean:
	rm -rf tmp server

deps: tmp/.deps

tmp/.deps: deps/capnproto deps/ekam deps/sandstorm
	@mkdir -p tmp
	@touch tmp/.deps

deps/capnproto:
	@mkdir -p deps
	git clone $(REMOTE_capnproto) deps/capnproto

deps/ekam:
	@mkdir -p deps
	git clone $(REMOTE_ekam) deps/ekam
	@ln -s .. deps/ekam/deps

deps/sandstorm:
	@mkdir -p deps
	git clone $(REMOTE_sandstorm) deps/sandstorm

tmp/ekam-bin: tmp/.deps
	@mkdir -p tmp
	@rm -f tmp/ekam-bin
	@which ekam >/dev/null && ln -s "`which ekam`" tmp/ekam-bin || \
	    (cd deps/ekam && $(MAKE) bin/ekam-bootstrap && \
	    cd ../.. && ln -s ../deps/ekam/bin/ekam-bootstrap tmp/ekam-bin)

tmp/.ekam-run: tmp/ekam-bin src/* src/sandstorm/* tmp/.deps
	CC="$(CC)" CXX="$(CXX)" CFLAGS="$(CFLAGS)" CXXFLAGS="$(CXXFLAGS2)" LIBS="$(LIBS)" tmp/ekam-bin -j$(PARALLEL)
	@touch tmp/.ekam-run

continuous: 
	CC="$(CC)" CXX="$(CXX)" CFLAGS="$(CFLAGS)" CXXFLAGS="$(CXXFLAGS2)" LIBS="$(LIBS)" tmp/ekam-bin -j$(PARALLEL) -c -n :41315

server: tmp/.ekam-run
	cp tmp/server server

