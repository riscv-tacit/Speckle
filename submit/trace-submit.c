#include "tacit.h"
#include <stdio.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/syscall.h>
#include <stdlib.h>

static inline int clock_gettime_syscall(clockid_t clk_id, struct timespec *ts) {
  return (int)syscall(SYS_clock_gettime, clk_id, ts);
}

static int parse_target(const char *arg)
{
  if (strcmp(arg, "dma") == 0) {
    printf("trace-submit: using dma target\n");
    return 1;
  }
  if (strcmp(arg, "fsim") == 0) {
    printf("trace-submit: using fsim target\n");
    return 2;
  }
  printf("trace-submit: invalid target '%s' (expected dma or fsim)\n", arg);
  return -1;
}

static const char *target_name(int target)
{
  return target == 1 ? "dma" : "fsim";
}

static void drain_tacit_log(int fd) {
  int flags = fcntl(fd, F_GETFL);
  if (flags >= 0 && !(flags & O_NONBLOCK)) {
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
      perror("fcntl(O_NONBLOCK)");
      return;
    }
  }

  while (1) {
    struct tacit_log_record rec;
    ssize_t ret = read(fd, &rec, sizeof(rec));
    if (ret == (ssize_t)sizeof(rec)) {
      printf("tacit: asid=%u pid=%d comm=%.*s\n",
             rec.asid, rec.pid,
             TACIT_COMM_LEN, rec.comm);
      continue;
    }
    if (ret < 0) {
      if (errno == EAGAIN)
        break;
      perror("read");
      break;
    }
    if (ret == 0) {
      break;
    }
    fprintf(stderr, "short read from tacit log (%zd bytes)\n", ret);
    break;
  }
}

int main(int argc, char **argv) {
  int target = 2;  // default: fsim
  int lossy = 0;
  long watermark = 0;   // 0 = encoder elaboration default
  int cmd_idx = 1;
  int fd = -1;
  pid_t pid = -1;
  int status = 0;

  while (cmd_idx < argc) {
    if (strcmp(argv[cmd_idx], "--target") == 0 && cmd_idx + 1 < argc) {
      target = parse_target(argv[cmd_idx + 1]);
      if (target < 0) {
        fprintf(stderr, "invalid trace target '%s' (expected dma or fsim)\n", argv[cmd_idx + 1]);
        return 2;
      }
      cmd_idx += 2;
    } else if (strcmp(argv[cmd_idx], "--lossy") == 0) {
      lossy = 1;
      cmd_idx += 1;
    } else if (strcmp(argv[cmd_idx], "--watermark") == 0 && cmd_idx + 1 < argc) {
      watermark = strtol(argv[cmd_idx + 1], NULL, 0);
      cmd_idx += 2;
    } else {
      break;
    }
  }

  if (cmd_idx >= argc) {
    fprintf(stderr, "usage: trace-submit [--target dma|fsim] [--lossy] "
            "[--watermark N] <command> [args...]\n");
    return 2;
  }

  if (cmd_idx >= argc) {
    fprintf(stderr, "usage: trace-submit [--target dma|fsim] <command> [args...]\n");
    return 2;
  }

  fd = tacit_open();
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

  /* Watermark before enable: the driver returns -EBUSY once tracing is on. */
  uint32_t wm_eff = 0;
  int wm_rc = tacit_apply_resume_wm(fd, (uint32_t)watermark, &wm_eff);
  if (wm_rc == -2) {
    fprintf(stderr, "requested watermark %ld but the encoder reports 0: this "
            "bitstream has no watermark register; refusing to run so the result "
            "is not mislabelled\n", watermark);
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
  struct timespec ts_start, ts_end;
  clock_gettime_syscall(CLOCK_MONOTONIC, &ts_start);

  uint64_t cycle_start = tacit_rdcycle();
  uint64_t instret_start = tacit_rdinstret();

  pid = fork();
  if (pid < 0) {
    fprintf(stderr, "failed to fork\n");
    tacit_disable(fd);
    tacit_close(fd);
    return 1;
  }
  if (pid == 0) {
    execvp(argv[cmd_idx], &argv[cmd_idx]);
    perror("execvp");
    _exit(127);
  }

  // parent
  waitpid(pid, &status, 0);

  clock_gettime_syscall(CLOCK_MONOTONIC, &ts_end);
  uint64_t cycle_end = tacit_rdcycle();
  uint64_t instret_end = tacit_rdinstret();

  if (tacit_disable(fd) < 0) {
    fprintf(stderr, "failed to disable tacit\n");
    tacit_close(fd);
    return 1;
  }
  drain_tacit_log(fd);
  uint64_t count = 0;
  if (tacit_stall_count(fd, &count) < 0) {
    fprintf(stderr, "failed to get stall count\n");
    tacit_close(fd);
    return 1;
  }
  printf("stall count: %" PRIu64 "\n", count);
  uint64_t gap_cycles = 0, dropped = 0, pauses = 0;
  if (tacit_gap_cycles(fd, &gap_cycles) == 0 &&
      tacit_dropped_packets(fd, &dropped) == 0 &&
      tacit_pause_count(fd, &pauses) == 0) {
    printf("gap cycles: %" PRIu64 " dropped: %" PRIu64 " pauses: %" PRIu64 "\n",
           gap_cycles, dropped, pauses);
  }
  uint64_t dma_count = 0;
  if (tacit_dma_count(fd, &dma_count) < 0) {
    fprintf(stderr, "failed to get dma count\n");
    tacit_close(fd);
    return 1;
  }
  printf("dma count: %" PRIu64 "\n", dma_count);
  uint32_t dma_wrap_count = 0;
  if (tacit_dma_wrap_count(fd, &dma_wrap_count) < 0) {
    fprintf(stderr, "failed to get dma wrap count\n");
    tacit_close(fd);
    return 1;
  }
  printf("dma wrap count: %" PRIu32 "\n", dma_wrap_count);
  /* Cycles the DMA sink had no free TileLink source ID with data waiting: the
   * direct measure of whether the sink was ever the bottleneck. Best-effort so
   * a bitstream without the counter still runs. */
  uint32_t dma_src_rdy_stall = 0;
  if (tacit_dma_src_rdy_stall_count(fd, &dma_src_rdy_stall) == 0)
    printf("dma src rdy stall count: %" PRIu32 "\n", dma_src_rdy_stall);
  uint64_t elapsed_ns = (uint64_t)(ts_end.tv_sec - ts_start.tv_sec) * 1000000000ULL
                       + (uint64_t)(ts_end.tv_nsec - ts_start.tv_nsec);
  printf("elapsed_ns: %" PRIu64 "\n", elapsed_ns);
  printf("cycles: %" PRIu64 "\n", cycle_end - cycle_start);
  printf("instret: %" PRIu64 "\n", instret_end - instret_start);
  if (tacit_close(fd) < 0) {
    fprintf(stderr, "failed to close /dev/tacit0\n");
    return 1;
  }

  return 0;
}
