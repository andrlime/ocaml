(* TEST
 flags = "-w +A-22-27-32-60-67-70-71-72";
 setup-ocamlc.byte-build-env;
 compile_only = "true";
 ocamlc.byte;
 check-ocamlc.byte-output;
*)

(* The [@far_memory] / [@main_memory] placement hints are accepted on allocating
   expressions and reported as misplaced (warning 53) anywhere else. *)

type t = { x : int }
type v = A | B of int

let _record = ({ x = 0 } [@far_memory])     (* accepted *)
let _tuple = ((1, 2) [@main_memory])        (* accepted *)
let _array = ([| 1; 2; 3 |] [@far_memory])  (* accepted *)
let _variant = ((B 1) [@far_memory])        (* accepted *)
let _poly = ((`Tag 1) [@main_memory])       (* accepted *)

let _const = (42 [@far_memory])             (* rejected: not an allocation *)
let _ident x = (x [@main_memory])           (* rejected: not an allocation *)
