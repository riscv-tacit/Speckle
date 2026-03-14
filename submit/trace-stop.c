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

  if (tacit_disable(fd) < 0) {
    fprintf(stderr, "failed to disable tacit\n");
    return 1;
  }
  uint64_t count;
  if (tacit_stall_count(fd, &count) < 0) {
    fprintf(stderr, "failed to get stall count\n");
    return 1;
  }
  printf("stall count: %" PRIu64 "\n", count);
  drain_tacit_log(fd);
  if (tacit_close(fd) < 0) {
    fprintf(stderr, "failed to close /dev/tacit0\n");
    return 1;
  }
  return 0;
}
