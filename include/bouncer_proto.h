#ifndef BOUNCER_PROTO_H
#define BOUNCER_PROTO_H

#include <stddef.h>
#include <stdint.h>

#define BOUNCER_MAGIC "BNCE"
#define BOUNCER_ACK "OK\n"
#define BOUNCER_MAGIC_LEN 4
#define BOUNCER_ACK_LEN 3

// Header structure for JSON serialization
// { "from": "...", "to": "...", "kind": null, "source": null }
typedef struct {
    const char *from;
    const char *to;
    const char *kind;
    const char *source;
} bouncer_header_t;

#endif // BOUNCER_PROTO_H
