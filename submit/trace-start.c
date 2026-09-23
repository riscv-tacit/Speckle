#include "tacit.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>

/*
 * Enable tracing without wrapping a process.
 *
 * trace-submit enables the encoder and then forks/execs the workload, so the
 * trace necessarily covers process startup. The tracer is core-wide, not
 * process-scoped, so enabling can be decoupled from launching -- which is what
 * lets a run script warm up first and trace only a steady-state window:
 *
 *   ./intspeed.sh <bmark> --threads 1 &   # untraced (run.sh)
 *   pid=$!
 *   sleep 3                               # warm-up
 *   ./trace-start                         # enable here
 *   sleep 5                               # traced window
 *   ./trace-stop                          # disable, drain, report
 *   kill $pid
 *
 * Target and lossy mode must be set here because trace-submit is not doing it.
 * Both must precede enable -- the driver returns -EBUSY once tracing is on.
 */

static int parse_target(const char *arg)
{
  if (strcmp(arg, "dma") == 0) {
    printf("trace-start: using dma target\n");
    return 1;
  }
  if (strcmp(arg, "fsim") == 0) {
    printf("trace-start: using fsim target\n");
    return 2;
  }
  printf("trace-start: invalid target '%s' (expected dma or fsim)\n", arg);
  return -1;
}

static const char *target_name(int target)
{
  return target == 1 ? "dma" : "fsim";
}

int main(int argc, char **argv) {
  int target = 2;  /* default: fsim, matching trace-submit */
  int lossy = 0;   /* default: lossless */
  long watermark = 0;
  int i = 1;

  while (i < argc) {
    if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
      target = parse_target(argv[i + 1]);
      if (target < 0)
        return 2;
      i += 2;
    } else if (strcmp(argv[i], "--lossy") == 0) {
      lossy = 1;
      i += 1;
    } else if (strcmp(argv[i], "--watermark") == 0 && i + 1 < argc) {
      watermark = strtol(argv[i + 1], NULL, 0);
      i += 2;
    } else {
      fprintf(stderr, "usage: trace-start [--target dma|fsim] [--lossy] "
              "[--watermark N]\n");
      return 2;
    }
  }

  int fd = tacit_open();
  if (fd < 0) {
    fprintf(stderr, "failed to open /dev/tacit0\n");
    return 1;
  }

  if (tacit_target(fd, target) < 0) {
    fprintf(stderr, "failed to set trace target to %s\n", target_name(target));
    tacit_close(fd);
    return 1;
  }

  /* lossy must be set before enable (driver returns -EBUSY otherwise) */
  if (tacit_lossy(fd, lossy) < 0) {
    fprintf(stderr, "failed to set lossy mode\n");
    tacit_close(fd);
    return 1;
  }
  printf("tacit lossy mode: %d\n", lossy);

  uint32_t wm_eff = 0;
  int wm_rc = tacit_apply_resume_wm(fd, (uint32_t)watermark, &wm_eff);
  if (wm_rc == -2) {
    fprintf(stderr, "requested watermark %ld but the encoder reports 0: this "
            "bitstream has no watermark register\n", watermark);
    tacit_close(fd);
    return 1;
  }
  if (wm_rc < 0) {
    fprintf(stderr, "failed to program resume watermark\n");
    tacit_close(fd);
    return 1;
  }
  printf("resume watermark: %u (requested %ld)\n", wm_eff, watermark);

  if (tacit_enable(fd) < 0) {
    fprintf(stderr, "failed to enable tacit\n");
    tacit_close(fd);
    return 1;
  }
  /* Sampled immediately after enable so the bracket covers only traced
   * execution. trace-stop prints the matching window_end_* values. */
  uint64_t cycle_start = tacit_rdcycle();
  uint64_t instret_start = tacit_rdinstret();

  printf("tacit tracing enabled (target %s)\n", target_name(target));
  printf("window_start_cycle: %" PRIu64 "\n", cycle_start);
  printf("window_start_instret: %" PRIu64 "\n", instret_start);

  if (tacit_close(fd) < 0) {
    fprintf(stderr, "failed to close /dev/tacit0\n");
    return 1;
  }
  return 0;
}
