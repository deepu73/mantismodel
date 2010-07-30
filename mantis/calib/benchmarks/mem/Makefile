###################### Cluster-on-Demand Makefile ###################
 
SHELL = /bin/sh
 
CC = gcc
#WARNINGS = -Wall -Werror -Wshadow
#WARNINGS = -Wall -Werror
WARNINGS = -Wall -Wshadow
#CFLAGS = -g $(WARNINGS) -I. -pthread
#CFLAGS = -g $(WARNINGS) -I. -pedantic # Give me ALL the warnings
#CFLAGS = -O3 $(WARNINGS) -I.
#CFLAGS = -O1 $(WARNINGS) -I. -pg
 
###### Needed on Solaris, should be harmless on *BSD and Linux ######
CFLAGS += -DBSD_COMP

CFLAGS += -D_REENTRANT # -D_THREAD_SAFE
 
LD = $(CC)
LDFLAGS = -g -lm -lpthread # -llthread -pthread
#LDFLAGS = -pg -lm -lpthread
STATIC_LD = -static
#LDFLAGS += -lefence
#LDFLAGS += -static

PROGS = gamut netgamut

utillib_OBJ = utilio.o utilnet.o utilarr.o utillog.o mt-rand.o
worker_OBJ  = workerctl.o workeropts.o workerlib.o workerinfo.o \
        workerwait.o workersync.o linkctl.o linklib.o \
	cpuworker.o memworker.o diskworker.o networker.o cpuburn.o
gamutlib_OBJ = calibrate.o opts.o mainctl.o reaper.o input.o
gamut_OBJ = gamut.o $(gamutlib_OBJ) $(worker_OBJ) $(utillib_OBJ)
netgamut_OBJ = netgamut.o $(gamutlib_OBJ) $(worker_OBJ) $(utillib_OBJ)

all:    $(PROGS) $(SPROGS)
.PHONY: all

# Generate dependencies
.%.d: %.c
	@echo -n "Updating $< dependencies ... "
	@$(CC) $(CFLAGS) -MM $< > $@
	@echo "done."
 
# Update dependencies
depend: .make.deps
	@echo "Updated dependencies"

.PHONY:	depend

.make.deps:     $(patsubst %.c,.%.d,$(wildcard *.c))
	@rm -f .make.deps
	@cat $(patsubst %.c,.%.d,$(wildcard *.c)) > .make.deps
 
include .make.deps

gamut:	$(gamut_OBJ)
	$(LD) -o gamut $(gamut_OBJ) $(LDFLAGS)

netgamut:	$(netgamut_OBJ)
	$(LD) -o netgamut $(netgamut_OBJ) $(LDFLAGS)

.PHONY: cleanall clean cleanobj cleandep
 
cleanall:       clean cleandep
 
clean:  cleanobj
	rm -f $(PROGS) $(SPROGS)
 
cleanobj:
	rm -f *.o
 
cleandep:
	rm -f .*.d .make.deps

