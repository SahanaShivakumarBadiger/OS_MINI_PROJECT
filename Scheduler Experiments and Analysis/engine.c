#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

void run_process(const char *program, char *const args[]) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        exit(1);
    }
    else if (pid == 0) {
        // Child process
        execvp(program, args);
        perror("exec failed"); // only runs if exec fails
        exit(1);
    }
    else {
        printf("Started %s with PID %d\n", program, pid);
    }
}

int main() {
    printf("=== ENGINE START ===\n");

    // Arguments for each program
    char *mem_args[] = {"./memory_test", NULL};
    char *io_args[]  = {"./io_pulse", "15", "300", NULL};
    char *cpu_args[] = {"./cpu_hog", "10", NULL};

    // Start all processes
    run_process("./memory_test", mem_args);
    run_process("./io_pulse", io_args);
    run_process("./cpu_hog", cpu_args);

    printf("All processes started...\n");

    // Wait for all child processes
    int status;
    while (wait(&status) > 0);

    printf("=== ENGINE END ===\n");

    return 0;
}