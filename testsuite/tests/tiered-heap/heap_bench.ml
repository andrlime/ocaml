(* Measures promotion latency to the DRAM and FAR major heaps.
 *
 * Allocates [n_objs] small arrays in the minor heap, then calls Gc.minor ()
 * to promote them.  The promotion path goes through
 *   oldify_one -> alloc_shared -> caml_shared_try_alloc_arena
 * which is the exact code that writes objects into the DRAM or FAR pool.
 *
 * Run twice:
 *   ./heap_bench.exe                   -> DRAM
 *   OCAML_FAR_ALL=1 ./heap_bench.exe   -> FAR
 *
 * Output: TSV row compatible with plots/main.py. *)

let n_objs   = 2_000_000
let obj_words = 7          (* 7 fields + 1 header = 64 bytes = 1 cacheline *)
let n_runs   = 30

let obj_bytes = (obj_words + 1) * 8

let now_ns () =
  let t = Unix.gettimeofday () in
  t *. 1e9

let mean arr =
  Array.fold_left ( +. ) 0. arr /. float (Array.length arr)

let stddev arr m =
  let sq = Array.fold_left (fun a x -> a +. (x -. m) ** 2.) 0. arr in
  sqrt (sq /. float (Array.length arr - 1))

(* Allocate a batch in the minor heap, return the elapsed promotion ns. *)
let promote_batch () =
  (* Keep a reference so the objects survive until after Gc.minor (). *)
  let batch = Array.init n_objs (fun _ -> Array.make obj_words 0) in
  let t0 = now_ns () in
  Gc.minor ();
  let t1 = now_ns () in
  let _ = Sys.opaque_identity batch in
  t1 -. t0

let () =
  let arena = match Sys.getenv_opt "OCAML_FAR_ALL" with
    | Some s when s <> "" && s <> "0" -> "FAR"
    | _ -> "DRAM"
  in

  (* Warm-up: two full batches to populate pool free-lists. *)
  ignore (promote_batch ());
  ignore (promote_batch ());

  let times_ns = Array.init n_runs (fun _ -> promote_batch ()) in

  (* Convert to per-object latency and effective write bandwidth. *)
  let lat_arr = Array.map (fun dt -> dt /. float n_objs) times_ns in
  let bw_arr  = Array.map (fun dt ->
    float (n_objs * obj_bytes) /. dt   (* bytes/ns == GiB/s *)
  ) times_ns in

  let lat_m = mean lat_arr   and lat_s = stddev lat_arr (mean lat_arr) /. sqrt (float n_runs) in
  let bw_m  = mean bw_arr    and bw_s  = stddev bw_arr  (mean bw_arr)  /. sqrt (float n_runs) in

  (* Print header on first line only if env var says so, to allow
   * the two runs (DRAM then FAR) to be concatenated cleanly. *)
  (match Sys.getenv_opt "HEAP_BENCH_HEADER" with
   | Some "1" ->
     Printf.printf "arena\tpattern\tbw_mean\tbw_std\tlat_mean\tlat_std\tstatus\n%!"
   | _ -> ());

  Printf.printf "%s\tpromote\t%.4f\t%.4f\t%.2f\t%.2f\tmeasured\n%!"
    arena bw_m bw_s lat_m lat_s
