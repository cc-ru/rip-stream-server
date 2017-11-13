#ifndef RIP_STREAM_SERVER_H
#define RIP_STREAM_SERVER_H

#include <stdio.h>
#include <stdint.h>

#include "slab.h"

#define MAXEVENTS 64
#define MAXCLIENTS 64

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif

#ifndef NI_MAXSERV
#define NI_MAXSERV 32
#endif

static int bind_listener(const char *service);
static int set_nonblock(int sfd);
static int create_timer(void);

struct client {
    int fd;
    int initialized;
    int first_time;
    size_t wrote;
    size_t index;
};

static int listener_accept(int sfd, int efd, slab_t *clients);
static int timer_read(int timerfd, int efd, slab_t *clients,
                      char *chunk, size_t *chunk_len, FILE *rip_file,
                      uint32_t *rip_time);

static int client_read(struct client *event, slab_t *clients, int efd);
static int client_write(struct client *event, const char *buf, size_t len,
                        slab_t *clients, int efd);
static int client_close(struct client *client, slab_t *clients, int efd);

void intHandler(int sig);

#endif

