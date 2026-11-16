#include "rt.h"

typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_arg __builtin_va_arg
#define va_end __builtin_va_end

__asm__(
    ".text\n"
    ".global _start\n"
    ".type _start,%function\n"
    "_start:\n"
    "  mov fp, #0\n"
    "  mov lr, #0\n"
    "  mov r0, sp\n"
    "  bic sp, sp, #7\n"
    "  bl rt_cstart\n"
    "  b .\n"
    "\n"
    ".global sys_call\n"
    ".type sys_call,%function\n"
    "sys_call:\n"
    "  push {r4, r5, r7, lr}\n"
    "  mov r7, r0\n"
    "  mov r0, r1\n"
    "  mov r1, r2\n"
    "  mov r2, r3\n"
    "  ldr r3, [sp, #16]\n"
    "  ldr r4, [sp, #20]\n"
    "  ldr r5, [sp, #24]\n"
    "  svc #0\n"
    "  pop {r4, r5, r7, pc}\n"
    "\n"
    ".global rt_sigreturn_tramp\n"
    ".type rt_sigreturn_tramp,%function\n"
    "rt_sigreturn_tramp:\n"
    "  mov r7, #173\n" /* __NR_rt_sigreturn */
    "  svc #0\n");

int main(int argc, char **argv, char **envp);
char **rt_environ;

void rt_cstart(long *sp)
{
    int argc = (int)sp[0];
    char **argv = (char **)(sp + 1);
    rt_environ = argv + argc + 1;
    sys_exit(main(argc, argv, rt_environ));
}

void sys_exit(int code)
{
    for (;;)
        sys_call(248 /* exit_group */, code, 0, 0, 0, 0, 0);
}

/* ---- compiler support (Cortex-A5 has no hardware divide) ---- */
static u32 udivmod(u32 n, u32 d, u32 *rem)
{
    u32 q = 0, r = 0;
    int i;
    if (d == 0) {
        *rem = 0;
        return 0;
    }
    for (i = 31; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= 1u << i;
        }
    }
    *rem = r;
    return q;
}
u32 __aeabi_uidiv(u32 n, u32 d) { u32 r; return udivmod(n, d, &r); }
unsigned long long __aeabi_uidivmod(u32 n, u32 d)
{
    u32 r, q = udivmod(n, d, &r);
    return (unsigned long long)q | ((unsigned long long)r << 32);
}
s32 __aeabi_idiv(s32 n, s32 d)
{
    u32 r, q = udivmod(n < 0 ? -(u32)n : (u32)n, d < 0 ? -(u32)d : (u32)d, &r);
    return ((n < 0) ^ (d < 0)) ? -(s32)q : (s32)q;
}
unsigned long long __aeabi_idivmod(s32 n, s32 d)
{
    u32 r, q = udivmod(n < 0 ? -(u32)n : (u32)n, d < 0 ? -(u32)d : (u32)d, &r);
    s32 sq = ((n < 0) ^ (d < 0)) ? -(s32)q : (s32)q;
    s32 sr = n < 0 ? -(s32)r : (s32)r;
    return (unsigned long long)(u32)sq | ((unsigned long long)(u32)sr << 32);
}

void *memcpy(void *d, const void *s, size_t n)
{
    u8 *dd = d;
    const u8 *ss = s;
    if ((((unsigned long)dd | (unsigned long)ss | n) & 3) == 0) {
        u32 *dw = (u32 *)dd;
        const u32 *sw = (const u32 *)ss;
        for (n >>= 2; n; n--)
            *dw++ = *sw++;
        return d;
    }
    while (n--)
        *dd++ = *ss++;
    return d;
}
void *memmove(void *d, const void *s, size_t n)
{
    u8 *dd = d;
    const u8 *ss = s;
    if (dd < ss)
        while (n--)
            *dd++ = *ss++;
    else
        while (n--)
            dd[n] = ss[n];
    return d;
}
void *memset(void *d, int c, size_t n)
{
    u8 *dd = d;
    while (n--)
        *dd++ = (u8)c;
    return d;
}
void __aeabi_memcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memcpy4(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memcpy8(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memmove(void *d, const void *s, size_t n) { memmove(d, s, n); }
void __aeabi_memmove4(void *d, const void *s, size_t n) { memmove(d, s, n); }
void __aeabi_memset(void *d, size_t n, int c) { memset(d, c, n); }
void __aeabi_memset4(void *d, size_t n, int c) { memset(d, c, n); }
void __aeabi_memclr(void *d, size_t n) { memset(d, 0, n); }
void __aeabi_memclr4(void *d, size_t n) { memset(d, 0, n); }
void __aeabi_memclr8(void *d, size_t n) { memset(d, 0, n); }

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (u8)*a - (u8)*b;
}
int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b || !*a)
            return (u8)*a - (u8)*b;
    }
    return 0;
}
char *strchr(const char *s, int c)
{
    for (; *s; s++)
        if (*s == (char)c)
            return (char *)s;
    return c ? NULL : (char *)s;
}

struct ts {
    long sec, nsec;
};
void msleep(u32 ms)
{
    struct ts t = {ms / 1000, (ms % 1000) * 1000000};
    sys_call(NR_nanosleep, (long)&t, 0, 0, 0, 0, 0);
}
u32 now_ms(void)
{
    struct ts t;
    sys_call(NR_clock_gettime, 1 /* CLOCK_MONOTONIC */, (long)&t, 0, 0, 0, 0);
    return (u32)t.sec * 1000u + (u32)t.nsec / 1000000u;
}

static int vfmt(char *buf, size_t cap, const char *f, va_list ap)
{
    size_t o = 0;
#define PUT(ch)                 \
    do {                        \
        char _c = (ch);         \
        if (o + 1 < cap)        \
            buf[o] = _c;        \
        o++;                    \
    } while (0)
    for (; *f; f++) {
        char tmp[12];
        const char *s;
        int width = 0, zero = 0, len, neg = 0;
        u32 v;
        if (*f != '%') {
            PUT(*f);
            continue;
        }
        f++;
        if (*f == '0')
            zero = 1, f++;
        while (*f >= '0' && *f <= '9')
            width = width * 10 + (*f++ - '0');
        switch (*f) {
        case 's':
            s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            len = strlen(s);
            for (; width > len; width--)
                PUT(' ');
            while (*s)
                PUT(*s++);
            continue;
        case 'c':
            PUT((char)va_arg(ap, int));
            continue;
        case 'd':
        case 'u':
        case 'x': {
            int base = *f == 'x' ? 16 : 10;
            v = va_arg(ap, u32);
            if (*f == 'd' && (s32)v < 0)
                neg = 1, v = -(s32)v;
            len = 0;
            do {
                u32 dgt = v % base;
                tmp[len++] = dgt < 10 ? '0' + dgt : 'a' + dgt - 10;
                v /= base;
            } while (v);
            if (neg)
                width--;
            if (neg && zero)
                PUT('-');
            for (; width > len; width--)
                PUT(zero ? '0' : ' ');
            if (neg && !zero)
                PUT('-');
            while (len)
                PUT(tmp[--len]);
            continue;
        }
        case '%':
            PUT('%');
            continue;
        default:
            PUT('%');
            PUT(*f);
        }
    }
    if (cap)
        buf[o < cap ? o : cap - 1] = 0;
    return (int)o;
#undef PUT
}

int fmt(char *buf, size_t cap, const char *f, ...)
{
    va_list ap;
    int r;
    va_start(ap, f);
    r = vfmt(buf, cap, f, ap);
    va_end(ap);
    return r;
}

void fdprintf(int fd, const char *f, ...)
{
    char buf[2048];
    va_list ap;
    int n;
    va_start(ap, f);
    n = vfmt(buf, sizeof buf, f, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 1)
        n = sizeof buf - 1;
    sys_write(fd, buf, n);
}

void *xalloc(size_t n)
{
    void *p = sys_mmap(NULL, (n + 4095) & ~4095ul, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return is_err((long)p) ? NULL : p;
}
void xfree(void *p, size_t n)
{
    if (p)
        sys_munmap(p, (n + 4095) & ~4095ul);
}

int read_file(const char *path, char *buf, size_t cap)
{
    int fd = sys_open(path, O_RDONLY, 0), n, tot = 0;
    if (fd < 0)
        return fd;
    while (tot + 1 < (int)cap && (n = sys_read(fd, buf + tot, cap - 1 - tot)) > 0)
        tot += n;
    buf[tot] = 0;
    sys_close(fd);
    return tot;
}

int write_file(const char *path, const char *buf, size_t n, int append)
{
    int fd = sys_open(path, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644), r;
    if (fd < 0)
        return fd;
    r = sys_write(fd, buf, n);
    sys_close(fd);
    return r;
}

struct ksigaction {
    sighandler_t handler;
    unsigned long flags;
    void (*restorer)(void);
    u32 mask[2];
};
extern void rt_sigreturn_tramp(void);
int set_signal(int sig, sighandler_t h)
{
    struct ksigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.handler = h;
    sa.flags = 0x04000000 /* SA_RESTORER */ | 4 /* SA_SIGINFO: rt frame matches rt_sigreturn */;
    sa.restorer = rt_sigreturn_tramp;
    return sys_call(NR_rt_sigaction, sig, (long)&sa, 0, 8, 0, 0);
}
