#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

// Global variables for the child process
static volatile sig_atomic_t start_work = 0; // 0 means not started or paused, 1 means start/continue working

// For the parent
static volatile sig_atomic_t current_child = 0; // which child is currently working
static int N = 0;
static pid_t *pids = NULL; // We will assign this to the VLA after we know N

// Child handlers
void handle_sigusr1_child(int sig) {
    (void) sig;
    start_work = 1; // start or resume working
}

void handle_sigusr2_child(int sig) {
    (void) sig;
    start_work = 0; // stop working
}

// Parent handler for SIGUSR1: switch working child
void handle_sigusr1_parent(int sig) {
    (void)sig;
    // Send SIGUSR2 to the currently working child to pause it
    kill(pids[current_child], SIGUSR2);

    // Move to next child in a circular fashion
    current_child = (current_child + 1) % N;

    // Send SIGUSR1 to the next child to start working
    kill(pids[current_child], SIGUSR1);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <N>\n", argv[0]);
        return EXIT_FAILURE;
    }

    N = atoi(argv[1]);
    if (N <= 0) {
        fprintf(stderr, "N must be a positive integer.\n");
        return EXIT_FAILURE;
    }

    // Using a variable-length array for pids
    pid_t pids_array[N];
    pids = pids_array;

    // Seed random number generator
    srand((unsigned)time(NULL));

    // Set up parent's SIGUSR1 handler to switch children
    struct sigaction sa_parent;
    sa_parent.sa_handler = handle_sigusr1_parent;
    sigemptyset(&sa_parent.sa_mask);
    sa_parent.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR1, &sa_parent, NULL) < 0) {
        perror("sigaction parent");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < N; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return EXIT_FAILURE;
        } else if (pid == 0) {
            // Child process
            printf("Child index: %d, PID: %d\n", i, getpid());
            fflush(stdout);

            // Set up SIGUSR1 handler for starting/resuming work
            struct sigaction sa1;
            sa1.sa_handler = handle_sigusr1_child;
            sigemptyset(&sa1.sa_mask);
            sa1.sa_flags = SA_RESTART;
            if (sigaction(SIGUSR1, &sa1, NULL) < 0) {
                perror("sigaction child SIGUSR1");
                exit(EXIT_FAILURE);
            }

            // Set up SIGUSR2 handler for stopping work
            struct sigaction sa2;
            sa2.sa_handler = handle_sigusr2_child;
            sigemptyset(&sa2.sa_mask);
            sa2.sa_flags = SA_RESTART;
            if (sigaction(SIGUSR2, &sa2, NULL) < 0) {
                perror("sigaction child SIGUSR2");
                exit(EXIT_FAILURE);
            }

            // Initially, child waits for SIGUSR1 to start working
            while (!start_work) {
                pause();
            }

            int counter = 0;
            while (1) {
                // Check if we should pause work (SIGUSR2 might have arrived)
                if (!start_work) {
                    while (!start_work) {
                        pause();
                    }
                }

                int sleep_time = 100000 + (rand() % 101) * 1000; // 100ms to 200ms
                usleep(sleep_time);

                counter++;
                printf("%d: %d Parent(%d)\n", getpid(), counter, getppid());
                fflush(stdout);
            }

            exit(EXIT_SUCCESS);
        } else {
            // Parent process
            pids[i] = pid;
        }
    }

    // Sleep for 1 second before starting the first child
    sleep(1);
    printf("%d",getpid());
    sleep(1);
    // Start the first child
    current_child = 0;
    kill(pids[0], SIGUSR1);

    // Parent just waits. When the parent receives SIGUSR1 (e.g., from user),
    // it will switch the working child in handle_sigusr1_parent().
    while (1) {
        pause();
    }

    return EXIT_SUCCESS;
}
