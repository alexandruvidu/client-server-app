#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>

int recv_all(int sockfd, void *buffer, size_t len) {
    size_t bytes_received = 0;
    size_t bytes_remaining = len;
    while(bytes_remaining) {
        int count = recv(sockfd, (char*)buffer + bytes_received, bytes_remaining, 0);
        if (count == -1 || count == 0)
            return count;
        bytes_received += count;
        bytes_remaining -= count;
    }
    return bytes_received;
}

int send_all(int sockfd, void *buffer, size_t len) {
    size_t bytes_sent = 0;
    size_t bytes_remaining = len;
    while(bytes_remaining) {
        int count = send(sockfd, (char*)buffer + bytes_sent, bytes_remaining, 0);
        if (count == -1 || count == 0)
            return count;
        bytes_sent += count;
        bytes_remaining -= count;
    }
    return bytes_sent;
}

int epoll_add_fd_in(int epollfd, int fd) {
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    return epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
}