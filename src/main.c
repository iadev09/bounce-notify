#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "bouncer_proto.h"

/*
 * MUST SYNC: This value should match 'bounce_size_limit' in Postfix main.cf.
 * Postfix default: 50000. Recommended: 51200 (50KB).
 * If Postfix sends more than this and we stop reading, the Rust parser
 * will receive a truncated MIME part and fail to find the Message-ID.
 */
#define MAX_BODY_BYTES (50 * 1024)
#define DEFAULT_TIMEOUT_SECS 10
#ifndef BOUNCE_NOTIFY_VERSION
#define BOUNCE_NOTIFY_VERSION "dev"
#endif

// Postfix/Standard exit codes
#define EX_USAGE 64
#define EX_TEMPFAIL 75

typedef struct {
    char *server;
    char *from;
    char *to;
    int timeout_secs;
} cli_args_t;

typedef enum {
    PARSE_OK = 0,
    PARSE_USAGE = -1,
    PARSE_HELP = 1,
    PARSE_VERSION = 2,
} parse_result_t;

void print_usage(const char *progname) {
    fprintf(stderr,
            "Usage: %s --server host:port --from sender --to recipient [--timeout-secs 10] [--version]\n",
            progname);
}

void print_version(void) { printf("bounce-notify %s\n", BOUNCE_NOTIFY_VERSION); }

parse_result_t parse_args(int argc, char **argv, cli_args_t *args) {
    args->server = NULL;
    args->from = NULL;
    args->to = NULL;
    args->timeout_secs = DEFAULT_TIMEOUT_SECS;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) {
            args->server = argv[++i];
        } else if (strcmp(argv[i], "--from") == 0 && i + 1 < argc) {
            args->from = argv[++i];
        } else if (strcmp(argv[i], "--to") == 0 && i + 1 < argc) {
            args->to = argv[++i];
        } else if (strcmp(argv[i], "--timeout-secs") == 0 && i + 1 < argc) {
            args->timeout_secs = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            return PARSE_HELP;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            return PARSE_VERSION;
        } else {
            fprintf(stderr, "Unknown or missing argument: %s\n", argv[i]);
            return PARSE_USAGE;
        }
    }

    if (!args->server || !args->from || !args->to) {
        return PARSE_USAGE;
    }
    return PARSE_OK;
}

// Minimal JSON manual serialization to avoid dependencies
char *serialize_header(const bouncer_header_t *header) {
    // Expected format: {"from":"...","to":"...","kind":null,"source":null}
    // We'll be conservative with buffer size.
    size_t len = strlen(header->from) + strlen(header->to) + 64;
    char *buf = malloc(len);
    if (!buf) return NULL;

    snprintf(buf, len, "{\"from\":\"%s\",\"to\":\"%s\",\"kind\":null,\"source\":null}", header->from, header->to);
    return buf;
}

int resolve_and_connect(const char *server, int timeout_secs) {
    char *host = strdup(server);
    char *port_str = strchr(host, ':');
    if (!port_str) {
        free(host);
        return -1;
    }
    *port_str = '\0';
    port_str++;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        free(host);
        return -1;
    }
    free(host);

    int sockfd = -1;
    struct addrinfo *p;
    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) continue;

        // Set timeout for connect
        struct timeval tv;
        tv.tv_sec = timeout_secs;
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(res);
    return sockfd;
}

int write_all(int fd, const void *buf, size_t len) {
    size_t total = 0;
    const char *p = buf;
    while (total < len) {
        ssize_t n = write(fd, p + total, len - total);
        if (n <= 0) return -1;
        total += n;
    }
    return 0;
}

int read_all(int fd, void *buf, size_t len) {
    size_t total = 0;
    char *p = buf;
    while (total < len) {
        ssize_t n = read(fd, p + total, len - total);
        if (n <= 0) return -1;
        total += n;
    }
    return 0;
}

int main(int argc, char **argv) {
    cli_args_t args;
    parse_result_t parse_result = parse_args(argc, argv, &args);
    if (parse_result == PARSE_HELP) {
        print_usage(argv[0]);
        return 0;
    }
    if (parse_result == PARSE_VERSION) {
        print_version();
        return 0;
    }
    if (parse_result != PARSE_OK) {
        print_usage(argv[0]);
        return EX_USAGE;
    }

    // Read body from stdin
    unsigned char *body = malloc(MAX_BODY_BYTES + 1);
    if (!body) {
        fprintf(stderr, "Failed to allocate memory for body\n");
        return EX_TEMPFAIL;
    }

    size_t body_len = 0;
    ssize_t n;
    while ((n = read(STDIN_FILENO, body + body_len, MAX_BODY_BYTES + 1 - body_len)) > 0) {
        body_len += n;
        if (body_len > MAX_BODY_BYTES) {
            fprintf(stderr, "Mail body too large\n");
            free(body);
            return EX_TEMPFAIL;
        }
    }

    bouncer_header_t header = {.from = args.from, .to = args.to, .kind = NULL, .source = NULL};
    char *header_json = serialize_header(&header);
    if (!header_json) {
        free(body);
        return EX_TEMPFAIL;
    }
    uint32_t header_len = (uint32_t) strlen(header_json);
    uint64_t body_len_64 = (uint64_t) body_len;

    int sockfd = resolve_and_connect(args.server, args.timeout_secs);
    if (sockfd == -1) {
        fprintf(stderr, "Failed to connect to %s\n", args.server);
        free(header_json);
        free(body);
        return EX_TEMPFAIL;
    }

    // Write frame
    uint32_t header_len_be = htonl(header_len);
    unsigned char body_len_buf[8];
    for (int i = 0; i < 8; i++) {
        body_len_buf[i] = (unsigned char) ((body_len_64 >> (8 * (7 - i))) & 0xFF);
    }

    if (write_all(sockfd, BOUNCER_MAGIC, 4) != 0 || write_all(sockfd, &header_len_be, 4) != 0 ||
        write_all(sockfd, body_len_buf, 8) != 0 || write_all(sockfd, header_json, header_len) != 0 ||
        write_all(sockfd, body, body_len) != 0) {
        fprintf(stderr, "Failed to send frame\n");
        close(sockfd);
        free(header_json);
        free(body);
        return EX_TEMPFAIL;
    }

    // Read ACK
    char ack_buf[3];
    if (read_all(sockfd, ack_buf, 3) != 0 || memcmp(ack_buf, BOUNCER_ACK, 3) != 0) {
        fprintf(stderr, "Invalid or missing ACK from server\n");
        close(sockfd);
        free(header_json);
        free(body);
        return EX_TEMPFAIL;
    }

    close(sockfd);
    free(header_json);
    free(body);
    return 0;
}

