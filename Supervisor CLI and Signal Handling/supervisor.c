#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#define MAX_CONTAINERS 100

typedef struct {
    int id;
    pid_t pid;
    char cmd[256];
    char state[20];
    char log_file[256];
} Container;

Container containers[MAX_CONTAINERS];
int container_count = 0;

// ==========================
// SIGNAL HANDLERS
// ==========================

// Handle child termination
void handle_sigchld(int sig) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < container_count; i++) {
            if (containers[i].pid == pid) {
                strcpy(containers[i].state, "STOPPED");
            }
        }
    }
}

// Ctrl + C
void handle_sigint(int sig) {
    printf("\nUse 'exit' to quit safely.\n");
}

// Kill all containers
void handle_sigterm(int sig) {
    printf("\nStopping all containers...\n");

    for (int i = 0; i < container_count; i++) {
        if (strcmp(containers[i].state, "RUNNING") == 0) {
            kill(containers[i].pid, SIGTERM);
        }
    }

    exit(0);
}

// ==========================
// COMMAND FUNCTIONS
// ==========================

// START (background)
void start_container(char *cmd) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("Fork failed");
        return;
    }

    if (pid == 0) {
        // CHILD PROCESS

        char log_file[50];
        sprintf(log_file, "log_%d.txt", container_count + 1);

        freopen(log_file, "w", stdout);
        freopen(log_file, "w", stderr);

        execl("/bin/sh", "sh", "-c", cmd, NULL);

        perror("Exec failed");
        exit(1);
    } else {
        // PARENT PROCESS

        Container c;
        c.id = container_count + 1;
        c.pid = pid;
        strcpy(c.cmd, cmd);
        strcpy(c.state, "RUNNING");

        sprintf(c.log_file, "log_%d.txt", c.id);

        containers[container_count++] = c;

        printf("Started container %d (PID=%d)\n", c.id, pid);
    }
}

// RUN (foreground)
void run_container(char *cmd) {
    pid_t pid = fork();

    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        perror("Exec failed");
        exit(1);
    } else {
        waitpid(pid, NULL, 0);
    }
}

// PS
void list_containers() {
    printf("\nID\tPID\tSTATE\t\tCMD\n");

    for (int i = 0; i < container_count; i++) {
        printf("%d\t%d\t%s\t%s\n",
               containers[i].id,
               containers[i].pid,
               containers[i].state,
               containers[i].cmd);
    }
}

// LOGS
void show_logs(int id) {
    for (int i = 0; i < container_count; i++) {
        if (containers[i].id == id) {
            FILE *f = fopen(containers[i].log_file, "r");

            if (!f) {
                printf("No logs available\n");
                return;
            }

            char ch;
            while ((ch = fgetc(f)) != EOF) {
                putchar(ch);
            }

            fclose(f);
            return;
        }
    }

    printf("Invalid container ID\n");
}

// STOP
void stop_container(int id) {
    for (int i = 0; i < container_count; i++) {
        if (containers[i].id == id) {

            if (strcmp(containers[i].state, "STOPPED") == 0) {
                printf("Already stopped\n");
                return;
            }

            kill(containers[i].pid, SIGTERM);
            strcpy(containers[i].state, "STOPPED");

            printf("Container %d stopped\n", id);
            return;
        }
    }

    printf("Invalid container ID\n");
}

// ==========================
// CLI LOOP
// ==========================

int main() {
    signal(SIGCHLD, handle_sigchld);
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigterm);

    char input[256];

    while (1) {
        printf("mini-docker> ");

        if (!fgets(input, sizeof(input), stdin))
            break;

        input[strcspn(input, "\n")] = 0; // remove newline

        if (strncmp(input, "start ", 6) == 0) {
            start_container(input + 6);
        }
        else if (strncmp(input, "run ", 4) == 0) {
            run_container(input + 4);
        }
        else if (strcmp(input, "ps") == 0) {
            list_containers();
        }
        else if (strncmp(input, "logs ", 5) == 0) {
            int id = atoi(input + 5);
            show_logs(id);
        }
        else if (strncmp(input, "stop ", 5) == 0) {
            int id = atoi(input + 5);
            stop_container(id);
        }
        else if (strcmp(input, "exit") == 0) {
            handle_sigterm(0);
        }
        else {
            printf("Unknown command\n");
        }
    }

    return 0;
}