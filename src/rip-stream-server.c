#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <byteswap.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>

#include "slab.h"

#define MAXEVENTS 64
#define MAXCLIENTS 64

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

struct track_metadata {
    char *name;
    char *artist;
    char *album;
    uint32_t length;
};

static void print_metadata(struct track_metadata *metadata) {
    printf("%s (%s) - %s [%u B]", metadata->artist, metadata->album, metadata->name, metadata->length);
}

static int read_string(FILE *f, char **out) {
    int count;
    uint16_t len;

    count = fread(&len, 1, 2, f);
    if (count == 0) {
        fprintf(stderr, "parse_rip: unexpected EOF\n");
        return -1;
    } else if (count != 2) {
        perror("parse_rip");
        return -1;
    }

#ifdef __LITTLE_ENDIAN
    len = __bswap_16(len); 
#endif

    *out = (char *) malloc(len);

    count = fread(*out, 1, len, f);
    if (count == 0) {
        fprintf(stderr, "parse_rip: unexpected EOF\n");
        return -1;
    } else if (count != len) {
        if (errno != 0)
            perror("parse_rip");
        else
            fprintf(stderr, "parse_rip: unexpected EOF\n");
        return -1;
    }

    return 0;
}

static int parse_rip(FILE *f, struct track_metadata *metadata) {
    int status, count;
    char signature[4];
    signature[3] = 0;

    count = fread(signature, 1, 3, f);
    if (count == 0) {
        fprintf(stderr, "parse_rip: unexpected EOF\n");
        return -1;
    } else if (count != 3) {
        if (errno != 0)
            perror("parse_rip");
        else
            fprintf(stderr, "parse_rip: unexpected EOF\n");
        return -1;
    }

    if (strcmp(signature, "rip") != 0) {
        fprintf(stderr, "parse_rip: bad signature\n");
        return -1;
    }

    status = read_string(f, &metadata->name);
    if (status == -1) return -1;

    status = read_string(f, &metadata->artist);
    if (status == -1) return -1;

    status = read_string(f, &metadata->album);
    if (status == -1) return -1;

    count = fread(&metadata->length, 1, 4, f);
    if (count == 0) {
        fprintf(stderr, "parse_rip: unexpected EOF\n");
        return -1;
    } else if (count != 4) {
        if (errno != 0)
            perror("parse_rip");
        else
            fprintf(stderr, "parse_rip: unexpected EOF\n");
        return -1;
    }

#ifdef __LITTLE_ENDIAN
    metadata->length = __bswap_32(metadata->length); 
#endif

    return 0;
}

static void free_metadata(struct track_metadata *metadata) {
    free(metadata->name);
    free(metadata->artist);
    free(metadata->album);
}

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

static int create_timer(void) {
    int status, timerfd;
    struct itimerspec timespec = {
        .it_interval = {
            .tv_sec = 1,
            .tv_nsec = 0
        },
        .it_value = {
            .tv_sec = 1,
            .tv_nsec = 0
        }
    };

    timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timerfd == -1) {
        perror("timerfd_create");
        return -1;
    }

    status = timerfd_settime(timerfd, 0, &timespec, NULL);
    if (status == -1) {
        perror("timerfd_settime");
        return -1;
    }

    return timerfd;
}

struct client {
    int fd;
    int initialized;
    size_t wrote;
    size_t index;
};

static int listener_accept(int sfd, int efd, slab_t *clients) {
    int status;
    struct epoll_event event;

    while (1) {
        int infd, index;
        struct sockaddr in_addr;
        char host[NI_MAXHOST], serv[NI_MAXSERV];
        struct client client;
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
            return -1;

        client.fd = infd;
        client.initialized = 0;
        client.wrote = 0;

        index = slab_insert(clients, &client);
        if (index == -1) {
            return -1;
        }

        event.data.ptr = slab_get(clients, index);
        ((struct client *) event.data.ptr)->index = index;
        event.events = EPOLLIN | EPOLLONESHOT;

        status = epoll_ctl(efd, EPOLL_CTL_ADD, infd, &event);
        if (status == -1) {
            perror("epoll_ctl");
            return -1;
        }
    }

    return 0;
}

static int client_read(struct epoll_event *event, int efd) {
    ssize_t count;
    int status, done = 0;
    char buf;

    struct client *client = (struct client *) event->data.ptr;

    count = read(client->fd, &buf, sizeof(char));
    if (count == -1) {
        if (errno != EAGAIN) {
            perror("read");
            done = 1;
        }
    } else if (count == 0) {
        done = 1;
    }

    if (buf != 0) done = 1;

    if (done) {
        close(client->fd);
    } else {
        event->events = 0;
        status = epoll_ctl(efd, EPOLL_CTL_MOD, client->fd, event);
        if (status == -1) {
            perror("epoll_ctl");
            return -1;
        }

        client->initialized = 1;

        printf("initialized %d fd\n", client->fd);
    }

    return 0;
}

static int client_write(struct epoll_event *event, const char *buf, size_t len) {
    int done;
    ssize_t count;
    struct client *client = (struct client *) event->data.ptr;

    done = 0;

    while (client->wrote < len) {
        count = write(client->fd, buf + client->wrote, len - client->wrote);
        if (count == -1) {
            if (errno != EAGAIN) {
                perror("write");
                done = 1;
            }
            break;
        } else if (count == 0) {
            done = 1;
            break;
        }
        
        client->wrote += count;
    }
    
    if (done) {
        close(client->fd);
    }

    return 0;
}

static int timer_read(int timerfd, int efd, slab_t *clients) {
    struct epoll_event event;
    uint64_t time;
    ssize_t count;
    struct client *client;
    int status;
    slab_iter_t iter;

    count = read(timerfd, &time, 8);
    if (count != 8) {
        return -1;
    }

    event.events = EPOLLOUT | EPOLLET;

    for (slab_iter_create(clients, &iter); !slab_iter_done(&iter);
         slab_iter_next(clients, &iter))
    {
        client = (struct client *) iter.data;
        if (client->initialized) {
            client->wrote = 0;
            event.data.ptr = client;

            status = epoll_ctl(efd, EPOLL_CTL_MOD, client->fd, &event);

            if (status == -1) {
                perror("epoll_ctl");
                return -1;
            }
        } 
    }

    return 0;
}

static volatile int running = 1;

void intHandler(int sig __attribute__((unused))) {
    running = 0;
    printf("\ninterrupted");
}

const char* const USAGE = "usage: %s <port> <file>\n";

int main(int argc, char *argv[]) {
    int status, sfd, efd, timerfd;
    struct epoll_event event;
    struct epoll_event *events;
    struct track_metadata metadata;
    slab_t clients;
    FILE *rip_file;

    char buf[] = "Hello, world!\n";
    size_t len = 14;

    if (argc != 3) {
        fprintf(stderr, USAGE, argv[0]);
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, intHandler);

    status = slab_new(&clients, MAXCLIENTS, sizeof(struct client));
    if (status == -1) exit(EXIT_FAILURE);

    rip_file = fopen(argv[2], "rb");
    if (rip_file == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    status = parse_rip(rip_file, &metadata);
    if (status == -1) exit(EXIT_FAILURE);

    print_metadata(&metadata);
    printf("\n");

    sfd = bind_listener(argv[1]);
    if (sfd == -1) exit(EXIT_FAILURE);
    
    timerfd = create_timer();
    if (timerfd == -1) exit(EXIT_FAILURE);

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

    event.data.fd = timerfd;
    event.events = EPOLLIN;

    status = epoll_ctl(efd, EPOLL_CTL_ADD, timerfd, &event);
    if (status == -1) {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }

    events = (struct epoll_event *) calloc(MAXEVENTS, sizeof event);

    printf("listening on %s port %d fd\n", argv[1], sfd);

    while (running) {
        int n = epoll_wait(efd, events, MAXEVENTS, -1);

        if (n == -1) break;

        for (int i = 0; i < n; i++) {
            if (events[i].events & EPOLLERR
                || events[i].events & EPOLLHUP)
            {
                int fd = ((struct client *) events[i].data.ptr)->fd;

                printf("closed %d fd\n", fd);

                status = epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);
                if (status == -1) {
                    perror("epoll_ctl");
                    exit(EXIT_FAILURE);
                }

                slab_remove(&clients, ((struct client *) events[i].data.ptr)->index);
            } else if (events[i].data.fd == sfd) {
                status = listener_accept(sfd, efd, &clients);
                if (status == -1) exit(EXIT_FAILURE);
            } else if (events[i].data.fd == timerfd) {
                status = timer_read(timerfd, efd, &clients);
                if (status == -1) exit(EXIT_FAILURE);
            } else if (events[i].events & EPOLLIN) {
                status = client_read(&events[i], efd);
                if (status == -1) exit(EXIT_FAILURE);
            } else if (events[i].events & EPOLLOUT) {
                status = client_write(&events[i], buf, len);
                if (status == -1) exit(EXIT_FAILURE);
            }
        }
    }

    free(events);
    free_metadata(&metadata);
    close(sfd);

    return EXIT_SUCCESS;
}

