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

# Build the placement benchmarks against the in-tree compiler. ROOT points at
# the ocaml-new checkout; override if building from elsewhere.
ROOT    ?= ../..
OCAMLOPT = $(ROOT)/ocamlopt.opt -I $(ROOT)/stdlib
RUNTIME  = -I $(ROOT)/runtime -ccopt -I$(ROOT)/runtime

BENCHES = ptrchase arraytraverse mixed

all: $(BENCHES)

benchstat.o: benchstat.c
	$(OCAMLOPT) $(RUNTIME) -c benchstat.c

bench_common.cmx: bench_common.ml
	$(OCAMLOPT) -c bench_common.ml

# Each benchmark links the shared harness module and the C stat stub.
$(BENCHES): %: %.ml bench_common.cmx benchstat.o
	$(OCAMLOPT) bench_common.cmx $*.ml benchstat.o -o $@

clean:
	rm -f *.cm* *.o $(BENCHES)

.PHONY: all clean
