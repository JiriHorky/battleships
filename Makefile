CC = g++
CFLAGS = -Wall -O3 -pg 
OUTDIR = bin

SRCS = main.cpp 

EXECS = $(SRCS:%.cpp=$(OUTDIR)/%)

all: $(EXECS)
	@echo Compiled all executables

$(OUTDIR):
	mkdir $(OUTDIR)

$(OUTDIR)/%: $(SRCS) | $(OUTDIR)
	$(CC) $(CFLAGS) $(MAINSRC) $(@:$(OUTDIR)/%=%.cpp) -o $@
