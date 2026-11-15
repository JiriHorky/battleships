CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O3 -g
OUTDIR = bin

LIB_SRCS = src/core.cpp src/brute.cpp src/dp.cpp src/board.cpp
LIB_HDRS = $(wildcard src/*.h)
TEST_SRCS = $(wildcard tests/*.cpp)

all: $(OUTDIR)/tests $(OUTDIR)/bench $(OUTDIR)/play $(OUTDIR)/tui
	@echo Compiled all executables

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/tests: $(LIB_SRCS) $(LIB_HDRS) $(TEST_SRCS) tests/test_util.h | $(OUTDIR)
	$(CXX) $(CXXFLAGS) -DSRC_ROOT=\"$(CURDIR)\" $(LIB_SRCS) $(TEST_SRCS) -o $@

$(OUTDIR)/bench: $(LIB_SRCS) $(LIB_HDRS) src/bench.cpp | $(OUTDIR)
	$(CXX) $(CXXFLAGS) $(LIB_SRCS) src/bench.cpp -o $@

$(OUTDIR)/play: $(LIB_SRCS) $(LIB_HDRS) src/play.cpp | $(OUTDIR)
	$(CXX) $(CXXFLAGS) $(LIB_SRCS) src/play.cpp -o $@

$(OUTDIR)/tui: $(LIB_SRCS) $(LIB_HDRS) src/tui.cpp | $(OUTDIR)
	$(CXX) $(CXXFLAGS) $(LIB_SRCS) src/tui.cpp -o $@

# Benchmark set: legacy setups and the 10x10 game fleet, from the empty board
# (the slowest: ~0.4 s and ~0.3 GB per run on an M4 Pro) to late game.
bench: $(OUTDIR)/bench
	./$(OUTDIR)/bench --repeat 3 legacy game-

test: $(OUTDIR)/tests
	./$(OUTDIR)/tests

test-all: $(OUTDIR)/tests
	./$(OUTDIR)/tests --slow

clean:
	rm -rf $(OUTDIR)

.PHONY: all bench test test-all clean
