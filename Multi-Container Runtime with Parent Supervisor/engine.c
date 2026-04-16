#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>

#define MAX_CONTAINERS 10

// ---------------- METADATA ----------------
typedef struct {
    char id[32];
    pid_t host_pid;
    char status[16];
    time_t start_time;
    int is_active;
} ContainerMetadata;

ContainerMetadata table[MAX_CONTAINERS];

// ---------------- PRINT TABLE ----------------
void print_metadata() {
    printf("\nID\tPID\tSTATUS\t\tSTART TIME\n");
    printf("-------------------------------------------\n");
    for (int i = 0; i < MAX_CONTAINERS; i++) {
        if (table[i].is_active) {
            printf("%s\t%d\t%s\t%ld\n", 
                   table[i].id, table[i].host_pid, table[i].status, table[i].start_time);
        }
    }
}

// ---------------- START CONTAINER ----------------
void start_container(char *id, char *rootfs, char *command) {

    pid_t pid = fork();

    if (pid < 0) {
        perror("Fork failed");
        return;
    }

    if (pid == 0) { // CHILD PROCESS

        // 1. Create namespaces
        if (unshare(CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS) != 0) {
            perror("unshare failed");
            exit(1);
        }

        // 2. Fork again for PID namespace
        pid_t pid2 = fork();

        if (pid2 < 0) {
            perror("fork failed");
            exit(1);
        }

        if (pid2 > 0) {
            wait(NULL);
            exit(0);
        }

        // Now inside container (PID 1)
        printf("[Container] PID inside namespace: %d\n", getpid());

        // 3. Set hostname
        sethostname("container", 9);

        // 4. Change root filesystem
        if (chroot(rootfs) != 0) {
            perror("chroot failed");
            exit(1);
        }

        chdir("/");

        // 5. Mount /proc
        mkdir("/proc", 0555);
        if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
            perror("mount /proc failed");
            exit(1);
        }

        // 6. Execute command
        printf("[Container] Running command: %s\n", command);
        char *args[] = {"/bin/sh", "-c", command, NULL};
        execvp(args[0], args);

        exit(1);
    } 
    else { // SUPERVISOR

        // Save metadata
        for (int i = 0; i < MAX_CONTAINERS; i++) {
            if (!table[i].is_active) {
                strncpy(table[i].id, id, 31);
                table[i].host_pid = pid;
                strcpy(table[i].status, "RUNNING");
                table[i].start_time = time(NULL);
                table[i].is_active = 1;
                break;
            }
        }

        printf("[Supervisor] Logged container %s with PID %d\n", id, pid);
    }
}

// ---------------- MAIN ----------------
int main() {

    printf("--- Multi-Container Supervisor Engine ---\n");

    // Start two containers with separate rootfs
    start_container("alpha", "./rootfs-alpha", "sleep 5");
    start_container("beta", "./rootfs-beta", "sleep 10");

    // Reaper loop
    while (1) {
        int status;
        pid_t dead_child = waitpid(-1, &status, WNOHANG);

        if (dead_child > 0) {
            for (int i = 0; i < MAX_CONTAINERS; i++) {
                if (table[i].host_pid == dead_child) {
                    printf("\n[Supervisor] Container %s (PID %d) exited.\n", 
                           table[i].id, dead_child);
                    table[i].is_active = 0;
                }
            }
        }

        print_metadata();
        sleep(2);

        // Exit when all done
        int active_count = 0;
        for (int i = 0; i < MAX_CONTAINERS; i++) {
            if (table[i].is_active) active_count++;
        }

        if (active_count == 0) break;
    }

    printf("All containers finished. Supervisor shutting down.\n");
    return 0;
}

