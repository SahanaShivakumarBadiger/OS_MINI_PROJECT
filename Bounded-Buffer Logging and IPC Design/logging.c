#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <signal.h>

#define SOCKET_PATH "/tmp/engine_socket"
#define MAX_CONTAINERS 10
#define MAX_BUFFER 20
#define MAX_LOG 256

/* ---------------- CONTAINERS ---------------- */

typedef struct {
    int id;
    pid_t pid;
    int active;
} container_t;

container_t containers[MAX_CONTAINERS];

/* ---------------- LOG BUFFER ---------------- */

typedef struct {
    int cid;
    char msg[MAX_LOG];
} log_entry;

typedef struct {
    log_entry buf[MAX_BUFFER];
    int in, out, count;
    int shutdown;

    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
} bounded_buffer;

bounded_buffer logbuf;

/* ---------------- INIT ---------------- */

void init_buffer() {
    logbuf.in = logbuf.out = logbuf.count = 0;
    logbuf.shutdown = 0;
    pthread_mutex_init(&logbuf.mutex, NULL);
    pthread_cond_init(&logbuf.not_full, NULL);
    pthread_cond_init(&logbuf.not_empty, NULL);
}

/* ---------------- INSERT ---------------- */

void insert_log(log_entry e) {
    pthread_mutex_lock(&logbuf.mutex);

    while (logbuf.count == MAX_BUFFER && !logbuf.shutdown)
        pthread_cond_wait(&logbuf.not_full, &logbuf.mutex);

    if (logbuf.shutdown) {
        pthread_mutex_unlock(&logbuf.mutex);
        return;
    }

    logbuf.buf[logbuf.in] = e;
    logbuf.in = (logbuf.in + 1) % MAX_BUFFER;
    logbuf.count++;

    pthread_cond_signal(&logbuf.not_empty);
    pthread_mutex_unlock(&logbuf.mutex);
}

/* ---------------- REMOVE ---------------- */

int remove_log(log_entry *e) {
    pthread_mutex_lock(&logbuf.mutex);

    while (logbuf.count == 0 && !logbuf.shutdown)
        pthread_cond_wait(&logbuf.not_empty, &logbuf.mutex);

    if (logbuf.count == 0 && logbuf.shutdown) {
        pthread_mutex_unlock(&logbuf.mutex);
        return 0;
    }

    *e = logbuf.buf[logbuf.out];
    logbuf.out = (logbuf.out + 1) % MAX_BUFFER;
    logbuf.count--;

    pthread_cond_signal(&logbuf.not_full);
    pthread_mutex_unlock(&logbuf.mutex);
    return 1;
}

/* ---------------- PRODUCER ---------------- */

typedef struct {
    int fd;
    int cid;
} prod_arg;

void *producer(void *arg) {
    prod_arg *p = (prod_arg *)arg;
    char buf[128];
    int n;

    while ((n = read(p->fd, buf, sizeof(buf)-1)) > 0) {
        buf[n] = '\0';

        log_entry e;
        e.cid = p->cid;
        snprintf(e.msg, MAX_LOG, "%s", buf);

        insert_log(e);
    }

    close(p->fd);
    free(p);
    return NULL;
}

/* ---------------- CONSUMER ---------------- */

void *consumer(void *arg) {
    FILE *files[MAX_CONTAINERS] = {0};
    log_entry e;

    while (remove_log(&e)) {
        if (!files[e.cid]) {
            char name[64];
            sprintf(name, "container_%d.log", e.cid);
            files[e.cid] = fopen(name, "w");
        }

        fprintf(files[e.cid], "%s", e.msg);
        fflush(files[e.cid]);
    }

    // flush and close all files
    for (int i = 0; i < MAX_CONTAINERS; i++) {
        if (files[i]) fclose(files[i]);
    }

    return NULL;
}

/* ---------------- START CONTAINER ---------------- */

void start_container(int cid, char *rootfs, char *cmd) {
    int pipefd[2];
    pipe(pipefd);

    pid_t pid = fork();

    if (pid == 0) {
        close(pipefd[0]);

        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        chroot(rootfs);
        chdir("/");

        execlp("sh", "sh", "-c", cmd, NULL);
        perror("exec failed");
        exit(1);
    } else {
        close(pipefd[1]);

        containers[cid].id = cid;
        containers[cid].pid = pid;
        containers[cid].active = 1;

        pthread_t t;
        prod_arg *arg = malloc(sizeof(prod_arg));
        arg->fd = pipefd[0];
        arg->cid = cid;

        pthread_create(&t, NULL, producer, arg);
        pthread_detach(t);
    }
}

/* ---------------- STOP ---------------- */

void stop_container(int cid) {
    if (containers[cid].active) {
        kill(containers[cid].pid, SIGTERM);
        containers[cid].active = 0;
    }
}

/* ---------------- PS ---------------- */

void list_containers(int client) {
    char buf[512] = "";
    for (int i = 0; i < MAX_CONTAINERS; i++) {
        if (containers[i].active) {
            char line[64];
            sprintf(line, "ID=%d PID=%d\n", i, containers[i].pid);
            strcat(buf, line);
        }
    }
    write(client, buf, strlen(buf));
}

/* ---------------- SERVER ---------------- */

void run_supervisor() {
    int server_fd, client_fd;
    struct sockaddr_un addr;

    unlink(SOCKET_PATH);

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);

    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 5);

    printf("Supervisor running...\n");

    pthread_t cons;
    pthread_create(&cons, NULL, consumer, NULL);

    while (1) {
        client_fd = accept(server_fd, NULL, NULL);

        char cmd[256];
        read(client_fd, cmd, sizeof(cmd));

        if (strncmp(cmd, "start", 5) == 0) {

            char *token = strtok(cmd, " ");
            token = strtok(NULL, " ");
            int id = atoi(token);

            token = strtok(NULL, " ");
            char rootfs[128];
            strcpy(rootfs, token);

            char command[128] = "";
            token = strtok(NULL, "");
            if (token) strcpy(command, token);

            start_container(id, rootfs, command);
            write(client_fd, "Started\n", 8);
        }

        else if (strncmp(cmd, "ps", 2) == 0) {
            list_containers(client_fd);
        }

        else if (strncmp(cmd, "stop", 4) == 0) {
            int id;
            sscanf(cmd, "stop %d", &id);
            stop_container(id);
            write(client_fd, "Stopped\n", 8);
        }

        close(client_fd);
    }
}

/* ---------------- CLIENT ---------------- */

void send_cmd(int argc, char *argv[]) {
    int fd;
    struct sockaddr_un addr;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);

    connect(fd, (struct sockaddr *)&addr, sizeof(addr));

    char cmd[256] = "";
    for (int i = 1; i < argc; i++) {
        strcat(cmd, argv[i]);
        strcat(cmd, " ");
    }

    write(fd, cmd, strlen(cmd));

    char buf[512];
    int n = read(fd, buf, sizeof(buf)-1);
    if (n > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }

    close(fd);
}

/* ---------------- CLEAN EXIT ---------------- */

void cleanup() {
    pthread_mutex_lock(&logbuf.mutex);
    logbuf.shutdown = 1;
    pthread_cond_broadcast(&logbuf.not_empty);
    pthread_cond_broadcast(&logbuf.not_full);
    pthread_mutex_unlock(&logbuf.mutex);
}

/* ---------------- MAIN ---------------- */

int main(int argc, char *argv[]) {

    if (argc < 2) {
        printf("Usage:\n");
        printf("engine supervisor\n");
        printf("engine start <id> <rootfs> <cmd>\n");
        printf("engine ps\n");
        printf("engine stop <id>\n");
        return 0;
    }

    if (strcmp(argv[1], "supervisor") == 0) {
        init_buffer();

        signal(SIGINT, cleanup);   // Ctrl+C
        signal(SIGTERM, cleanup);

        run_supervisor();
    } else {
        send_cmd(argc, argv);
    }

    return 0;
}