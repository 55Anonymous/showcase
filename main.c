#include "a5.h"

static int get_port();

static int thread_creation_allowed = 1;
static int thread_exit_allowed = 1;
static int n_threads = 0;
static int max_threads = 0;

static pthread_t accepting_thread;

struct t_info {
    void * arg;
    void *(*f)(void *);
};

static void * thread_wrapper(void * arg) {
    struct t_info * t = (struct t_info*) arg;

    void * ret = t->f(t->arg);

    if (!thread_exit_allowed) {
        printf("Thread stopped running\n");
        exit(1);
    }

    return ret;
}

int a5_pthread_create(pthread_t *restrict thread, const pthread_attr_t *restrict attr, void *(*start_routine)(void *), void *restrict arg) {
    if (!thread_creation_allowed) {
        printf("Thread creation not allowed\n");
        exit(1);
    }

#if T910==1
    n_threads += 1;
    if (n_threads > max_threads) {
        printf("Only allowed to create %d threads\n", max_threads);
        exit(1);
    }
#endif

    struct t_info * t = malloc(sizeof(struct t_info));
    t->arg = arg;
    t->f = start_routine;

    return pthread_create(thread, attr, thread_wrapper, t);
}

int a5_accept(int socket, struct sockaddr * address, socklen_t * address_len) {
#if T910==1
    if (pthread_self() != accepting_thread) {
        printf("Only the main thread can call accept for A5\n");
        exit(1);
    }
#endif
    return accept(socket, address, address_len);
}

int create_server_socket(int port, int nthreads);

void accept_client(int server_socket);

int send_fully(int sockfd, const char * data, int size, int opts) {
    int sent = 0;
    while (sent != size) {
        sent += send(sockfd, data+sent, size-sent, 0);
    }

    assert(sent == size);

    return sent;
}

int recv_http_request(int sockfd, char * request, int max_size, int opts) {
    assert(max_size >= 2048);

    int ret = 0;

    // Read at least 4 characters, to see if it's a POST or GET request
    while (ret < 4) {
        int read = recv(sockfd, request+ret, max_size-ret, 0);

        // Client disconnected
        if (read == 0) {
            return ret;
        }

        ret += read;
    }

    char * end;
    int adjust;

    if (!strncmp("POST", request, 4)) {
        // POST requests must end in \r\r\r\r for A5 CS361 SP23
        // This is made-up to make A5 a bit easier
        end = "\r\r\r\r";
        adjust = 4;
    } else if (!strncmp("GET", request, 3)) {
        // All other requests must end in \r\n\r\n
        // Same as A4
        end = "\r\n\r\n";
        adjust = 0;
    } else {
        assert(0);
    }

    while (strncmp(request+(ret-4), end, 4)) {
        // We did not read enough data because we couldn't find the terminus
        // Read more data
        ret += recv(sockfd, request+ret, max_size-ret, 0);
    }

    // Do not count \r\r\r\r
    ret -= adjust;

    return ret;

}



int main(int argc, char** argv) {
    int port = get_port();

    if (argc < 2) {
        printf("Please provide the number of threads to use\n");
#if T910==1
        printf("Test 9 and 10 enabled\n");
#endif
        exit(1);
    }

    accepting_thread = pthread_self();

    int n_threads = atoi(argv[1]);

    printf("Using port %d\n", port);
    printf("PID: %d\n", getpid());
    printf("Using %d threads\n", n_threads);

#if T910==1
    max_threads = n_threads;
    thread_creation_allowed = 1;
    thread_exit_allowed = 1;
#endif

    // Make server available on port
    int server_socket = create_server_socket(port, n_threads);

#if T910==1
    thread_creation_allowed = 0;
    thread_exit_allowed = 0;
#endif

    // Process client requests
    while (1) {
        accept_client(server_socket);
    }

    return 0;
}

static int get_port() {
    int fd = open("port.txt", O_RDONLY);
    if (fd < 0) {
        perror("Could not open port");
        exit(1);
    }

    char buffer[32];
    int r = read(fd, buffer, sizeof(buffer));
    if (r < 0) {
        perror("Could not read port");
        exit(1);
    }

    return atoi(buffer);
}

