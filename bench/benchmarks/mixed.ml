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

(* Mixed workload: a hot pointer index over cold bulk payloads -- the realistic
   case where placement matters most. Each record holds a small hot field and a
   reference to a large flat byte payload. The hot loop chases the index
   (pointer-bearing, latency-critical) and only rarely touches a payload
   (flat, bulk, cold).

   The ideal split keeps the index in DRAM and pushes the payloads to far. Both
   flat_far (by scannability) and size_threshold (by size) approximate it, and
   should beat all_far -- which slows the hot index -- while using far less DRAM
   than all_dram. Size via BENCH_SIZE (record count). *)

let size = int_of_string (Bench_common.getenv_default "BENCH_SIZE" "1500000")
let payload_bytes = 512

type record = { mutable next : record; payload : bytes; mutable hot : int }

let () =
  Bench_common.main "mixed" (fun () ->
    let rec dummy =
      { next = dummy; payload = Bytes.empty; hot = 0 } in
    let recs =
      Array.init size (fun i ->
        { next = dummy;
          payload = Bytes.make payload_bytes (Char.chr (i land 255));
          hot = i }) in
    let perm = Array.init size (fun i -> i) in
    for i = size - 1 downto 1 do
      let j = Random.int (i + 1) in
      let t = perm.(i) in perm.(i) <- perm.(j); perm.(j) <- t
    done;
    for i = 0 to size - 1 do
      recs.(perm.(i)).next <- recs.(perm.((i + 1) mod size))
    done;
    Gc.minor ();
    (* Hot: chase the index. Cold: touch a payload byte every 64th hop. *)
    let steps = size * 4 in
    let cur = ref recs.(0) in
    let sum = ref 0 in
    for k = 1 to steps do
      let n = !cur.next in
      sum := !sum + n.hot;
      if k land 63 = 0 && Bytes.length n.payload > 0 then
        sum := !sum + Char.code (Bytes.unsafe_get n.payload 0);
      cur := n
    done;
    (recs, !sum))
