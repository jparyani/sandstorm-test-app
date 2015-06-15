CXX=clang++
CXXFLAGS=-O2 -Wall
CXXFLAGS2=-std=c++1y -Itmp $(CXXFLAGS)
SANDSTORM_CAPNP_DIR=~/workspace/sandstorm/src

.PHONEY: all clean dev

package.spk: server sandstorm-pkgdef.capnp empty
	spk pack package.spk

dev: server sandstorm-pkgdef.capnp empty
	spk dev

clean:
	rm -rf tmp server package.spk empty

tmp/genfiles:
	@mkdir -p tmp
	capnp compile --src-prefix=$(SANDSTORM_CAPNP_DIR) -oc++:tmp $(SANDSTORM_CAPNP_DIR)/sandstorm/*.capnp
	capnp compile -I $(SANDSTORM_CAPNP_DIR) -oc++:tmp test.capnp
	@cp $(SANDSTORM_CAPNP_DIR)/sandstorm/util.h tmp/sandstorm/
	@cp $(SANDSTORM_CAPNP_DIR)/sandstorm/util.c++ tmp/sandstorm/
	@touch tmp/genfiles

server: tmp/genfiles server.c++
	$(CXX) -static server.c++ tmp/sandstorm/util.c++ tmp/sandstorm/*.capnp.c++ tmp/*.c++ -o server $(CXXFLAGS2) `pkg-config capnp-rpc --cflags --libs`

empty:
	mkdir -p empty

