#include "kernel/types.h"
#include "user.h"

int main(int argc,char* argv[]){
    int pipe_fds1[2];
    int pipe_fds2[2];
    char buf[1024];
    if (pipe(pipe_fds1) == -1) {
        printf("pipe error\n");
        exit(-1);
    }
    if (pipe(pipe_fds2) == -1) {
        printf("pipe error\n");
        exit(-1);
    }
    int pid = fork();
    if (pid == 0) {
        // child process
        // close the write end
        close(pipe_fds1[1]);
        read(pipe_fds1[0], &buf, 10);
        int id = getpid();
        printf("%d: received %s\n", id, buf);
        close(pipe_fds1[0]);
        close(pipe_fds2[0]);
        if ((write(pipe_fds2[1], "pong", 5)) == -1) {
            printf("child write error\n");
            exit(-1);
        }
        close(pipe_fds2[1]);
        exit(0);
    }
    // parent process
    close(pipe_fds1[0]);
    if ((write(pipe_fds1[1], "ping", 5)) == -1) {
        printf("parent write error\n");
        exit(-1);
    }
    // close the write end
    close(pipe_fds1[1]);
    close(pipe_fds2[1]);
    read(pipe_fds2[0], &buf, 10);
    int id = getpid();
    printf("%d: received %s\n", id, buf);
    close(pipe_fds2[0]);
    exit(0);
}