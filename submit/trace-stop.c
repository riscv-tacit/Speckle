#include "tacit.h"
#include <stdio.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <fcntl.h>

static void drain_tacit_log(int fd) {
  printf("draining tacit log\n");
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
  int fd = tacit_open();
    if (fd < 0) {
    fprintf(stderr, "failed to open /dev/tacit0\n");
    return 1;
  }

  /* Sampled before disable so the bracket covers only traced execution;
   * trace-start printed the matching window_start_* values. */
  uint64_t cycle_end = tacit_rdcycle();
  uint64_t instret_end = tacit_rdinstret();

  if (tacit_disable(fd) < 0) {
    fprintf(stderr, "failed to disable tacit\n");
    return 1;
  }
  uint32_t wm_eff = 0;
  if (tacit_get_resume_wm(fd, &wm_eff) == 0)
    printf("resume watermark: %u\n", wm_eff);
  printf("window_end_cycle: %" PRIu64 "\n", cycle_end);
  printf("window_end_instret: %" PRIu64 "\n", instret_end);

  uint64_t count;
  if (tacit_stall_count(fd, &count) < 0) {
    fprintf(stderr, "failed to get stall count\n");
    return 1;
  }
  printf("stall count: %" PRIu64 "\n", count);

  /* Lossy-mode counters, in trace-submit's line format so one parser handles
   * both. Best-effort: a bitstream without the lossy registers reports nothing
   * rather than failing the run. */
  uint64_t gap_cycles = 0, dropped = 0, pauses = 0;
  if (tacit_gap_cycles(fd, &gap_cycles) == 0 &&
      tacit_dropped_packets(fd, &dropped) == 0 &&
      tacit_pause_count(fd, &pauses) == 0) {
    printf("gap cycles: %" PRIu64 " dropped: %" PRIu64 " pauses: %" PRIu64 "\n",
           gap_cycles, dropped, pauses);
  }

  /* DMA sink counters. Best-effort too: trace-stop is also used with the fsim
   * target, where these are meaningless (and the sink may be absent). */
  uint64_t dma_count = 0;
  if (tacit_dma_count(fd, &dma_count) == 0)
    printf("dma count: %" PRIu64 "\n", dma_count);
  uint32_t dma_wrap_count = 0;
  if (tacit_dma_wrap_count(fd, &dma_wrap_count) == 0)
    printf("dma wrap count: %" PRIu32 "\n", dma_wrap_count);
  /* Cycles the DMA sink had no free TileLink source ID, i.e. cycles the sink
   * itself could not accept a beat. This is the direct measure of whether the
   * sink was ever the bottleneck -- without it, sink pressure can only be
   * inferred from bandwidth arithmetic. */
  uint32_t dma_src_rdy_stall = 0;
  if (tacit_dma_src_rdy_stall_count(fd, &dma_src_rdy_stall) == 0)
    printf("dma src rdy stall count: %" PRIu32 "\n", dma_src_rdy_stall);

  drain_tacit_log(fd);
  if (tacit_close(fd) < 0) {
    fprintf(stderr, "failed to close /dev/tacit0\n");
    return 1;
  }
  return 0;
}
