#include "kernel/types.h"
#include "kernel/param.h"
#include "user.h"

// handlers 
// receive nums from parent & send nums to child
// note that this function must be called in a child process
void handler(int *fds_left) {
    // close the write end in the child process
    close(fds_left[1]);
    int first_num;
    if (read(fds_left[0], (void *)(&first_num), sizeof(int)) == 0) {
        // no more nums
        close(fds_left[0]);
        exit(0);
    }
    printf("prime %d\n", first_num);
    int fds_right[2];
    pipe(fds_right);
    if (fork() == 0) {
        handler(fds_right);
    }

    close(fds_right[0]);
    int num;
    while (read(fds_left[0], (void *)(&num), sizeof(int)) > 0) {
        if (num % first_num > 0) {
            write(fds_right[1], (void *)(&num), sizeof(int));
        }
    }
    close(fds_left[0]);
    close(fds_right[1]);
    // wait the child
    wait(0);
    exit(0);
}

int main(int argc, char *argv[]) {
    int fds[2];
    pipe(fds);
    if (fork() == 0) {
        handler(fds);
    }
    close(fds[0]);
    int i;
    for (i = 2; i <= 35; ++i) {
        write(fds[1], (void *)(&i), sizeof(int));
    }
    close(fds[1]);
    wait(0);
    exit(0);
}