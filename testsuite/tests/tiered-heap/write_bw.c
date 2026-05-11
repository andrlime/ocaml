/* Sequential and random write-bandwidth benchmark: DRAM vs FAR (devdax).
 *
 * Compile:
 *   gcc -O2 -o write_bw write_bw.c && ./write_bw
 *
 * Output: one line per measurement, tab-separated fields:
 *   arena  pattern  bw_gib_s  lat_ns_per_line  status
 * where status is "measured" or "skip:<reason>".
 *
 * Handles devdax SIGBUS gracefully — if the device is present but the
 * physical backing is inaccessible (e.g. emulated PMEM, bad MCE), FAR
 * rows are emitted with status "skip:sigbus". */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#define BENCH_BYTES  (256ULL * 1024 * 1024)
#define CACHELINE    64
#define NRUNS        5

/* ---- SIGBUS fence ---- */
static volatile int        g_sigbus_armed = 0;
static sigjmp_buf          g_sigbus_jmp;

static void sigbus_handler(int sig) {
  (void)sig;
  if (g_sigbus_armed) siglongjmp(g_sigbus_jmp, 1);
  _exit(135);
}

static void arm_sigbus(void)   { g_sigbus_armed = 1; }
static void disarm_sigbus(void){ g_sigbus_armed = 0; }

/* ---- Timing ---- */
static double now_ns(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec * 1e9 + t.tv_nsec;
}

/* ---- Workloads ---- */
static void seq_write(void* mem, size_t bytes) {
  volatile uint64_t* p = mem;
  size_t stride = CACHELINE / sizeof(uint64_t);
  size_t n = bytes / sizeof(uint64_t);
  for (size_t i = 0; i < n; i += stride) p[i] = i;
}

static void rand_write(void* mem, size_t bytes) {
  volatile uint64_t* p = mem;
  size_t stride = CACHELINE / sizeof(uint64_t);
  size_t nlines = bytes / CACHELINE;
  uint64_t idx = 1;
  for (size_t i = 0; i < nlines; i++) {
    p[idx * stride] = idx;
    idx = (idx * 6364136223846793005ULL + 1442695040888963407ULL) % nlines;
  }
}

typedef void (*bench_fn)(void*, size_t);

static double mean(const double* v, int n) {
  double s = 0; for (int i = 0; i < n; i++) s += v[i]; return s / n;
}
static double stddev(const double* v, int n, double m) {
  double s = 0; for (int i = 0; i < n; i++) s += (v[i]-m)*(v[i]-m);
  return n > 1 ? sqrt(s / (n-1)) : 0.0;
}

/* Returns 1 on success, 0 on SIGBUS. */
static int run_bench(const char* arena, const char* pattern,
                     bench_fn fn, void* mem, size_t bytes) {
  size_t nlines = bytes / CACHELINE;
  double bw_runs[NRUNS], lat_runs[NRUNS];

  /* warm-up with SIGBUS guard */
  arm_sigbus();
  if (sigsetjmp(g_sigbus_jmp, 1)) {
    disarm_sigbus();
    printf("%s\t%s\t0\t0\t0\t0\tskip:sigbus\n", arena, pattern);
    fflush(stdout);
    return 0;
  }
  fn(mem, bytes);
  disarm_sigbus();

  for (int r = 0; r < NRUNS; r++) {
    arm_sigbus();
    if (sigsetjmp(g_sigbus_jmp, 1)) {
      disarm_sigbus();
      printf("%s\t%s\t0\t0\t0\t0\tskip:sigbus\n", arena, pattern);
      fflush(stdout);
      return 0;
    }
    double t0 = now_ns();
    fn(mem, bytes);
    double dt = now_ns() - t0;
    disarm_sigbus();
    bw_runs[r]  = (double)bytes / dt;
    lat_runs[r] = dt / (double)nlines;
  }

  double bw_m  = mean(bw_runs,  NRUNS);
  double bw_s  = stddev(bw_runs,  NRUNS, bw_m);
  double lat_m = mean(lat_runs, NRUNS);
  double lat_s = stddev(lat_runs, NRUNS, lat_m);

  printf("%s\t%s\t%.4f\t%.4f\t%.2f\t%.2f\tmeasured\n",
         arena, pattern, bw_m, bw_s, lat_m, lat_s);
  fflush(stdout);
  return 1;
}

int main(void) {
  struct sigaction sa = { .sa_handler = sigbus_handler, .sa_flags = SA_RESETHAND };
  sigemptyset(&sa.sa_mask);
  sigaction(SIGBUS, &sa, NULL);

  fprintf(stderr, "bench_bytes=%zuMiB  nruns=%d  cacheline=%dB\n",
          (size_t)(BENCH_BYTES >> 20), NRUNS, CACHELINE);
  printf("arena\tpattern\tbw_mean\tbw_std\tlat_mean\tlat_std\tstatus\n");

  /* ---- DRAM ---- */
  void* dram = mmap(NULL, BENCH_BYTES, PROT_READ|PROT_WRITE,
                    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (dram == MAP_FAILED) { perror("mmap DRAM"); return 1; }
  memset(dram, 0, BENCH_BYTES);

  run_bench("DRAM", "seq-write",  seq_write,  dram, BENCH_BYTES);
  run_bench("DRAM", "rand-write", rand_write, dram, BENCH_BYTES);
  munmap(dram, BENCH_BYTES);

  /* ---- FAR (devdax) ---- */
  const char* dev = getenv("OCAML_DAX_DEVICE");
  if (!dev || !*dev) dev = "/dev/dax2.0";

  int fd = open(dev, O_RDWR);
  if (fd < 0) {
    printf("FAR\tseq-write\t0\t0\t0\t0\tskip:no-device\n");
    printf("FAR\trand-write\t0\t0\t0\t0\tskip:no-device\n");
    return 0;
  }

  void* far = mmap(NULL, BENCH_BYTES, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (far == MAP_FAILED) {
    printf("FAR\tseq-write\t0\t0\t0\t0\tskip:mmap-failed\n");
    printf("FAR\trand-write\t0\t0\t0\t0\tskip:mmap-failed\n");
    close(fd);
    return 0;
  }

  /* pre-fault with SIGBUS guard; skip patterns if the device faults */
  arm_sigbus();
  int pre_ok = 1;
  if (sigsetjmp(g_sigbus_jmp, 1)) {
    pre_ok = 0;
    disarm_sigbus();
  } else {
    memset(far, 0, BENCH_BYTES);
    disarm_sigbus();
  }

  if (!pre_ok) {
    printf("FAR\tseq-write\t0\t0\t0\t0\tskip:sigbus\n");
    printf("FAR\trand-write\t0\t0\t0\t0\tskip:sigbus\n");
  } else {
    run_bench("FAR", "seq-write",  seq_write,  far, BENCH_BYTES);
    run_bench("FAR", "rand-write", rand_write, far, BENCH_BYTES);
  }

  munmap(far, BENCH_BYTES);
  close(fd);
  return 0;
}
