#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <netinet/tcp.h>
#include <math.h>

#include "common.h"
#include "helpers.h"

#define EPOLL_INIT_BACKSTORE 2

static int err;
static char id[11];
static int sockfd;
static int epfd;

/* Free used memory and close sockets */
void close_client() {
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
    close(epfd);
}

/* Send input command to server */
int handle_cmd() {
    char cmd_buf[MAX_CMDSIZE];
    fgets(cmd_buf, MAX_CMDSIZE, stdin);
    if (isspace(cmd_buf[0]))
        return 0;

    char* tok = strtok(cmd_buf, " \n");
    if(strcmp(tok, "exit") == 0)
        return 1;
    
    /* Check command type */
    uint8_t subs = strcmp(tok, "subscribe") == 0;
    uint8_t unsubs = strcmp(tok, "unsubscribe") == 0;
    if(subs != 0 && unsubs != 0)
        return 0;
    
    char buf[MAX_CLIENTMSGSIZE];
    memset(buf, 0, MAX_CLIENTMSGSIZE);
    struct header *hdr = (struct header*) buf;

    tok = strtok(NULL, " \n");
    hdr->len = strlen(tok);
    memcpy(buf + sizeof(struct header), tok, hdr->len);

    if(subs) {               
        tok = strtok(NULL, " \n");
        if(atoi(tok) == 1)
            hdr->type = hdr->type | SF;
        hdr->type = hdr->type | SUBSCRIBE;
    } else if (unsubs){
        hdr->type = hdr->type | UNSUBSCRIBE;
    }
    err = send_all(sockfd, buf, sizeof(struct header) + hdr->len);
    DIE(err <= 0, "send_all");

    if(subs)
        printf("Subscribed to topic.\n");
    else if(unsubs)
        printf("Unsubscribed from topic.\n");
    return 0;
}

/* Print message received from server */
void print_notification(int sockfd, char* buf) {
    struct msg *msg = (struct msg*)buf;
    char payload_output[MAX_PAYLOADSTRLEN];
    memset(payload_output, 0, MAX_PAYLOADSTRLEN);
    struct in_addr ip;
    ip.s_addr = msg->src_ip;

    char topic[MAX_TOPICLEN + 1];
    memset(topic, 0, MAX_TOPICLEN + 1);
    memcpy(topic, buf + sizeof(struct msg), msg->topic_len);

    uint8_t type = msg->payload_type;
    int payload_offset = sizeof(struct msg) + msg->topic_len;
    if(type == 0) {
        uint8_t sign = buf[payload_offset];
        uint32_t number;
        memcpy(&number, buf + payload_offset + 1, sizeof(uint32_t));
        sprintf(payload_output, "INT -%s%u", sign == 1 ? " -" : " ", ntohl(number));
    } else if (type == 1) {
        uint16_t number;
        memcpy(&number, buf + payload_offset, sizeof(uint16_t));
        number = ntohs(number);
        sprintf(payload_output, "SHORT_REAL - %.2f", (number * 1.0) / 100);
    } else if (type == 2) {
        uint8_t sign = buf[payload_offset];
        uint32_t number;
        memcpy(&number, buf + payload_offset + 1, sizeof(uint32_t));
        uint8_t power = buf[payload_offset + 1 + sizeof(uint32_t)];
        sprintf(payload_output, "FLOAT -%s%f", sign == 1 ? " -" : " ", (double)ntohl(number)/pow(10, power));
    } else if (type == 3) {
        sprintf(payload_output, "STRING - %s", buf + payload_offset);
    }
    printf("%s:%hu - %s - %s\n", inet_ntoa(ip), ntohs(msg->src_port), topic, payload_output);
}

/* Client core */
void run_client(int sockfd) {
    epfd = epoll_create(EPOLL_INIT_BACKSTORE);
    DIE(epfd == -1, "epoll_create");

    err = epoll_add_fd_in(epfd, STDIN_FILENO);
    DIE(err < 0, "epoll_add_fd_in");

    err = epoll_add_fd_in(epfd, sockfd);
    DIE(err < 0, "epoll_add_fd_in");

    while (1) {
        struct epoll_event ret_ev;
        err = epoll_wait(epfd, &ret_ev, 1, -1);
        DIE(err <= 0, "epoll_wait");

        if (ret_ev.data.fd == STDIN_FILENO && ((ret_ev.events & EPOLLIN) != 0)) {
            /* Command from input */
            int rc = handle_cmd();
            if (rc == 1)
                break;
        } else if (ret_ev.data.fd == sockfd && ((ret_ev.events & EPOLLIN) != 0)) {
            /* Message from server */
            char buf[MAX_SERVERMSGSIZE];
            memset(buf, 0, MAX_SERVERMSGSIZE);

            err = recv_all(sockfd, buf, sizeof(struct header));
            DIE(err <= 0, "recv_all");
            
            struct header *hdr = (struct header*) buf;
            if(hdr->type & ID_COMM)
                break;

            if(hdr->type & NOTIFICATION) {
                err = recv_all(sockfd, buf + sizeof(struct header), hdr->len);
                DIE(err <= 0, "recv_all");
                print_notification(sockfd, buf + sizeof(struct header));
            }
        }
    }
    close_client();
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("\n Usage: %s <ID_CLIENT> <IP_SERVER> <PORT_SERVER>\n", argv[0]);
        return 1;
    }
    DIE(strlen(argv[1]) > 10, "ID_CLIENT too long");

    uint16_t port;
    err = sscanf(argv[3], "%hu", &port);
    DIE(err != 1, "Given port is invalid");

    err = setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    DIE(err != 0, "setvbuf");

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd < 0, "socket");

    int enable = 1;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int)) < 0)
        perror("setsockopt(TCP_NODELAY) failed");

    struct sockaddr_in serv_addr;
    socklen_t socket_len = sizeof(struct sockaddr_in);

    memset(&serv_addr, 0, socket_len);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    err = inet_pton(AF_INET, argv[2], &serv_addr.sin_addr.s_addr);
    DIE(err <= 0, "inet_pton");

    err = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    DIE(err < 0, "connect");
    
    // Send ID to server
    char buf[MAX_IDMSGSIZE];
    memset(buf, 0, MAX_IDMSGSIZE);

    struct header *hdr = (struct header*)buf;
    hdr->type = hdr->type | ID_COMM;
    hdr->len = strlen(argv[1]);
    memcpy(buf + sizeof(struct header), argv[1], hdr->len);

    err = send_all(sockfd, buf, sizeof(struct header) + hdr->len);
    DIE(err < 0, "send_all");
    strcpy(id, argv[1]);
    
    // Start client
    run_client(sockfd);

    return 0;
}
