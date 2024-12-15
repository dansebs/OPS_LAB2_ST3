#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>

// Global variables for the child process
static volatile sig_atomic_t start_work = 0; // 0 means paused/not started, 1 means working
static volatile sig_atomic_t got_sigint = 0; // Child: set when SIGINT received
static int counter = 0; // Child's work counter

// For the parent
static volatile sig_atomic_t current_child = 0; // which child is currently working
static int N = 0;
static pid_t *pids = NULL;

// We will have a flag to indicate parent got SIGINT
static volatile sig_atomic_t parent_shutdown = 0;

// Child handlers
void handle_sigusr1_child(int sig) {
    (void) sig;
    if (!got_sigint) { // Only resume if we haven't received SIGINT
        start_work = 1;
    }
}

void handle_sigusr2_child(int sig) {
    (void) sig;
    // Only pause if we haven't received SIGINT
    if (!got_sigint) {
        start_work = 0;
    }
}

// Child SIGINT handler: write counter to file and exit
void handle_sigint_child(int sig) {
    (void)sig;
    got_sigint = 1;   // Indicate we've got SIGINT
    start_work = 0;   // Stop working immediately

    // Create filename "PID.txt"
    char filename[32];
    snprintf(filename, sizeof(filename), "%d.txt", getpid());

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        // Convert counter to string
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%d\n", counter);
        write(fd, buf, len);
        close(fd);
    }
    // Exit after writing
    _exit(EXIT_SUCCESS);
}

// Parent handlers

// Parent handler for SIGUSR1: switch working child
void handle_sigusr1_parent(int sig) {
    (void)sig;
    fprintf(stderr, "Parent received SIGUSR1, switching from child %d\n", current_child);
    kill(pids[current_child], SIGUSR2);
    current_child = (current_child + 1) % N;
    fprintf(stderr, "Now switching to child %d (pid %d)\n", current_child, pids[current_child]);
    kill(pids[current_child], SIGUSR1);
}

// Parent handler for SIGINT: send SIGINT to all children and set shutdown flag
void handle_sigint_parent(int sig) {
    (void)sig;
    parent_shutdown = 1;

    // Send SIGINT to all children
    for (int i = 0; i < N; i++) {
        kill(pids[i], SIGINT);
    }
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

    pid_t pids_array[N];
    pids = pids_array;

    // Seed random number generator
    srand((unsigned)time(NULL));

    // Set up parent's SIGUSR1 handler to switch children
    struct sigaction sa_parent_usr1;
    sa_parent_usr1.sa_handler = handle_sigusr1_parent;
    sigemptyset(&sa_parent_usr1.sa_mask);
    sa_parent_usr1.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR1, &sa_parent_usr1, NULL) < 0) {
        perror("sigaction parent SIGUSR1");
        return EXIT_FAILURE;
    }

    // Set up parent's SIGINT handler to shut down
    struct sigaction sa_parent_int;
    sa_parent_int.sa_handler = handle_sigint_parent;
    sigemptyset(&sa_parent_int.sa_mask);
    sa_parent_int.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa_parent_int, NULL) < 0) {
        perror("sigaction parent SIGINT");
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

            // Set up SIGINT handler for saving counter and exiting
            struct sigaction sa_int_child;
            sa_int_child.sa_handler = handle_sigint_child;
            sigemptyset(&sa_int_child.sa_mask);
            sa_int_child.sa_flags = SA_RESTART;
            if (sigaction(SIGINT, &sa_int_child, NULL) < 0) {
                perror("sigaction child SIGINT");
                exit(EXIT_FAILURE);
            }

            // Initially, child waits for SIGUSR1 to start working
            while (!start_work && !got_sigint) {
                pause();
            }

            while (1) {
                if (got_sigint) {
                    // If SIGINT received after starting work loop for some reason, handle will exit process.
                    // But we check anyway.
                    break;
                }

                if (!start_work) {
                    // Paused, wait until we can start again or got SIGINT
                    while (!start_work && !got_sigint) {
                        pause();
                    }
                    if (got_sigint) {
                        break;
                    }
                }

                int sleep_time = 100000 + (rand() % 101) * 1000; // 100ms to 200ms
                usleep(sleep_time);

                counter++;
                printf("%d: %d | Parent(%d)\n", getpid(), counter, getppid());
                fflush(stdout);
            }

            // If we ever break out of loop normally (which shouldn't happen unless SIGINT):
            // handle_sigint_child would have exited. This is just a safeguard.
            exit(EXIT_SUCCESS);
        } else {
            // Parent process
            pids[i] = pid;
        }
    }

    // Sleep for 1 second before starting the first child
    sleep(1);

    // Start the first child
    current_child = 0;
    kill(pids[0], SIGUSR1);

    // Parent main loop
    // - If no SIGINT: Just wait (pause)
    // - If SIGINT: parent_shutdown = 1, send SIGINT to all children, then break loop and wait them out.
    while (!parent_shutdown) {
        pause();
    }

    // Now parent_shutdown == 1 means we got SIGINT.
    // Wait for all children to finish
    int status;
    while (wait(&status) > 0 || errno == EINTR);

    printf("All children finished. Parent exiting.\n");
    return EXIT_SUCCESS;
}
