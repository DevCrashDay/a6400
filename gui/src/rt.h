/* Tiny freestanding runtime for the a6400 (Linux 3.0, ARMv7 EABI).
 * Static, no libc: raw syscalls only, so the binary runs regardless of the
 * camera's glibc version. */
#ifndef RT_H
#define RT_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;
typedef unsigned long size_t;
typedef long ssize_t;

#define NULL ((void *)0)

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000

#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED ((void *)-1)

#define WNOHANG 1
#define SIGINT 2
#define SIGKILL 9
#define SIGTERM 15
#define SIGHUP 1
#define SIGPIPE 13

#define ENOENT 2

long sys_call(long n, long a, long b, long c, long d, long e, long f);

#define NR_exit 1
#define NR_fork 2
#define NR_read 3
#define NR_write 4
#define NR_open 5
#define NR_close 6
#define NR_unlink 10
#define NR_execve 11
#define NR_lseek 19
#define NR_getpid 20
#define NR_access 33
#define NR_kill 37
#define NR_dup2 63
#define NR_setsid 66
#define NR_ioctl 54
#define NR_munmap 91
#define NR_wait4 114
#define NR_nanosleep 162
#define NR_rt_sigaction 174
#define NR_mmap2 192
#define NR_clock_gettime 263

static inline ssize_t sys_read(int fd, void *b, size_t n) { return sys_call(NR_read, fd, (long)b, n, 0, 0, 0); }
static inline ssize_t sys_write(int fd, const void *b, size_t n) { return sys_call(NR_write, fd, (long)b, n, 0, 0, 0); }
static inline int sys_open(const char *p, int fl, int mode) { return sys_call(NR_open, (long)p, fl, mode, 0, 0, 0); }
static inline int sys_close(int fd) { return sys_call(NR_close, fd, 0, 0, 0, 0, 0); }
static inline int sys_unlink(const char *p) { return sys_call(NR_unlink, (long)p, 0, 0, 0, 0, 0); }
static inline long sys_lseek(int fd, long off, int wh) { return sys_call(NR_lseek, fd, off, wh, 0, 0, 0); }
static inline int sys_getpid(void) { return sys_call(NR_getpid, 0, 0, 0, 0, 0, 0); }
static inline int sys_access(const char *p, int m) { return sys_call(NR_access, (long)p, m, 0, 0, 0, 0); }
static inline int sys_kill(int pid, int sig) { return sys_call(NR_kill, pid, sig, 0, 0, 0, 0); }
static inline int sys_ioctl(int fd, unsigned long req, void *arg) { return sys_call(NR_ioctl, fd, req, (long)arg, 0, 0, 0); }
static inline int sys_fork(void) { return sys_call(NR_fork, 0, 0, 0, 0, 0, 0); }
static inline int sys_dup2(int a, int b) { return sys_call(NR_dup2, a, b, 0, 0, 0, 0); }
static inline int sys_setsid(void) { return sys_call(NR_setsid, 0, 0, 0, 0, 0, 0); }
static inline int sys_execve(const char *p, char *const *a, char *const *e) { return sys_call(NR_execve, (long)p, (long)a, (long)e, 0, 0, 0); }
static inline int sys_wait4(int pid, int *st, int opt) { return sys_call(NR_wait4, pid, (long)st, opt, 0, 0, 0); }
static inline void *sys_mmap(void *a, size_t len, int prot, int fl, int fd, long pgoff) { return (void *)sys_call(NR_mmap2, (long)a, len, prot, fl, fd, pgoff); }
static inline int sys_munmap(void *a, size_t len) { return sys_call(NR_munmap, (long)a, len, 0, 0, 0, 0); }
__attribute__((noreturn)) void sys_exit(int code);

static inline int is_err(long r) { return (unsigned long)r > (unsigned long)-4096; }

void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);

void msleep(u32 ms);
u32 now_ms(void);

/* printf subset: %s %d %u %x %c %%, optional 0-pad and width */
int fmt(char *buf, size_t cap, const char *f, ...);
void fdprintf(int fd, const char *f, ...);
#define printf(...) fdprintf(1, __VA_ARGS__)

void *xalloc(size_t n);
void xfree(void *p, size_t n);
int read_file(const char *path, char *buf, size_t cap);
int write_file(const char *path, const char *buf, size_t n, int append);

typedef void (*sighandler_t)(int);
int set_signal(int sig, sighandler_t h);

#endif
