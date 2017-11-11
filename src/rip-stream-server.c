#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define MAXEVENTS 64

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif

#ifndef NI_MAXSERV
#define NI_MAXSERV 32
#endif

#ifdef DEBUG
#define DEBUG_PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG_PRINT(...) do {} while (0)
#endif

const char* const USAGE = "usage: %s <port>\n";

static int bind_listener(const char *service) {
    struct addrinfo *result, *rp;
    int status, sfd;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    
    status = getaddrinfo(NULL, service, &hints, &result);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) continue;

        status = bind(sfd, rp->ai_addr, rp->ai_addrlen);
        if (status == 0) break;
        DEBUG_PRINT("bind: %s\n", strerror(status));

        close(sfd);
    }

    if (rp == NULL) {
        perror("bind");
        return -1;
    }

    freeaddrinfo(result);

    return sfd;
}

static int set_nonblock(int sfd) {
    int flags, status;

    flags = fcntl(sfd, F_GETFL);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    status = fcntl(sfd, F_SETFL, flags); 
    if (status == -1) {
        perror("fcntl");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    int status, sfd, efd;
    struct epoll_event event;
    struct epoll_event *events;

    if (argc != 2) {
        fprintf(stderr, USAGE, argv[0]);
        exit(EXIT_FAILURE);
    }

    sfd = bind_listener(argv[1]);
    if (sfd == -1) exit(EXIT_FAILURE);

    status = set_nonblock(sfd);
    if (status == -1) exit(EXIT_FAILURE);

    status = listen(sfd, SOMAXCONN);
    if (status == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    efd = epoll_create1(0);
    if (efd == -1) {
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }

    event.data.fd = sfd;
    event.events = EPOLLIN | EPOLLET;
    
    status = epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event);
    if (status == -1) {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }

    events = (struct epoll_event *) calloc(MAXEVENTS, sizeof event);

    printf("listening on %s port %d fd\n", argv[1], sfd);

    for (;;) {
        int n = epoll_wait(efd, events, MAXEVENTS, -1);
        DEBUG_PRINT("processing %d events\n", n);

        for (int i = 0; i < n; i++) {
            if (events[i].events & EPOLLERR ||
                events[i].events & EPOLLHUP ||
                !(events[i].events & EPOLLIN))
            {
                fprintf(stderr, "epoll error\n");
                close(events[i].data.fd);
                continue;
            } else if (events[i].data.fd == sfd) {
                while (1) {
                    int infd;
                    struct sockaddr in_addr;
                    char host[NI_MAXHOST], serv[NI_MAXSERV];
                    socklen_t in_addrlen = sizeof in_addr;

                    infd = accept(sfd, &in_addr, &in_addrlen);
                    if (infd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        perror("accept");
                        break;
                    }

                    status = getnameinfo(&in_addr, in_addrlen, host, sizeof host,
                                         serv, sizeof serv,
                                         NI_NUMERICHOST | NI_NUMERICSERV);
                    if (status == 0)
                        printf("accepted %s:%s on %d fd\n", host, serv, infd);
                    
                    status = set_nonblock(infd);
                    if (status == -1)
                        exit(EXIT_FAILURE);

                    event.data.fd = infd;
                    event.events = EPOLLIN | EPOLLET;

                    status = epoll_ctl(efd, EPOLL_CTL_ADD, infd, &event);
                    if (status == -1) {
                        perror("epoll_ctl");
                        exit(EXIT_FAILURE);
                    }
                }
            } else {
                int done = 0; 

                for (;;) {
                    ssize_t count;
                    char buf[512];

                    count = read(events[i].data.fd, buf, sizeof buf);
                    if (count == -1) {
                        if (errno != EAGAIN) {
                            perror("read");
                            done = 1;
                        }
                        break;
                    } else if (count == 0) {
                        done = 1;
                        break;
                    }

                    status = write(1, buf, count);
                    if (status == -1) {
                        perror("write");
                        exit(EXIT_FAILURE);
                    }
                }

                if (done) {
                    printf("closed %d fd\n", events[i].data.fd);
                    close(events[i].data.fd);
                }
            }
        }
    }

    free(events);
    close(sfd);

    return EXIT_SUCCESS;
}

