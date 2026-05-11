(* Test: verify that OCAML_FAR_ALL=1 routes promoted objects into the DAX
 * arena, and that OCAML_FAR_ALL unset leaves them in DRAM.
 *
 * Skips gracefully if /dev/dax2.0 is absent.
 *
 * To run manually:
 *   OCAML_FAR_ALL=1 ./dax_placement.byte    (should print PASS for FAR checks)
 *   ./dax_placement.byte                    (should print PASS for DRAM checks)
 * To run with the address-range debug assertions enabled, build the runtime
 * with -DCAML_DEBUG_FAR_ARENA and the test will abort on any mismatch. *)

external dax_available : unit -> bool       = "caml_test_dax_available"
external dax_contains  : 'a -> bool         = "caml_test_dax_contains"
external dax_extent    : unit -> nativeint * nativeint
                                            = "caml_test_dax_extent"

let skip msg = Printf.printf "SKIP: %s\n%!" msg; exit 0
let fail msg = Printf.printf "FAIL: %s\n%!" msg; exit 1
let pass msg = Printf.printf "PASS: %s\n%!" msg

(* ------------------------------------------------------------------ *)
(* Helpers                                                              *)
(* ------------------------------------------------------------------ *)

(* Force [v] into the major heap so the arena check is meaningful.
 * We minor-collect twice: once to move the object out of the nursery,
 * and once to make sure any forwarding pointers are resolved. *)
let promote_to_major v =
  Gc.minor ();
  Gc.minor ();
  v

(* Allocate a mix of small (pool) and large (>SIZECLASS_MAX) objects. *)
let alloc_small () = Array.make 4 42          (* fits in a pool sizeclass *)
let alloc_large () = Array.make 4096 0xdeadbeef (* exceeds SIZECLASS_MAX, ~32 KiB *)

(* ------------------------------------------------------------------ *)
(* Main                                                                 *)
(* ------------------------------------------------------------------ *)

let () =
  if not (dax_available ()) then
    skip "DAX arena unavailable (no /dev/dax2.0 or insufficient permissions)";

  let base, total = dax_extent () in
  Printf.printf "DAX arena: base=0x%nx  total=%nd bytes (%.0f MiB)\n%!"
    base total (Int64.to_float (Int64.of_nativeint total) /. (1024. *. 1024.));

  (* Determine which arena we expect based on OCAML_FAR_ALL. *)
  let far_all =
    match Sys.getenv_opt "OCAML_FAR_ALL" with
    | Some s when s <> "" && s <> "0" -> true
    | _ -> false
  in
  Printf.printf "OCAML_FAR_ALL=%b\n%!" far_all;

  (* ---- Small objects ---- *)
  let n_small = 50_000 in
  let smalls = Array.init n_small (fun _ -> alloc_small ()) in
  let _ = promote_to_major smalls in
  (* Probe a sample: checking all 50k would be slow. Take every 100th. *)
  let small_mismatches = ref 0 in
  for i = 0 to n_small / 100 - 1 do
    let in_dax = dax_contains smalls.(i * 100) in
    if far_all && not in_dax then incr small_mismatches;
    if not far_all && in_dax then incr small_mismatches;
  done;
  if !small_mismatches = 0 then
    pass (Printf.sprintf
      "small objects (%d sampled): all in expected arena (far=%b)"
      (n_small / 100) far_all)
  else
    fail (Printf.sprintf
      "small objects: %d/%d in wrong arena (far=%b)"
      !small_mismatches (n_small / 100) far_all);

  (* ---- Large objects ---- *)
  let n_large = 20 in
  let larges = Array.init n_large (fun _ -> alloc_large ()) in
  let _ = promote_to_major larges in
  let large_mismatches = ref 0 in
  for i = 0 to n_large - 1 do
    let in_dax = dax_contains larges.(i) in
    if far_all && not in_dax then incr large_mismatches;
    if not far_all && in_dax then incr large_mismatches;
  done;
  if !large_mismatches = 0 then
    pass (Printf.sprintf
      "large objects (%d): all in expected arena (far=%b)" n_large far_all)
  else
    fail (Printf.sprintf
      "large objects: %d/%d in wrong arena (far=%b)"
      !large_mismatches n_large far_all);

  (* ---- Compaction smoke-check ---- *)
  (* Compact should not crash even with FAR objects present. *)
  Gc.compact ();
  pass "Gc.compact() completed without crash";

  (* ---- Summary ---- *)
  Printf.printf "\nAll checks passed (OCAML_FAR_ALL=%b).\n%!" far_all
