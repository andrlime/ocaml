(* TEST
 modules = "stubs.c";
*)

(* Objects requested into the far arena land on the far device when it is
   available and fall back to DRAM otherwise, but the allocation never fails.
   Both the pooled (small) and large allocation paths are exercised, then the
   far pools are cycled through a few major collections. *)

external far_available : unit -> bool = "far_arena_available"
external far_place : int -> int = "far_arena_place"

let () =
  let expected = if far_available () then 1 (* device *) else 2 (* DRAM *) in
  List.iter
    (fun words ->
       let where = far_place words in
       assert (where <> 0);
       assert (where = expected))
    [1; 4; 100; 1000; 100_000];
  for _ = 1 to 5 do Gc.full_major () done;
  print_string "ok\n"
