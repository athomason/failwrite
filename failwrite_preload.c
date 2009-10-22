#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char preload_lib[] = "failwrite_preload_lib.so";

int
main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <path_pattern> <program> [<args>]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char* pattern = argv[1];
    char* program = argv[2];

    setenv("FAILWRITE_PATTERN", pattern, 1);

    char* old_preload = getenv("LD_PRELOAD");
    if (old_preload == NULL) {
        setenv("LD_PRELOAD", preload_lib, 1);
    }
    else {
        char* preload = calloc(strlen(preload_lib) + 1 + strlen(old_preload) + 1, 1);
        strcpy(preload, preload_lib);
        strcat(preload, " ");
        strcat(preload, old_preload);
        setenv("LD_PRELOAD", preload, 1);
        free(preload);
    }

    execv(program, &argv[2]);
    perror("execv failed");
    return EXIT_FAILURE;
}
