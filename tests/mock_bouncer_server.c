#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "bouncer_proto.h"

#define DEFAULT_LISTEN "127.0.0.1:32147"
#define MAX_HEADER_BYTES (16 * 1024)
#define MAX_BODY_BYTES (2 * 1024 * 1024)

static int read_all(int fd, void *buf, size_t len) {
    size_t total = 0;
    char *p = (char *) buf;
    while (total < len) {
        ssize_t n = read(fd, p + total, len - total);
        if (n <= 0) return -1;
        total += (size_t) n;
    }
    return 0;
}

static int write_all(int fd, const void *buf, size_t len) {
    size_t total = 0;
    const char *p = (const char *) buf;
    while (total < len) {
        ssize_t n = write(fd, p + total, len - total);
        if (n <= 0) return -1;
        total += (size_t) n;
    }
    return 0;
}

static uint64_t read_u64_be(const unsigned char in[8]) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | in[i];
    return v;
}

static int parse_host_port(const char *listen_addr, char **host_out, char **port_out) {
    char *tmp = strdup(listen_addr);
    char *sep = strrchr(tmp, ':');
    if (!sep) {
        free(tmp);
        return -1;
    }
    *sep = '\0';
    *host_out = strdup(tmp);
    *port_out = strdup(sep + 1);
    free(tmp);
    if (!*host_out || !*port_out) {
        free(*host_out);
        free(*port_out);
        return -1;
    }
    return 0;
}

static int make_server_socket(const char *listen_addr) {
    char *host = NULL;
    char *port = NULL;
    if (parse_host_port(listen_addr, &host, &port) != 0) return -1;

    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int rc = getaddrinfo(host, port, &hints, &res);
    free(host);
    free(port);
    if (rc != 0 || !res) return -1;

    int listen_fd = -1;
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        listen_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listen_fd < 0) continue;

        int one = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        if (bind(listen_fd, p->ai_addr, p->ai_addrlen) == 0 && listen(listen_fd, 1) == 0) break;
        close(listen_fd);
        listen_fd = -1;
    }
    freeaddrinfo(res);
    return listen_fd;
}

int main(int argc, char **argv) {
    const char *listen_addr = DEFAULT_LISTEN;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            listen_addr = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            fprintf(stderr, "Usage: %s [--listen host:port]\n", argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 1;
        }
    }

    int listen_fd = make_server_socket(listen_addr);
    if (listen_fd < 0) {
        fprintf(stderr, "Failed to listen on %s\n", listen_addr);
        return 2;
    }
    fprintf(stdout, "LISTENING %s\n", listen_addr);
    fflush(stdout);

    int conn_fd = accept(listen_fd, NULL, NULL);
    close(listen_fd);
    if (conn_fd < 0) {
        fprintf(stderr, "Accept failed\n");
        return 3;
    }

    char magic[BOUNCER_MAGIC_LEN];
    uint32_t header_len_be = 0;
    unsigned char body_len_buf[8];

    if (read_all(conn_fd, magic, sizeof(magic)) != 0 || read_all(conn_fd, &header_len_be, 4) != 0 ||
        read_all(conn_fd, body_len_buf, 8) != 0) {
        fprintf(stderr, "Failed to read frame prefix\n");
        close(conn_fd);
        return 4;
    }

    if (memcmp(magic, BOUNCER_MAGIC, BOUNCER_MAGIC_LEN) != 0) {
        fprintf(stderr, "Bad magic\n");
        close(conn_fd);
        return 5;
    }

    uint32_t header_len = ntohl(header_len_be);
    uint64_t body_len = read_u64_be(body_len_buf);

    if (header_len == 0 || header_len > MAX_HEADER_BYTES || body_len > MAX_BODY_BYTES) {
        fprintf(stderr, "Invalid lengths header=%u body=%llu\n", header_len, (unsigned long long) body_len);
        close(conn_fd);
        return 6;
    }

    char *header = (char *) malloc((size_t) header_len + 1);
    unsigned char *body = (unsigned char *) malloc((size_t) body_len);
    if (!header || (!body && body_len > 0)) {
        fprintf(stderr, "Allocation failed\n");
        free(header);
        free(body);
        close(conn_fd);
        return 7;
    }

    if (read_all(conn_fd, header, header_len) != 0 || (body_len > 0 && read_all(conn_fd, body, (size_t) body_len) != 0)) {
        fprintf(stderr, "Failed to read frame payload\n");
        free(header);
        free(body);
        close(conn_fd);
        return 8;
    }
    header[header_len] = '\0';

    fprintf(stdout, "FRAME header_len=%u body_len=%llu\n", header_len, (unsigned long long) body_len);
    fprintf(stdout, "HEADER %s\n", header);

    if (write_all(conn_fd, BOUNCER_ACK, BOUNCER_ACK_LEN) != 0) {
        fprintf(stderr, "Failed to write ACK\n");
        free(header);
        free(body);
        close(conn_fd);
        return 9;
    }

    fprintf(stdout, "RESULT ok\n");
    fflush(stdout);

    free(header);
    free(body);
    close(conn_fd);
    return 0;
}
