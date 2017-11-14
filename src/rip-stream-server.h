#ifndef RIP_STREAM_SERVER_H
#define RIP_STREAM_SERVER_H

#include <stdio.h>
#include <stdint.h>

#include "slab.h"
#include "rip.h"

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
    unsigned int initialized: 1;
    unsigned int needs_metadata: 1;
    size_t wrote;
    size_t index;
};

static int listener_accept(int sfd, int efd, slab_t *clients);
static int timer_read(int timerfd, int efd, slab_t *clients,
                      char *chunk, size_t *chunk_len, FILE **rip_file,
                      char **playlist, int *current_song, int playlist_size,
                      struct rip_metadata *metadata, char **metadata_out,
                      size_t *metadata_out_len, uint32_t *rip_time);

static int client_read(struct client *event, slab_t *clients, int efd);
static int client_write(struct client *event, const char *buf, size_t len,
                        slab_t *clients, int efd);
static int client_close(struct client *client, slab_t *clients, int efd);

static int load_playlist(char *dir_path, char ***out);
static int load_song(char *song_path, struct rip_metadata *metadata,
                     char **metadata_out, size_t *metadata_out_len,
                     uint32_t *time, FILE **rip_file);

void intHandler(int sig);

#endif

