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

static inline int clock_gettime_syscall(clockid_t clk_id, struct timespec *ts) {
  return (int)syscall(SYS_clock_gettime, clk_id, ts);
}

static inline uint64_t rdcycle(void) {
  uint64_t val;
  asm volatile("rdcycle %0" : "=r"(val));
  return val;
}

static inline uint64_t rdinstret(void) {
  uint64_t val;
  asm volatile("rdinstret %0" : "=r"(val));
  return val;
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
  int cmd_idx = 1;
  int fd = -1;
  pid_t pid = -1;
  int status = 0;

  if (argc < 2) {
    fprintf(stderr, "usage: trace-submit [--target dma|fsim] <command> [args...]\n");
    return 2;
  }

  if (strcmp(argv[1], "--target") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: trace-submit [--target dma|fsim] <command> [args...]\n");
      return 2;
    }
    target = parse_target(argv[2]);
    if (target < 0) {
      fprintf(stderr, "invalid trace target '%s' (expected dma or fsim)\n", argv[2]);
      return 2;
    }
    cmd_idx = 3;
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

  if (tacit_enable(fd) < 0) {
      fprintf(stderr, "failed to enable tacit\n");
      tacit_close(fd);
      return 1;
  }
  struct timespec ts_start, ts_end;
  clock_gettime_syscall(CLOCK_MONOTONIC, &ts_start);

  uint64_t cycle_start = rdcycle();
  uint64_t instret_start = rdinstret();

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
  uint64_t cycle_end = rdcycle();
  uint64_t instret_end = rdinstret();

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
