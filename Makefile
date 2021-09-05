CC = g++
#CFLAGS = -Wall -O3 -g -fprofile-arcs -ftest-coverage 
CFLAGS = -Wall -O3 -g 
OUTDIR = bin

SRCS = main.cpp 

EXECS = $(SRCS:%.cpp=$(OUTDIR)/%)

all: $(EXECS)
	@echo Compiled all executables

$(OUTDIR):
	mkdir $(OUTDIR)

$(OUTDIR)/%: $(SRCS) | $(OUTDIR)
	$(CC) $(CFLAGS) $(MAINSRC) $(@:$(OUTDIR)/%=%.cpp) -o $@
