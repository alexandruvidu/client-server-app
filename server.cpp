#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <map>
#include <list>
#include <ctype.h>

#include "common.h"
#include "helpers.h"

#define EPOLL_INIT_BACKSTORE 2

static int err;
static int epfd;
static int tcpfd;
static int udpfd;

/* Required struct to use char* as key in map */
struct cmp_str
{
   bool operator()(char const *a, char const *b) const
   {
      return strcmp(a, b) < 0;
   }
};

std::map<char*, struct conn*, cmp_str> clients;
std::map<int, char*> sockets;
std::map<char*, std::list<struct sub_entry*>, cmp_str> topics;

/* Free used memory and close all sockets */
void close_server() {
    for(std::pair<char*, struct conn*> item : clients) {
        free(item.first);
        destroyQueue(item.second->q);
        if(item.second->fd != -1)
            close(item.second->fd);
        free(item.second);
    }

    for(std::pair<char*, std::list<struct sub_entry*>> item : topics) {
        free(item.first);
        for(struct sub_entry* list_item : item.second)
            free(list_item);
    }

    shutdown(udpfd, SHUT_RDWR);
    close(udpfd);
    shutdown(tcpfd, SHUT_RDWR);
    close(tcpfd);
    close(epfd);
}

void already_connected(char *id, int fd) {
    struct header reply;
    memset(&reply, 0, sizeof(struct header));
    reply.type = reply.type | ID_COMM;

    /* Send packet of type ID_COMM to TCP client */
    err = send_all(fd, &reply, sizeof(struct header));
    if(err <= 0)
        return;

    close(fd);
    printf("Client %s already connected.\n", id);

    free(id);
}

void reconnected(char *id, int fd) {
    clients[id]->fd = fd;
    sockets[fd] = id;

    /* Send all queued messages to client */
    while(!isQueueEmpty(clients[id]->q)) {
        char *q_msg = front(clients[id]->q);
        dequeue(clients[id]->q);

        struct header *hdr = (struct header*) q_msg;

        err = send_all(fd, q_msg, sizeof(struct header) + hdr->len);
        if(err <= 0)
            break;

        free(q_msg);
    }
}

void connected(char *id, int fd) {
    struct conn *connection = (struct conn*) calloc(1, sizeof(struct conn));
    connection->fd = fd;
    connection->q = createQueue();
    clients[id] = connection;
    sockets[fd] = id;
}

void new_udp_message(int fd) {
    char udp_buf[MAX_UDPBUFSIZE];
    memset(udp_buf, 0, MAX_UDPBUFSIZE);
    struct sockaddr_in udp_addr;
    int addr_len = sizeof(struct sockaddr);
    err = recvfrom(fd, &udp_buf, MAX_UDPBUFSIZE, 0, (struct sockaddr*)&udp_addr, (socklen_t*)&addr_len);
    
    uint8_t type = udp_buf[50];

    char tcp_buf[MAX_SERVERMSGSIZE];
    memset(tcp_buf, 0, MAX_SERVERMSGSIZE);

    /* Initialize header for packet */
    struct header *hdr = (struct header*)tcp_buf;
    hdr->type = hdr->type | NOTIFICATION;

    /* Initialize header for message */
    struct msg *packet = (struct msg*)(tcp_buf + sizeof(struct header));
    packet->src_ip = udp_addr.sin_addr.s_addr;
    packet->src_port = udp_addr.sin_port;
    int x = strlen(udp_buf);
    packet->topic_len = x <= MAX_TOPICLEN ? x : MAX_TOPICLEN;
    packet->payload_type = type;

    /* Copy topic in packet */
    strncpy(tcp_buf + sizeof(struct header) + sizeof(struct msg), udp_buf, packet->topic_len);
    packet->topic_len = strlen(tcp_buf + sizeof(struct header) + sizeof(struct msg));
    if(type == 0)
        hdr->len = 1 + sizeof(uint32_t);
    else if(type == 1)
        hdr->len = sizeof(uint16_t);
    else if(type == 2)
        hdr->len = 1 + sizeof(uint32_t) + sizeof(uint8_t);
    else if(type == 3)
        hdr->len = strlen(udp_buf + MAX_TOPICLEN + 1);

    /* Copy payload in packet */
    memcpy(tcp_buf + sizeof(struct header) + sizeof(struct msg) + packet->topic_len, 
            udp_buf + MAX_TOPICLEN + 1, hdr->len);

    hdr->len = hdr->len + sizeof(struct msg) + packet->topic_len;

    char topic[MAX_TOPICLEN + 1];
    memset(topic, 0, MAX_TOPICLEN + 1);
    strncpy(topic, udp_buf, packet->topic_len);
    
    for(struct sub_entry* entry : topics[topic]) {
        if(clients[entry->id]->fd != -1)
            err = send_all(clients[entry->id]->fd, &tcp_buf, sizeof(struct header) + hdr->len);
        else if(entry->sf != 0) {
            char *q_packet = (char*) calloc(sizeof(struct header) + hdr->len, sizeof(char));
            memcpy(q_packet, &tcp_buf, sizeof(struct header) + hdr->len);
            enqueue(clients[entry->id]->q, q_packet);
        }
    }
}

void new_tcp_message(int fd) {
    char recv_pkt[MAX_CLIENTMSGSIZE];
    memset(recv_pkt, 0, MAX_CLIENTMSGSIZE);
    int len = recv_all(fd, recv_pkt, sizeof(struct header)); 
    DIE(len < 0, "recv_all");

    /* Close connection */
    if (len == 0) {
        printf("Client %s disconnected.\n", sockets[fd]);
        clients[sockets[fd]]->fd = -1;
        close(fd); 
    }

    struct header *hdr = (struct header*)recv_pkt;
    err = recv_all(fd, recv_pkt + sizeof(struct header), hdr->len);

    if(hdr->type & ID_COMM) { /* Receive client id */
        int id_len = strlen(recv_pkt + sizeof(struct header));
        char *id = (char*)calloc(id_len, sizeof(char));
        memcpy(id, recv_pkt + sizeof(struct header), id_len);

        if(clients.count(id) > 0 && clients[id]->fd != -1) { /* Already connected client */
            already_connected(id, fd);
            return;
        }
        else if(clients.count(id) > 0) /* Old client reconnecting */
            reconnected(id, fd);
        else /* New client */
            connected(id, fd);
    
        struct sockaddr_in cli_addr; 
        socklen_t cli_len = sizeof(cli_addr);
        err = getpeername(fd, (struct sockaddr*) &cli_addr, &cli_len);
        DIE(err < 0, "getpeername");

        printf("New client %s connected from %s:%d\n",
                id, inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
        return;
    }

    /* Receive a command (subscribe/unsubscribe) */
    char *topic = (char*) calloc(hdr->len + 1, sizeof(char));
    strncpy(topic, recv_pkt + sizeof(struct header), hdr->len);
    if(hdr->type & UNSUBSCRIBE) { /* Unsubscribe */
        for(auto iter = topics[topic].begin(); iter != topics[topic].end(); iter++) {
            if(strcmp((*iter)->id, sockets[fd]) == 0) {
                topics[topic].erase(iter);
                break;
            }
        }
    }
    else if(hdr->type & SUBSCRIBE) { /* Subscribe */
        int found = 0;
        /* Check if user is already subscribed to topic */
        for(auto iter = topics[topic].begin(); iter != topics[topic].end(); iter++) {
            if(strcmp((*iter)->id, sockets[fd]) == 0) {
                /* Update SF option if is already subscribed */
                (*iter)->sf = hdr->type & SF;
                found = 1;
                break;
            }
        }
        if(found) 
            return;

        struct sub_entry *sub_entry = (struct sub_entry*) calloc(1, sizeof(struct sub_entry));
        strcpy(sub_entry->id, sockets[fd]);
        sub_entry->sf = hdr->type & SF;
        topics[topic].push_back(sub_entry);
    }
}

/* Server core */
void run_server(int tcpfd, int udpfd) {
    epfd = epoll_create(EPOLL_INIT_BACKSTORE);
    DIE(epfd == -1, "epoll_create");

    err = listen(tcpfd, SOMAXCONN);
    DIE(err < 0, "listen");

    err = epoll_add_fd_in(epfd, STDIN_FILENO);
    DIE(err < 0, "epoll_add_fd_in stdin");

    err = epoll_add_fd_in(epfd, tcpfd);
    DIE(err < 0, "epoll_add_fd_in tcp");

    err = epoll_add_fd_in(epfd, udpfd);
    DIE(err < 0, "epoll_add_fd_in udp");

    while (1) {
        struct epoll_event ret_ev;
        epoll_wait(epfd, &ret_ev, 1, -1);

        if (ret_ev.data.fd == STDIN_FILENO && ((ret_ev.events & EPOLLIN) != 0)) { // Command received from input
            char buf[5];
            fgets(buf, sizeof(buf), stdin);
            if (isspace(buf[0]))
                continue;
            if(strcmp(buf, "exit") == 0)
                break;
        }
        else if (ret_ev.data.fd == tcpfd && ((ret_ev.events & EPOLLIN) != 0)) { // Connection from TCP client
            struct sockaddr_in cli_addr; 
            socklen_t cli_len = sizeof(cli_addr);

            int newsockfd = accept(tcpfd, (struct sockaddr *)&cli_addr, &cli_len);
            DIE(newsockfd < 0, "accept");

            int enable = 1;
            if (setsockopt(newsockfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int)) < 0)
                perror("setsockopt(TCP_NODELAY) failed");

            err = epoll_add_fd_in(epfd, newsockfd);
            DIE(err < 0, "epoll_add_fd_in");
        } 
        else if (ret_ev.data.fd == udpfd && ((ret_ev.events & EPOLLIN) != 0)) { // Message from UDP client
            new_udp_message(ret_ev.data.fd);
        }
        else { // Message from TCP client
            new_tcp_message(ret_ev.data.fd);
        }
    }
    close_server();
    close(epfd);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
      printf("\n Usage: %s <port>\n", argv[0]);
      return 1;
    }

    uint16_t port;
    err = sscanf(argv[1], "%hu", &port);
    DIE(err != 1, "Given port is invalid");

    err = setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    DIE(err != 0, "setvbuf");

    udpfd = socket(AF_INET, SOCK_DGRAM, 0);
    DIE(udpfd < 0, "socket");
    tcpfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(tcpfd < 0, "socket");

    struct sockaddr_in serv_addr;
    socklen_t socket_len = sizeof(struct sockaddr_in);

    int enable = 1;
    if (setsockopt(udpfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");
    if (setsockopt(tcpfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");
    if (setsockopt(tcpfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int)) < 0)
        perror("setsockopt(TCP_NODELAY) failed");

    memset(&serv_addr, 0, socket_len);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    err = bind(udpfd, (const struct sockaddr *)&serv_addr, sizeof(serv_addr));
    DIE(err < 0, "bind");

    err = bind(tcpfd, (const struct sockaddr *)&serv_addr, sizeof(serv_addr));
    DIE(err < 0, "bind");

    run_server(tcpfd, udpfd);

    return 0;
}
