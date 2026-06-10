(**************************************************************************)
(*                                                                        *)
(*                                 OCaml                                  *)
(*                                                                        *)
(*                               Andrew Li                                *)
(*                                                                        *)
(*   Copyright 2026 Andrew Li                                             *)
(*                                                                        *)
(*   All rights reserved.  This file is distributed under the terms of    *)
(*   the GNU Lesser General Public License version 2.1, with the          *)
(*   special exception on linking described in the file LICENSE.          *)
(*                                                                        *)
(**************************************************************************)

(* Array traversal: high memory-level parallelism, bandwidth-bound. A set of
   large flat float arrays is scanned sequentially many times. Sequential access
   prefetches well and many loads are in flight at once, so far memory's higher
   latency is largely hidden and its bandwidth is what matters -- this is the
   workload where far placement is nearly free.

   Float arrays are flat (No_scan), so flat_far/size_threshold route them to far
   with little penalty. Size via BENCH_SIZE (total words across the arrays). *)

let size = int_of_string (Bench_common.getenv_default "BENCH_SIZE" "4000000")

(* Split into chunks so each array is a separate placeable major-heap object. *)
let chunk = 65536

let () =
  Bench_common.main "arraytraverse" (fun () ->
    let n_arrays = (size + chunk - 1) / chunk in
    let arrays =
      Array.init n_arrays (fun k ->
        Array.init chunk (fun i -> float_of_int (i + k))) in
    Gc.minor ();
    (* Several sequential passes accumulating every element. *)
    let sum = ref 0.0 in
    for _ = 1 to 8 do
      Array.iter (fun a ->
        for i = 0 to Array.length a - 1 do sum := !sum +. a.(i) done)
        arrays
    done;
    (arrays, !sum))
