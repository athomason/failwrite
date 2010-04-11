/**
 * Executable wrapper to fake failure of write syscalls to specific handles
 * created with open() calls.
 *
 * NB: does not work with scripts invoked by #! lines; instead supply the
 * script interpreter as the executable name.
 **/

#include <assert.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/reg.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <unistd.h>

#if defined __i386__

    #define SYSCALL_NUM         orig_eax
    #define SYSCALL_ARG1        ebx
    #define SYSCALL_ARG2        ecx
    #define SYSCALL_ARG3        edx
    #define SYSCALL_RET_OFFSET  EAX

#elif defined __x86_64__

    #define SYSCALL_NUM         orig_rax
    #define SYSCALL_ARG1        rdi
    #define SYSCALL_ARG2        rsi
    #define SYSCALL_ARG3        rdx
    #define SYSCALL_RET_OFFSET  RAX

#else

    #error Unsupported platform; i386 and x86_64 only.

#endif

#define MAX_PATH_LEN 1024
#define WRITE_ERROR ENOSPC
#define DEBUG 0

const int word_size = sizeof(long);

static void peekstring(pid_t child, long addr, char* str);

int
main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <path_pattern> <program> [<args>]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char* pattern = argv[1];
    char* program = argv[2];
    char* watched_fds;

    struct rlimit lim;
    getrlimit(RLIMIT_NOFILE, &lim);
    assert(lim.rlim_max > 0);
    watched_fds = calloc(lim.rlim_max, 1);

    /* match filenames with POSIX regular expressions */
    regex_t reg;
    regcomp(&reg, pattern, REG_NOSUB);

    pid_t pid = fork();
    if (pid == -1) {
        /* failed */
        perror("couldn't fork");
        return EXIT_FAILURE;
    }
    else if (pid == 0) {
        /* child: enable tracing and run program */
        if (ptrace(PTRACE_TRACEME, 0, (char*) 1, 0) < 0) {
            perror("ptrace(PTRACE_TRACEME, ...)");
            return EXIT_FAILURE;
        }
        kill(getpid(), SIGSTOP); /* halt here until parent restarts us */
        execv(program, &argv[2]);
        perror(program);
        _exit(EXIT_FAILURE);
    }

    /* parent */

    long syscallno = 0;
    long retval = 0;
    struct user_regs_struct regs;
    int pid_status;
    int entry = 0; /* go through main loop twice per syscall, once on entry and again on exit; this is true on the first pass */
    char path_buf[MAX_PATH_LEN];

    for (entry = 1; ; entry = !entry) {
        waitpid(pid, &pid_status, 0);
        if (WIFEXITED(pid_status)) /* child exited, cleanup */
            break;

        if (entry) {
            /* copy args on syscall entry */
            ptrace(PTRACE_GETREGS, pid, NULL, &regs);
            syscallno = regs.SYSCALL_NUM;
        }
        else {
            /* do processing on syscall return */
            retval = ptrace(PTRACE_PEEKUSER, pid, word_size * SYSCALL_RET_OFFSET, NULL);

            switch (syscallno) {

              case SYS_open: {
                /* copy the opened path from child process */
                peekstring(pid, regs.SYSCALL_ARG1, path_buf);

                #if DEBUG
                fprintf(stderr,
                    "open(\"%s\", %ld, %ld) = %ld\n",
                    path_buf, regs.SYSCALL_ARG2, regs.SYSCALL_ARG3, retval
                );
                #endif

                /* retval is fd */
                if (retval >= 0 && !regexec(&reg, path_buf, 0, NULL, 0))
                    watched_fds[retval] = 1;
                break;
              }

              case SYS_close: {
                /* forget interesting handles when they're closed */
                int fd = regs.SYSCALL_ARG1;
                if (fd >= 0)
                    watched_fds[fd] = 0;

                #if DEBUG
                fprintf(stderr, "close(%d) = %ld\n", fd, retval);
                #endif
                break;
              }

              case SYS_write: {
                #if DEBUG
                fprintf(stderr,
                    "write(%ld, 0x%lx, %ld) = %ld",
                    regs.SYSCALL_ARG1, regs.SYSCALL_ARG2, regs.SYSCALL_ARG3, retval
                );
                #endif

                /* first arg to write is fd */
                if (watched_fds[regs.SYSCALL_ARG1]) {
                    retval = -WRITE_ERROR;
                    if (ptrace(PTRACE_POKEUSER, pid, word_size * SYSCALL_RET_OFFSET, retval) < 0) {
                        perror("retvalset: ptrace(PTRACE_POKEUSER, ...)");
                    }
                    #if DEBUG
                    fprintf(stderr, " --> %ld", retval);
                    #endif
                }
                #if DEBUG
                fprintf(stderr, "\n");
                #endif
                break;
              }

            }
        }

        if (ptrace(PTRACE_SYSCALL, pid, (char*) 1, 0) < 0) {
            perror("resume: ptrace(PTRACE_SYSCALL, ...)");
            return EXIT_FAILURE;
        }
    }
    ptrace(PTRACE_DETACH, pid, (char*) 1, 0);
    regfree(&reg);

    return EXIT_SUCCESS;
}

/* copy a null-terminated string from child into a buffer. no bounds checking */
static
void
peekstring(pid_t child, long addr, char* str)
{
    union {
        long val;
        char chars[word_size];
    } data;
    int words;
    int bytes;
    char* bufpos = str;

    for (words = 0; ; words++) {
        data.val = ptrace(PTRACE_PEEKDATA, child, addr + words * word_size, NULL);
        memcpy(bufpos, data.chars, word_size);
        for (bytes = 0; bytes < word_size; bytes++) {
            if (data.chars[bytes] == 0) {
                str[words * word_size + bytes] = 0;
                return;
            }
        }
        bufpos += word_size;
    }
    str[words * word_size] = 0;
}
