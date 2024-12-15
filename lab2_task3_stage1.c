#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <N>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int N = atoi(argv[1]);
    if (N <= 0) {
        fprintf(stderr, "N must be a positive integer.\n");
        return EXIT_FAILURE;
    }

    //pid_t pids[N];
    for (int i = 0; i < N; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            // In case of error, we should wait for any already created children
            // but for simplicity, we'll just return failure here.
            return EXIT_FAILURE;
        } else if (pid == 0) {
            // Child process
            printf("Child index: %d, PID: %d\n", i, getpid());
            // Child finishes immediately
            exit(EXIT_SUCCESS);
        } else {
            // Parent process
            //pids[i] = pid;
        }
    }

    // Parent waits for all children
    while (wait(NULL) != -1 || errno != ECHILD);
    printf("All children finished\n");
    return EXIT_SUCCESS;
}
