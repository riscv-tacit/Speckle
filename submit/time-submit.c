#include <stdio.h>
#include <sys/wait.h>
#include <inttypes.h>
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

int main(int argc, char **argv) {
  pid_t pid;
  int status = 0;
  struct timespec ts_start, ts_end;

  if (argc < 2) {
    fprintf(stderr, "usage: time-submit <command> [args...]\n");
    return 2;
  }

  clock_gettime_syscall(CLOCK_MONOTONIC, &ts_start);
  uint64_t cycle_start = rdcycle();
  uint64_t instret_start = rdinstret();

  pid = fork();
  if (pid < 0) {
    fprintf(stderr, "failed to fork\n");
    return 1;
  }
  if (pid == 0) {
    execvp(argv[1], &argv[1]);
    perror("execvp");
    _exit(127);
  }

  waitpid(pid, &status, 0);

  clock_gettime_syscall(CLOCK_MONOTONIC, &ts_end);
  uint64_t cycle_end = rdcycle();
  uint64_t instret_end = rdinstret();

  uint64_t elapsed_ns = (uint64_t)(ts_end.tv_sec - ts_start.tv_sec) * 1000000000ULL
                       + (uint64_t)(ts_end.tv_nsec - ts_start.tv_nsec);
  printf("elapsed_ns: %" PRIu64 "\n", elapsed_ns);
  printf("cycles: %" PRIu64 "\n", cycle_end - cycle_start);
  printf("instret: %" PRIu64 "\n", instret_end - instret_start);

  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  return 1;
}
