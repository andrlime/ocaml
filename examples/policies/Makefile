#**************************************************************************
#*                                                                        *
#*                                 OCaml                                  *
#*                                                                        *
#*                               Andrew Li                                *
#*                                                                        *
#*   Copyright 2026 Andrew Li                                             *
#*                                                                        *
#*   All rights reserved.  This file is distributed under the terms of    *
#*   the GNU Lesser General Public License version 2.1, with the          *
#*   special exception on linking described in the file LICENSE.          *
#*                                                                        *
#**************************************************************************

# Build placement policies as loadable shared objects.
#
#   make            # compile every *.c into the matching *.so
#   make clean
#
# Each .so is loaded by pointing CAML_GC_POLICY at its path:
#   CAML_GC_POLICY=$PWD/string_far.so ./your_program
#
# The only build dependency is the placement ABI header, caml/placement.h,
# taken from the runtime tree this examples directory lives in.

RUNTIME  := ../../runtime
CC       ?= cc
CFLAGS   ?= -O2 -Wall
CPPFLAGS += -I $(RUNTIME) -I $(RUNTIME)/caml

SRCS := $(wildcard *.c)
SOS  := $(SRCS:.c=.so)

all: $(SOS)

%.so: %.c
	$(CC) -shared -fPIC $(CPPFLAGS) $(CFLAGS) $< -o $@

clean:
	rm -f $(SOS)

.PHONY: all clean
