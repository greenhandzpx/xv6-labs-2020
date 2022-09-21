#include "kernel/types.h"
#include "kernel/param.h"
#include "user.h"

int main(int argc, char* argv[]) {
    char *exec_argv[MAXARG];
    // copy the command and the initial args to the new buffer
    for (int i = 1; i < argc; ++i) {
        exec_argv[i-1] = malloc(strlen(argv[i]));
        strcpy(exec_argv[i-1], argv[i]);
    }
    // exec_argv[argc] = malloc(1);
    // strcpy(exec_argv[argc], "\0");
    int first = 1;
    for(;;) {
        if (first) {
            first = 0;
        } else {
            // in order not to lead to memory leak
            free(exec_argv[argc-1]);
        }
        exec_argv[argc-1] = malloc(32);
        gets(exec_argv[argc-1], 32);
        for (int i = 0; exec_argv[argc-1][i]; ++i) {
            if (exec_argv[argc-1][i] == '\n') {
                exec_argv[argc-1][i] = '\0';
                break;
            }
        }
        if (strcmp(exec_argv[argc-1], "\0") == 0) {
            break;
        }
        // printf("gets: %s\n", exec_argv[argc-1]);
        if (fork() == 0) {
            // printf("%s\n", exec_argv[1]);
            // for(argc = 0; exec_argv[argc]; argc++) {
            //     printf("%d %s\n", argc, exec_argv[argc]);
            // }
            int ret;
            if ((ret = exec(exec_argv[0], exec_argv)) < 0) {
                printf("exec error\n");
            }
            exit(0);
        }
        wait(0);
    }
    exit(0);
}