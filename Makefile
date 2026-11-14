CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O3 -g
OUTDIR = bin

LIB_SRCS = src/core.cpp src/brute.cpp
LIB_HDRS = $(wildcard src/*.h)
TEST_SRCS = $(wildcard tests/*.cpp)

all: $(OUTDIR)/main $(OUTDIR)/tests
	@echo Compiled all executables

$(OUTDIR):
	mkdir -p $(OUTDIR)

# The original solver, kept as-is for reference.
$(OUTDIR)/main: main.cpp | $(OUTDIR)
	$(CXX) -Wall -O3 -g main.cpp -o $@

$(OUTDIR)/tests: $(LIB_SRCS) $(LIB_HDRS) $(TEST_SRCS) tests/test_util.h | $(OUTDIR)
	$(CXX) $(CXXFLAGS) $(LIB_SRCS) $(TEST_SRCS) -o $@

test: $(OUTDIR)/tests
	./$(OUTDIR)/tests

test-all: $(OUTDIR)/tests
	./$(OUTDIR)/tests --slow

clean:
	rm -rf $(OUTDIR)

.PHONY: all test test-all clean
