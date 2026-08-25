# Builds the compiled C++ tools in this repo. Manual `g++`/`clang++` compiles
# still work fine — this just automates the same commands and only rebuilds
# what's stale (based on .cpp/.hpp timestamps).
#
# Usage:
#   make            # same as `make all` (default target)
#   make all        # everything: static + rt (needs protobuf + pkg-config, see gtfs-rt/readme.md)
#   make static     # static-gtfs + webserver tools only (no protobuf needed)
#   make rt         # GTFS-RT decoders only (needs protobuf + pkg-config, see gtfs-rt/readme.md)
#   make exp        # experimental fast-static-gtfs tools (no protobuf needed)
#   make fast       # fast-static-gtfs webserver (no protobuf needed)
#   make clean      # remove all binaries built by this Makefile

CXX    ?= g++
CXXSTD := -std=c++17
OPT    := -O3

STATIC_GTFS_DIR      := static-gtfs
WEBSERVER_TOOLS_DIR  := webserver/tools
RT_DIR                := gtfs-rt/proto-conversion/webserver-implementation
RT_PROTO_SRC          := gtfs-rt/proto-conversion/transit-files/gtfs-realtime.pb.cc
PROTOBUF_FLAGS        := $(shell pkg-config --cflags --libs protobuf 2>/dev/null)
EXP_DIR               := fast-static-gtfs

STATIC_GTFS_BINS := $(STATIC_GTFS_DIR)/gtfs_cli

WEBSERVER_TOOLS  := getTrips searchstop tripjson stopjson getneareststopsjson stopinfo searchroute
WEBSERVER_BINS   := $(addprefix $(WEBSERVER_TOOLS_DIR)/,$(WEBSERVER_TOOLS))

RT_TOOLS := decodeTrip decodeStop decodeAlerts
RT_BINS  := $(addprefix $(RT_DIR)/,$(RT_TOOLS))

EXP_BINS := $(EXP_DIR)/test

FAST_BINS := $(EXP_DIR)/webserver

.PHONY: all static rt exp fast static-gtfs webserver-tools clean

# Default: everything.
all: static-gtfs webserver-tools rt

# Just the tools that build with a C++17 compiler alone, no protobuf setup required.
static: static-gtfs webserver-tools

static-gtfs: $(STATIC_GTFS_BINS)

webserver-tools: $(WEBSERVER_BINS)

rt: $(RT_BINS)

exp: $(EXP_BINS)

fast: $(FAST_BINS)

$(STATIC_GTFS_DIR)/gtfs_cli: $(STATIC_GTFS_DIR)/gtfs_cli.cpp $(STATIC_GTFS_DIR)/gtfs.hpp $(STATIC_GTFS_DIR)/config.hpp
	$(CXX) $(CXXSTD) $(OPT) -o $@ $<

$(WEBSERVER_TOOLS_DIR)/%: $(WEBSERVER_TOOLS_DIR)/%.cpp $(STATIC_GTFS_DIR)/gtfs.hpp
	$(CXX) $(CXXSTD) $(OPT) -o $@ $<

# gtfs-realtime.pb.cc/.pb.h are generated once via `protoc` (see gtfs-rt/readme.md) and
# checked in already, so this only re-links if a decoder's own .cpp changes.
$(RT_DIR)/%: $(RT_DIR)/%.cpp $(RT_PROTO_SRC)
	$(CXX) $(CXXSTD) -O3 -o $@ $^ $(PROTOBUF_FLAGS)

$(EXP_DIR)/%: $(EXP_DIR)/%.cpp $(EXP_DIR)/fast-gtfs.hpp
	$(CXX) $(CXXSTD) $(OPT) -o $@ $<

$(EXP_DIR)/webserver: $(EXP_DIR)/webserver.cpp $(EXP_DIR)/fast-gtfs.hpp $(EXP_DIR)/fast-config.hpp $(EXP_DIR)/webservermethods.hpp $(EXP_DIR)/httplib.h $(STATIC_GTFS_DIR)/gtfs.hpp
	$(CXX) $(CXXSTD) $(OPT) -o $@ $<

clean:
	rm -f $(STATIC_GTFS_BINS) $(WEBSERVER_BINS) $(RT_BINS) $(EXP_BINS) $(FAST_BINS)
