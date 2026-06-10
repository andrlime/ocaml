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

(* Shared harness for the placement benchmarks: time a workload, read the GC
   and placement-engine counters around it, and print one CSV row. The policy
   and DRAM cap are set by the environment (CAML_GC_POLICY /
   CAML_DRAM_CAP_WORDS, driven by run.sh) and echoed into the row so the CSV is
   self-describing. *)

external arena_committed : int -> int = "bench_arena_committed"
external decision_count : unit -> int = "bench_decision_count"
external far_available : unit -> bool = "bench_far_available"
external now_ns : unit -> float = "bench_now_ns"

let dram = 0
let far = 1

let getenv_default name default =
  match Sys.getenv_opt name with Some s when s <> "" -> s | _ -> default

(* CSV header, printed once by the first invocation (run.sh dedups). *)
let header =
  "bench,policy,dram_cap,rep,wall_ms,dram_committed,far_committed,\
   minor_colls,major_colls,minor_words,decision_count"

(* Run [workload] once, fully collecting afterwards so promotion and arena
   placement have settled before we read occupancy, and print the row for
   benchmark [name]. [workload] should build and retain its working set; the
   harness keeps the returned value live across the measurement. *)
let measure name (workload : unit -> 'a) =
  let policy = getenv_default "CAML_GC_POLICY" "all_dram" in
  let cap = getenv_default "CAML_DRAM_CAP_WORDS" "0" in
  let rep = getenv_default "BENCH_REP" "0" in

  let g0 = Gc.quick_stat () in
  let t0 = now_ns () in
  let result = workload () in
  let t1 = now_ns () in
  Sys.opaque_identity result |> ignore;
  Gc.full_major ();
  let g1 = Gc.quick_stat () in

  let wall_ms = (t1 -. t0) /. 1e6 in
  Printf.printf "%s,%s,%s,%s,%.3f,%d,%d,%d,%d,%.0f,%d\n"
    name policy cap rep wall_ms
    (arena_committed dram) (arena_committed far)
    (g1.minor_collections - g0.minor_collections)
    (g1.major_collections - g0.major_collections)
    (g1.minor_words -. g0.minor_words)
    (decision_count ())

(* Entry point for a benchmark: with [--header] print the CSV header and exit;
   otherwise time [workload] and print its row. *)
let main name workload =
  if Array.length Sys.argv > 1 && Sys.argv.(1) = "--header" then
    (print_endline header; exit 0)
  else
    measure name workload
