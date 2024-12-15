#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

// Global variables for the child process
static volatile sig_atomic_t start_work = 0; // 0 means not started, 1 means start working

void handle_sigusr1(int sig) {
    (void) sig;
    // Set start_work to 1, indicating we should begin the loop
    start_work = 1;
}

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

    pid_t pids[N];
    // We will create N children. Each child will:
    // - Print their PID and index.
    // - Set up a signal handler for SIGUSR1.
    // - Then pause, waiting for SIGUSR1.

    // Seed random number generator (do it before fork so each child gets different states)
    srand((unsigned)time(NULL));

    for (int i = 0; i < N; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            // In case of error, ideally wait for any already created children
            return EXIT_FAILURE;
        } else if (pid == 0) {
            // Child process

            // Print PID and index as before
            printf("Child index: %d, PID: %d\n", i, getpid());
            fflush(stdout);

            // Set up SIGUSR1 handler
            struct sigaction sa;
            sa.sa_handler = handle_sigusr1;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = SA_RESTART;
            if (sigaction(SIGUSR1, &sa, NULL) < 0) {
                perror("sigaction");
                exit(EXIT_FAILURE);
            }

            // Wait until we receive SIGUSR1 to start work
            // Initially we do nothing but pause
            while (!start_work) {
                pause(); // Wait for signal
            }

            // Once SIGUSR1 is received, start the work loop
            int counter = 0;
            while (1) {
                // Sleep 100-200ms
                int sleep_time = 100000 + (rand() % 101) * 1000; // 100000us to 200000us
                usleep(sleep_time);

                counter++;
                printf("%d: %d\n", getpid(), counter);
                fflush(stdout);
            }

            // We never reach here in stage 2, but if we did, we'd exit normally
            exit(EXIT_SUCCESS);
        } else {
            // Parent process
            pids[i] = pid;
        }
    }
    sleep(1);
    // Parent waits until all children are ready (we know they printed their info)
    // In Stage 2, we are not told to wait before starting the first child.
    // So we can directly send SIGUSR1 to the first child to start working.
    kill(pids[0], SIGUSR1);

    // Now the first child is working indefinitely.
    // The other children are idle, waiting for a signal.
    // We won't terminate in stage 2, so just sleep here or wait for Ctrl+C.
    // If you want, you can wait indefinitely:
    while (1) {
        pause(); // Just keep the parent alive
    }

    return EXIT_SUCCESS;
}
