#ifndef __COMMON_H__
#define __COMMON_H__

#include <stddef.h>
#include <stdint.h>

typedef char* Item;
#include "queue.h"

/* Max sizes */
#define MAX_CMDSIZE         66
#define MAX_TOPICLEN        50
#define MAX_UDPBUFSIZE      1551
#define MAX_SERVERMSGSIZE   sizeof(struct header) + sizeof(struct msg) + MAX_UDPBUFSIZE - 1
#define MAX_CLIENTMSGSIZE   sizeof(struct header) + MAX_TOPICLEN
#define MAX_IDMSGSIZE       sizeof(struct header) + 10
#define MAX_PAYLOADSTRLEN   1509

/* Identifiers for message type used in header */
#define UNSUBSCRIBE     1
#define SUBSCRIBE       2
#define NOTIFICATION    4
#define ID_COMM         8
#define SF              16

/* Header */
struct header {
    uint8_t type;           /* Message type */
    uint16_t len;           /* Message length */
}__attribute__((packed));

/* Header of a message received from an UDP client */
struct msg {
    uint32_t src_ip;        /* UDP client IP address */
    uint16_t src_port;      /* UDP client port */
    uint8_t topic_len;      /* Topic length */
    uint8_t payload_type;   /* Message type (0 - INT, 1 - SHORT_REAL, 2 - FLOAT, 3 - STRING) */
}__attribute__((packed));

/* TCP client connection */
struct conn {
    int fd;                 /* Subscriber's socket ID, -1 if client is not connected */
    Queue *q;               /* Queue which contains messages received when disconnected */
};

/* Subscribe */
struct sub_entry {
    char id[11];            /* Subscriber ID */
    uint8_t sf;             /* Type of subscribe */
};

int send_all(int sockfd, void *buff, size_t len);
int recv_all(int sockfd, void *buff, size_t len);
int epoll_add_fd_in(int epollfd, int fd);

#endif
