(* TEST
 setup-ocamlc.byte-build-env;
 flags = "-dlambda -dno-unique-ids";
 ocamlc.byte;
 check-ocamlc.byte-output;
*)

(* The [@far_memory] / [@main_memory] attributes thread through to the
   [makeblock] primitive as a placement hint; unannotated allocations carry
   none. *)

type t = { a : int; b : int }

let record x = ({ a = x; b = x } [@far_memory])

let tuple x = ((x, x) [@main_memory])

let constructor x = (Some x [@far_memory])

let plain x = (x, x)
