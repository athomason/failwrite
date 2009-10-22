/*
 * LD_PRELOAD-able overrides for glibc functions.
 * Inspiration from libfiu: http://blitiri.com.ar/p/libfiu/
 */

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>

#define WRITE_ERROR ENOSPC
#define DEBUG 1

void* _libc;
int __thread _reentered = 0;

typedef int     (*open_t)(const char*, int, ...);
typedef int     (*close_t)(int);
typedef ssize_t (*write_t)(int, const void*, size_t);

static open_t  _orig_open  = NULL;
static close_t _orig_close = NULL;
static write_t _orig_write = NULL;

static regex_t reg;
char* watched_fds;

void __attribute__((constructor)) _fw_init(void)
{
    static int initialized = 0;
    if (initialized)
        return;

    /* initialize original function pointers */
    _libc = dlopen("libc.so.6", RTLD_NOW);
    if (_libc == NULL) {
        fprintf(stderr, "Error loading libc: %s\n", dlerror());
        exit(1);
    }
    _orig_open  = (open_t)  dlsym(_libc, "open");
    _orig_close = (close_t) dlsym(_libc, "close");
    _orig_write = (write_t) dlsym(_libc, "write");

    /* initialize regex matcher */
    char* pattern = getenv("FAILWRITE_PATTERN");
    if (pattern == NULL) {
        fprintf(stderr, "FAILWRITE_PATTERN not set\n");
        exit(1);
    }
    regcomp(&reg, pattern, REG_NOSUB);

    /* initialize fd array */
    struct rlimit lim;
    getrlimit(RLIMIT_NOFILE, &lim);
    assert(lim.rlim_max > 0);
    watched_fds = calloc(lim.rlim_max, 1);

    initialized = 1;
}

int
open(const char *pathname, int flags, ...)
{
    int mode;

    if (flags & O_CREAT) {
        va_list l;
        __builtin_va_start(l, flags);
        mode = __builtin_va_arg(l, mode_t);
        __builtin_va_end(l);
    }
    else {
        mode = 0;
    }

    if (_reentered)
        return _orig_open(pathname, flags, mode);

    _reentered++;
    int ret = _orig_open(pathname, flags, mode);
    _reentered--;

    /* is this file interesting? */
    if (ret >= 0 && !regexec(&reg, pathname, 0, NULL, 0)) {
        watched_fds[ret] = 1;
        #if DEBUG
        fprintf(stderr, "watching \"%s\" (fd=%d)\n", pathname, ret);
        #endif
    }

    return ret;
}

int
close(int fd)
{
    if (_reentered)
        return _orig_close(fd);

    _reentered++;
    int ret = _orig_close(fd);
    _reentered--;

    if (ret >= 0 && fd >= 0)
        watched_fds[fd] = 0;

    return ret;
}

ssize_t
write(int fd, const void *buf, size_t count)
{
    if (_reentered)
        return _orig_write(fd, buf, count);

    ssize_t ret;

    if (watched_fds[fd]) {
        #if DEBUG
        fprintf(stderr, "failed fd=%d\n", fd);
        #endif
        ret = -1;
        *__errno_location() = WRITE_ERROR;
    }
    else {
        _reentered++;
        ret = _orig_write(fd, buf, count);
        _reentered--;
    }
    return ret;
}
