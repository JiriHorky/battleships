CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O3 -g
OUTDIR = bin

LIB_SRCS = src/core.cpp src/brute.cpp src/dp.cpp
LIB_HDRS = $(wildcard src/*.h)
TEST_SRCS = $(wildcard tests/*.cpp)

all: $(OUTDIR)/main $(OUTDIR)/tests $(OUTDIR)/bench
	@echo Compiled all executables

$(OUTDIR):
	mkdir -p $(OUTDIR)

# The original solver, kept as-is for reference.
$(OUTDIR)/main: main.cpp | $(OUTDIR)
	$(CXX) -Wall -O3 -g main.cpp -o $@

$(OUTDIR)/tests: $(LIB_SRCS) $(LIB_HDRS) $(TEST_SRCS) tests/test_util.h | $(OUTDIR)
	$(CXX) $(CXXFLAGS) -DSRC_ROOT=\"$(CURDIR)\" $(LIB_SRCS) $(TEST_SRCS) -o $@

$(OUTDIR)/bench: $(LIB_SRCS) $(LIB_HDRS) src/bench.cpp | $(OUTDIR)
	$(CXX) $(CXXFLAGS) $(LIB_SRCS) src/bench.cpp -o $@

# Quick benchmark set (fast scenarios only).
bench: $(OUTDIR)/bench
	./$(OUTDIR)/bench --repeat 3 legacy czech-8x8 czech-10x10-shots30 czech-10x10-shots50

test: $(OUTDIR)/tests
	./$(OUTDIR)/tests

test-all: $(OUTDIR)/tests
	./$(OUTDIR)/tests --slow

clean:
	rm -rf $(OUTDIR)

.PHONY: all bench test test-all clean
