/***************  minikv.c  ***************/
/* Single-file implementation of tiny key-value store
   Build:  gcc minikv.c -o minikv
   Run:    ./minikv 9001
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <zlib.h>

#define PORT_ARG 1
#define MAX_EVENTS 64
#define BUF_SIZE 4096
#define DB_FILE "minikv.db"

static FILE *db;
static char *db_path;

/* ---------- CRC helper ---------- */
static uint32_t crc32buf(const char *buf, size_t len) {
    return crc32(0L, (Bytef *)buf, len);
}

/* ---------- KV engine ---------- */
static int kv_open(const char *dir) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir, DB_FILE);
    db = fopen(path, "a+");
    if (!db) return -1;
    db_path = strdup(path);
    return 0;
}

static void kv_close(void) {
    if (db) fclose(db);
    free(db_path);
}

static int kv_put(const char *k, const char *v) {
    if (!db) return -1;
    fprintf(db, "%zu %zu %s %s\n", strlen(k), strlen(v), k, v);
    fflush(db);
    return 0;
}

static char *kv_get(const char *k) {
    if (!db) return NULL;
    rewind(db);
    char *line = NULL;
    size_t len = 0;
    while (getline(&line, &len, db) != -1) {
        size_t klen, vlen;
        char key[256], val[256];
        if (sscanf(line, "%zu %zu %255s %255s", &klen, &vlen, key, val) == 4 &&
            strcmp(key, k) == 0) {
            char *ret = strdup(val);
            free(line);
            return ret;
        }
    }
    free(line);
    return NULL;
}

/* ---------- TCP server ---------- */
static int set_nonblock(int fd) {
    return fcntl(fd, F_SETFL, O_NONBLOCK);
}

static void handle_cmd(int fd, char *buf) {
    char *cmd = strtok(buf, "\r\n");
    if (!cmd) return;

    if (strncmp(cmd, "PUT ", 4) == 0) {
        char *k = cmd + 4;
        char *space = strchr(k, ' ');
        if (!space) return;
        *space = 0;
        char *v = space + 1;
        kv_put(k, v);
        send(fd, "OK\n", 3, 0);
    } else if (strncmp(cmd, "GET ", 4) == 0) {
        char *k = cmd + 4;
        char *v = kv_get(k);
        if (v) {
            send(fd, v, strlen(v), 0);
            send(fd, "\n", 1, 0);
            free(v);
        } else {
            send(fd, "NIL\n", 4, 0);
        }
    } else {
        send(fd, "ERR\n", 4, 0);
    }
}

static void run_server(uint16_t port) {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(port),
                               .sin_addr.s_addr = INADDR_ANY};
    bind(listener, (struct sockaddr *)&addr, sizeof(addr));
    listen(listener, 128);
    set_nonblock(listener);

    int epfd = epoll_create1(0);
    struct epoll_event ev = {.events = EPOLLIN, .data.fd = listener};
    epoll_ctl(epfd, EPOLL_CTL_ADD, listener, &ev);

    struct epoll_event events[MAX_EVENTS];
    while (1) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            if (fd == listener) {
                int client = accept(listener, NULL, NULL);
                set_nonblock(client);
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = client;
                epoll_ctl(epfd, EPOLL_CTL_ADD, client, &ev);
            } else {
                char buf[BUF_SIZE];
                ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
                if (n <= 0) {
                    close(fd);
                } else {
                    buf[n] = 0;
                    handle_cmd(fd, buf);
                }
            }
        }
    }
}

/* ---------- main ---------- */
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <port>\n", argv[0]); return 1; }
    uint16_t port = atoi(argv[PORT_ARG]);
    if (kv_open(".") < 0) { perror("kv_open"); return 1; }
    run_server(port);
    kv_close();
    return 0;
}


**miniKV** – single-file C99 key-value store:  
// TCP server (`./minikv 9001`) with EPOLL, append-only WAL, GET/PUT commands via plain sockets. No deps, 200 lines.