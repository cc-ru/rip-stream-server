#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h> 
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>

#include "slab.h"
#include "rip.h"
#include "rip-stream-server.h"

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
        client.needs_metadata = 1;

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

static int timer_read(int timerfd, int efd, slab_t *clients,
                      char *chunk, size_t *chunk_len, FILE **rip_file,
                      char **playlist, int *current_song, int playlist_size,
                      struct rip_metadata *metadata, char **metadata_out,
                      size_t *metadata_out_len, uint32_t *rip_time)
{
    struct epoll_event event;
    ssize_t count;
    struct client *client;
    ssize_t time;
    int status, next = 0;
    slab_iter_t iter;

    count = read(timerfd, &time, 8);
    if (count != 8) return -1; 

    *chunk_len = rip_read_chunk(*rip_file, chunk, rip_time);
    if (*chunk_len == (size_t) -1)
        return -1;
    else if (*chunk_len == 0) {
        *current_song += 1;

        if (*current_song >= playlist_size) *current_song = 0;
        status = load_song(playlist[*current_song], metadata,
                  metadata_out, metadata_out_len, rip_time, rip_file);
        if (status == -1) return -1;
        next = 1;
    }

    event.events = EPOLLOUT | EPOLLET;

    for (slab_iter_create(clients, &iter); !slab_iter_done(&iter);
         slab_iter_next(clients, &iter))
    {
        client = (struct client *) iter.data;
        if (client->initialized) {
            client->wrote = 0;
            client->needs_metadata = next;
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

static int client_read(struct client *client, slab_t *clients, int efd) {
    ssize_t count;
    struct epoll_event event;
    char buf;
    int status, closing = 0;

    count = recv(client->fd, &buf, sizeof(char), 0);
    if (count == -1) {
        if (errno != EAGAIN) {
            closing = 1;
        }
    }

    if (count == 0 || buf != 'a') closing = 1;

    if (closing) {
        client_close(client, clients, efd);
    } else {
        event.data.ptr = client;
        event.events = EPOLLOUT | EPOLLET;

        status = epoll_ctl(efd, EPOLL_CTL_MOD, client->fd, &event);
        if (status == -1) {
            perror("epoll_ctl");
            return -1;
        }

        client->initialized = 1;

        printf("initialized %d fd\n", client->fd);
    }

    return 0;
}

static int client_write(struct client *client, const char *buf, size_t len,
                        slab_t *clients, int efd)
{
    int closing;
    ssize_t count;

    closing = 0;

    if (len == 0) closing = 1;

    while (client->wrote < len) {
        count = send(client->fd, buf + client->wrote, len - client->wrote, MSG_NOSIGNAL);
        if (count == -1) {
            if (errno != EAGAIN) {
                closing = 1;
            }
            break;
        } else if (count == 0) {
            closing = 1;
            break;
        }
        
        client->wrote += count;
    }

    if (client->wrote == len) {
        client->needs_metadata = 0;
    }
    
    if (closing) {
        client_close(client, clients, efd);
    }

    return 0;
}

static int client_close(struct client *client, slab_t *clients, int efd) {
    int status;

    printf("closed %d fd\n", client->fd);

    shutdown(client->fd, SHUT_RDWR);

    status = epoll_ctl(efd, EPOLL_CTL_DEL, client->fd, NULL);
    if (status == -1) {
        perror("epoll_ctl");
        return -1;
    }

    slab_remove(clients, client->index);
    return 0;
}

static int load_playlist(char *dir_path, char ***out) {
    struct dirent *dir;
    size_t dir_path_len = strlen(dir_path);
    char *dot;
    DIR *d;
    int n = 0, i = 0;

    d = opendir(dir_path);
    if (d == NULL) {
        perror("opendir");
        return -1;
    }

    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type != DT_REG) continue;
        dot = strrchr(dir->d_name, '.');
        if (!dot || strcmp(dot, ".rip")) continue;
        n++; 
    }

    closedir(d);

    if (n == 0) {
        fprintf(stderr, "empty playlist");
        return -1;
    }

    d = opendir(dir_path);
    if (d == NULL) {
        perror("opendir");
        return -1;
    }

    *out = (char **) malloc(n * sizeof(char *));
    if (*out == NULL) return -1;

    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type != DT_REG) continue;
        dot = strrchr(dir->d_name, '.');
        if (!dot || strcmp(dot, ".rip")) continue;

        (*out)[i] = (char *) malloc(strlen(dir->d_name) + dir_path_len + 2);
        if ((*out)[i] == NULL) return -1;
        
        memcpy((*out)[i], dir_path, dir_path_len);
        (*out)[i][dir_path_len] = '/';
        memcpy((*out)[i] + dir_path_len + 1, dir->d_name,
               strlen(dir->d_name) + 1);

        i++;
    }

    closedir(d);

    return n;
}

static int load_song(char *song_path, struct rip_metadata *metadata,
                     char **metadata_out, size_t *metadata_out_len,
                     uint32_t *time, FILE **rip_file)
{
    int status;

    if (*rip_file != NULL)
        fclose(*rip_file);

    *rip_file = fopen(song_path, "rb");
    if (rip_file == NULL) {
        perror("fopen");
        return -1;
    }

    status = rip_parse_metadata(*rip_file, metadata);
    if (status == -1) return -1;
    
    printf("current song: ");
    rip_print_metadata(metadata);
    printf("\n");

    *metadata_out_len = rip_encode_metadata(metadata, metadata_out);
    if (*metadata_out_len == (size_t) -1) return -1;

    *time = 0;

    return 0;
}

static volatile int running = 1;

void intHandler(int sig __attribute__((unused))) {
    running = 0;
    printf("\ninterrupted");
}

const char* const USAGE = "usage: %s <port> <playlist>\n";

int main(int argc, char *argv[]) {
    int status, sfd, efd, timerfd;
    struct epoll_event event;
    struct epoll_event *events;
    struct rip_metadata metadata;
    slab_t clients;
    FILE *rip_file;

    char *metadata_out;
    size_t metadata_out_len;

    char chunk[8 * SAMPLESIZE * SAMPLERATE + 9] = {0};
    size_t chunk_len = 0;
    uint32_t time = 0;

    char **playlist = NULL;
    int current_song = 0;
    int playlist_size;
    
    if (argc != 3) {
        fprintf(stderr, USAGE, argv[0]);
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, intHandler);

    status = slab_new(&clients, MAXCLIENTS, sizeof(struct client));
    if (status == -1) exit(EXIT_FAILURE);

    playlist_size = load_playlist(argv[2], &playlist);
    if (playlist_size == -1) exit(EXIT_FAILURE);

    printf("playlist loaded (%d songs)\n", playlist_size);

    status = load_song(playlist[current_song], &metadata, &metadata_out,
                       &metadata_out_len, &time, &rip_file);
    if (status == -1) exit(EXIT_FAILURE);

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
            struct client *client = (struct client *) events[i].data.ptr;

            if (events[i].data.fd == sfd) {
                status = listener_accept(sfd, efd, &clients);
                if (status == -1) exit(EXIT_FAILURE);

            } else if (events[i].data.fd == timerfd) {
                status = timer_read(timerfd, efd, &clients, chunk, &chunk_len,
                                    &rip_file, playlist, &current_song,
                                    playlist_size, &metadata, &metadata_out,
                                    &metadata_out_len, &time);

                if (status == -1) exit(EXIT_FAILURE);

            } else if (events[i].events & EPOLLIN) {
                status = client_read(client, &clients, efd);
                if (status == -1) exit(EXIT_FAILURE);

            } else if (events[i].events & EPOLLOUT) {
                if (client->needs_metadata)
                    status = client_write(client, metadata_out,
                                          metadata_out_len,
                                          &clients, efd);
                else
                    status = client_write(client, chunk, chunk_len,
                                          &clients, efd);
                if (status == -1) exit(EXIT_FAILURE);

            } else if (events[i].events & EPOLLHUP
                       || events[i].events & EPOLLERR)
            {
                status = client_close(client, &clients, efd);
                if (status == -1) exit(EXIT_FAILURE);
            }
        }
    }

    free(events);
    rip_free_metadata(&metadata);
    close(sfd);
    fclose(rip_file);

    return EXIT_SUCCESS;
}

