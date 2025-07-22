
VERSION=3.0.2

# List of object files to generate (and the list of source files, generated
# by pattern substitution)

COBJS = main.o globals.o devices.o dev_disk.o dev_term.o dev_alarm.o dev_clock.o sig_ints.o mmu.o
SRCS = ${COBJS:.o=.c}
CC = gcc
CFLAGS = -Wall -DVERSION=\"$(VERSION)\" 
CFLAGS += -DMMU
CFLAGS += -Wno-deprecated-declarations
CFLGAS += -Wno-int-to-void-pointer-cast
CFLAGS += -Wno-pointer-to-int-cast
CFLAGS += -Wno-int-to-pointer-cast
CFLAGS += -g
CFLAGS += -DVIRTUAL_TIME

UNAME := $(shell uname -s)

ifeq ($(UNAME), Darwin)
	CFLAGS += -D_XOPEN_SOURCE
endif

LD = ld
LDFLAGS = -L.
HOME=..
TARGET=libusloss$(VERSION).a

# Implicit rules for generating object files
.c.o:
	$(CC) $(CFLAGS) -DMAKELIB -c -o $*.o $*.c


$(TARGET) : $(COBJS)
	$(AR) -r $@ $(COBJS)

clean:
	rm -f $(COBJS) usloss libusloss*.a core*	


$(COBJS): usloss.h Makefile


install: $(TARGET) 
	mkdir -p $(HOME)/build/include $(HOME)/build/lib
	cp usloss.h usyscall.h mmu.h $(HOME)/build/include
	cp phases/*.h $(HOME)/build/include
	-chmod ug+w,o+r $(HOME)/build/include/*
	cp $(TARGET) $(HOME)/build/lib/
	-chmod ug+w,o+r $(HOME)/build/lib/$(TARGET)
	-rm -f $(HOME)/build/lib/libusloss.a
	-(cd $(HOME)/build/lib; ln -s $(TARGET) libusloss.a)
