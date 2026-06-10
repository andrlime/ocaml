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

(* Pointer-chasing: low memory-level parallelism, latency-bound. A large set of
   pointer-bearing records is linked into one random cycle, then chased for many
   dependent steps -- each load depends on the previous, so nothing prefetches
   and the access latency of wherever the nodes live dominates.

   This is the workload that punishes far placement most. The records are
   scannable, so flat_far/attribute keep them in DRAM while all_far banishes
   them to slow memory. Size via BENCH_SIZE (node count). *)

type node = { mutable next : node; mutable acc : int }

let size = int_of_string (Bench_common.getenv_default "BENCH_SIZE" "2000000")

let () =
  Bench_common.main "ptrchase" (fun () ->
    let rec dummy = { next = dummy; acc = 0 } in
    let nodes = Array.init size (fun i -> { next = dummy; acc = i }) in
    (* Random permutation (Fisher-Yates), then link each node to the next in
       permuted order, closing the cycle. *)
    let perm = Array.init size (fun i -> i) in
    for i = size - 1 downto 1 do
      let j = Random.int (i + 1) in
      let t = perm.(i) in perm.(i) <- perm.(j); perm.(j) <- t
    done;
    for i = 0 to size - 1 do
      nodes.(perm.(i)).next <- nodes.(perm.((i + 1) mod size))
    done;
    (* Promote the structure to the major heap so the policy has placed it. *)
    Gc.minor ();
    (* Chase: several laps of dependent loads. *)
    let steps = size * 4 in
    let cur = ref nodes.(0) in
    let sum = ref 0 in
    for _ = 1 to steps do
      let n = !cur.next in
      sum := !sum + n.acc;
      cur := n
    done;
    (nodes, !sum))
