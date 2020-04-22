#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include <gmodule.h>

#define MSG_LEN_MAX 512
#define EPOLL_MAX_EVENTS 100

static int verbose = 0;
#define info(fmt, ...) \
    do { if (verbose) printf(fmt, __VA_ARGS__); } while (0)
#define quit(fmt, ...) \
    do { fprintf(stderr, fmt, __VA_ARGS__); exit(EXIT_FAILURE); } while (0)

/* Hash table of connected clients. fd as key and struct client data as value */
static GHashTable *clients;
struct client_data {
    int fd;
    char *address;
};

/* on_new_client(): Accept and setup the new connection from a client.
 *
 * Parameters:
 *     sock_fd: File descriptor of the listening socket.
 *     epoll_fd: epoll()'s file descriptor to use to monitor the new client.
 */
static void on_new_client(int sock_fd, int epoll_fd) {
    struct sockaddr *client_addr = malloc(sizeof(struct sockaddr));
    socklen_t addrlen = sizeof(struct sockaddr);

    int clientfd = accept(sock_fd, client_addr, &addrlen);
    if (clientfd == -1) {
        fprintf(stderr, "accept(): %s\n", strerror(errno));
        return;
    }
    int flags = fcntl(clientfd, F_GETFL, 0);
    if (flags == -1) {
        quit("fcntl(): %s\n", strerror(errno));
    }
    if (fcntl(clientfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        quit("fcntl(): %s\n", strerror(errno));
    }

    struct sockaddr_in *addr_in = (struct sockaddr_in *) client_addr;
    char *address = g_strdup_printf("%s:%d",
                                    inet_ntoa(addr_in->sin_addr),
                                    ntohs(addr_in->sin_port));
    free(client_addr);

    info("New connection from %s\n", address);

    struct client_data *data = malloc(sizeof(struct client_data));
    data->fd = clientfd;
    data->address = address;

    g_hash_table_insert(clients, &data->fd, data);

    struct epoll_event e = {
        .events = EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLET,
        .data.ptr = data
    };
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, clientfd, &e) == -1) {
        quit("epoll_ctl(): %s\n", strerror(errno));
    }
}

/* on_client_destroy(): Cleanup resources of a disconnected client.
 *
 * Parameters:
 *     epoll_fd: epoll()'s file descriptor monitoring the client.
 *     data: Data associated to the client.
 */
static void on_client_destroy(int epoll_fd, struct client_data *data) {
    info("%s disconnected\n", data->address);

    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, data->fd, NULL) == -1) {
        quit("epoll_ctl(): %s\n", strerror(errno));
    }

    close(data->fd);

    g_hash_table_remove(clients, &data->fd);
    free(data->address);
    free(data);
}

/* on_client_ready(): Read and broadcast data from a client.
 *
 * Parameters:
 *     data: Data associated to the client.
 */
static void on_client_ready(struct client_data *data) {
    GHashTableIter iter;
    int count;
    char buf[MSG_LEN_MAX + 1];

    while ((count = read(data->fd, buf, MSG_LEN_MAX)) > 0) {
        buf[count] = 0;
        info("Read from %s: %s\n", data->address, buf);

        void *key, *value;
        g_hash_table_iter_init (&iter, clients);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
            int fd = *((int*)key);
            struct client_data *cdata = (struct client_data*)value;

            if (
                /* No client data: it's the listening socket fd.
                 * Skip it as it's not a client. */
                cdata == NULL
                /* It's the fd we've just read data from. Skip it. */
                || cdata->fd == data->fd) {
                continue;
            }

            info("Writing to %s\n", cdata->address);
            if (write(fd, buf, strlen(buf)) == -1) {
                fprintf(stderr, "write(): %s\n", strerror(errno));
            }
        }
    }
    if (count == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        fprintf(stderr, "read(): %s\n", strerror(errno));
    }
}

static int opt_index = 0;
static struct option long_opts[] =
{
    {"verbose", no_argument, 0, 'v'},
    {"port",  required_argument, 0, 'p'},
    {"help",  no_argument, 0, 'h'},
    {0, 0, 0, 0}
};

void handler() {
    exit(EXIT_SUCCESS);
}

int main(int argc, char *const *argv) {
    signal(SIGINT, handler);

    int c, port = 10000;
    while ((c = getopt_long(argc, argv, "vp:h", long_opts, &opt_index)) != -1) {
        switch (c) {
            case 'p':
                port = atoi(optarg);
                break;
            case 'h':
                printf(
                    "Usage %s [OPTIONS]\n\n"
                    "Options\n"
                    "\t-p, --port [PORT]  Listen on port PORT (default 10000)\n"
                    "\t-v, --verbose      Print verbose info\n"
                    "\t-h, --help         Print this help\n",
                    argv[0]
                );
                exit(EXIT_SUCCESS);
            case 'v':
                verbose = 1;
                break;
            default:
                exit(EXIT_FAILURE);
        }
    }
    if (optind < argc) {
        quit("Too many arguments. See %s --help for usage.\n", argv[0]);
    }

    printf("Listening on port %d...\n", port);

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        quit("socket(): %s\n", strerror(errno));
    }

    struct sockaddr_in sin = {
        .sin_family = AF_INET,
        .sin_port = htons(port)
    };
    if (bind(sock_fd, (struct sockaddr*) &sin, sizeof(sin)) == -1) {
        quit("bind(): %s\n", strerror(errno));
    }

    if (listen(sock_fd, SOMAXCONN) == -1) {
        quit("listen(): %s\n", strerror(errno));
    }

    /* No flags since the server is not multithreaded */
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        quit("epoll_create1(): %s\n", strerror(errno));
    }

    struct epoll_event sockev = {
        .events = EPOLLIN,
        .data.fd = sock_fd
    };
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &sockev) == -1) {
        quit("epoll_ctl(): %s\n", strerror(errno));
    }

    struct epoll_event *events = calloc(EPOLL_MAX_EVENTS, sizeof(struct epoll_event));

    clients = g_hash_table_new(g_int_hash, g_int_equal);
    while(1) {
        int events_cnt = epoll_wait(epoll_fd, events, EPOLL_MAX_EVENTS, -1);
        if (events_cnt == -1) {
            quit("epoll_pwait(): %s\n", strerror(errno));
        }

        for(int i = 0; i < events_cnt; i++) {
            struct epoll_event *ev = &events[i];

            /* Event on the listening socket (ie. a client has connected) */
            if (ev->data.fd == sock_fd) {
                on_new_client(sock_fd, epoll_fd);
            /* Event from clients */
            } else {
                struct client_data *data = ev->data.ptr;

                /* New data */
                if (ev->events & EPOLLIN) {
                    on_client_ready(data);
                }
                /* Client disconnected */
                if (ev->events & EPOLLRDHUP || ev->events & EPOLLERR || ev->events & EPOLLHUP) {
                    on_client_destroy(epoll_fd, data);
                }
            }
        }
    }
}
