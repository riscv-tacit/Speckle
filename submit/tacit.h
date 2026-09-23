#ifndef TACIT_H
#define TACIT_H

#include <sys/ioctl.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <linux/types.h>

#define TACIT_COMM_LEN 16

struct tacit_log_record {
  uint32_t asid;
  pid_t pid;
  char comm[TACIT_COMM_LEN];
};

#define TACIT_LOG_RECORD_SIZE ((ssize_t)sizeof(struct tacit_log_record))

#define TRACE_IOC_MAGIC      't'
// --- IOCTL commands ---
// Enable the trace encoder
#define TRACE_IOC_ENABLE     _IO(TRACE_IOC_MAGIC, 0)
// Disable the trace encoder
#define TRACE_IOC_DISABLE    _IO(TRACE_IOC_MAGIC, 1)
// Set the trace target
#define TRACE_IOC_TARGET     _IOW(TRACE_IOC_MAGIC, 2, __u8)
// Read trace encoder stall count
#define TRACE_IOC_STALL_COUNT _IOR(TRACE_IOC_MAGIC, 3, __u64)
// Read trace encoder dma count
#define TRACE_IOC_DMA_COUNT   _IOR(TRACE_IOC_MAGIC, 4, __u64)
// Read trace encoder dma wrap count
#define TRACE_IOC_DMA_WRAP_COUNT _IOR(TRACE_IOC_MAGIC, 5, __u32)
// Read trace encoder dma src rdy stall count
#define TRACE_IOC_DMA_SRC_RDY_STALL_COUNT _IOR(TRACE_IOC_MAGIC, 6, __u32)
// Lossy mode: pause/resume instead of stalling the core. Set while disabled.
#define TRACE_IOC_LOSSY           _IOW(TRACE_IOC_MAGIC, 7, __u32)
#define TRACE_IOC_GAP_CYCLES      _IOR(TRACE_IOC_MAGIC, 8, __u64)
#define TRACE_IOC_DROPPED_PACKETS _IOR(TRACE_IOC_MAGIC, 9, __u64)
#define TRACE_IOC_PAUSE_COUNT     _IOR(TRACE_IOC_MAGIC, 10, __u64)
// Lossy resume watermark, absolute queue entries; 0 = encoder default.
// Set while disabled (-EBUSY otherwise). The get returns the EFFECTIVE value
// after the encoder clamps, so writing ~0 and reading back gives this build's
// maximum -- and a read of 0 after writing non-zero means the bitstream has no
// watermark register at all.
#define TRACE_IOC_RESUME_WM       _IOW(TRACE_IOC_MAGIC, 11, __u32)
#define TRACE_IOC_GET_RESUME_WM   _IOR(TRACE_IOC_MAGIC, 12, __u32)

/* Hart-wide cycle/instruction counters, used to bracket a trace window.
 * trace-start samples them just after enable, trace-stop just before disable;
 * the difference is the denominator for the drop-rate metrics. Single-hart
 * targets only -- on a multi-hart config these must be read on the traced hart. */
static inline uint64_t tacit_rdcycle(void) {
  uint64_t val;
  asm volatile("rdcycle %0" : "=r"(val));
  return val;
}

static inline uint64_t tacit_rdinstret(void) {
  uint64_t val;
  asm volatile("rdinstret %0" : "=r"(val));
  return val;
}

static inline int tacit_open(void) {
  const char *devpath = "/dev/tacit0";
  return open(devpath, O_RDWR);
}

static inline int tacit_enable(int fd) {
  return ioctl(fd, TRACE_IOC_ENABLE);
}

static inline int tacit_disable(int fd) {
  return ioctl(fd, TRACE_IOC_DISABLE);
}

static inline int tacit_target(int fd, __u8 target) {
  return ioctl(fd, TRACE_IOC_TARGET, target);
}

static inline int tacit_stall_count(int fd, uint64_t *count) {
  return ioctl(fd, TRACE_IOC_STALL_COUNT, count);
}

static inline int tacit_lossy(int fd, __u32 lossy) {
  return ioctl(fd, TRACE_IOC_LOSSY, lossy);
}

static inline int tacit_gap_cycles(int fd, uint64_t *count) {
  return ioctl(fd, TRACE_IOC_GAP_CYCLES, count);
}

static inline int tacit_dropped_packets(int fd, uint64_t *count) {
  return ioctl(fd, TRACE_IOC_DROPPED_PACKETS, count);
}

static inline int tacit_set_resume_wm(int fd, __u32 wm) {
  return ioctl(fd, TRACE_IOC_RESUME_WM, wm);
}

static inline int tacit_get_resume_wm(int fd, uint32_t *wm) {
  return ioctl(fd, TRACE_IOC_GET_RESUME_WM, wm);
}

/* Program the watermark and verify the hardware agrees. Returns 0 on success.
 * A non-zero request reading back as 0 means this bitstream predates the
 * watermark register: the run would silently use the encoder default while the
 * log claimed otherwise, which would mislabel a whole sweep point. */
static inline int tacit_apply_resume_wm(int fd, __u32 wm, uint32_t *eff) {
  if (tacit_set_resume_wm(fd, wm) < 0) return -1;
  if (tacit_get_resume_wm(fd, eff) < 0) return -1;
  if (wm != 0 && *eff == 0) return -2;
  return 0;
}

static inline int tacit_pause_count(int fd, uint64_t *count) {
  return ioctl(fd, TRACE_IOC_PAUSE_COUNT, count);
}

static inline int tacit_dma_count(int fd, uint64_t *count) {
  return ioctl(fd, TRACE_IOC_DMA_COUNT, count);
}

static inline int tacit_dma_wrap_count(int fd, uint32_t *count) {
  return ioctl(fd, TRACE_IOC_DMA_WRAP_COUNT, count);
}

static inline int tacit_dma_src_rdy_stall_count(int fd, uint32_t *count) {
  return ioctl(fd, TRACE_IOC_DMA_SRC_RDY_STALL_COUNT, count);
}

static inline int tacit_close(int fd) {
  return close(fd);
}

#endif
