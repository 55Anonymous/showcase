#include "a5-pthread.h"
#include "a5.h"

static const char ping_request[] = "GET /ping HTTP/1.1\r\n\r\n";
static const char echo_request[] = "GET /echo HTTP/1.1\r\n";
static const char write_request[] = "POST /write HTTP/1.1\r\n";
static const char read_request[] = "GET /read HTTP/1.1\r\n";
static const char file_request[] = "GET /%s HTTP/1.1\r\n";
static const char stats_request[] = "GET /stats HTTP/1.1\r\n";

static const char stats_response_body[] = "Requests: %d\nHeader bytes: %d\nBody bytes: %d\nErrors: %d\nError bytes: %d";

static const char ok200_response[]  = "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n";
static const char err404_response[] = "HTTP/1.1 404 Not Found";
static const char err400_response[] = "HTTP/1.1 400 Bad Request";

static const char content_len_header[] = "Content-Length: %d";

static char written[1024] = "<empty>";
static int written_size = 7;

static const char ping_header[] = "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\n";
static const char ping_body[] = "pong";

static int reqs = 0;
static int head_bytes = 0;
static int body_bytes = 0;
static int errs = 0;
static int err_bytes = 0;
pthread_mutex_t write_lock;
pthread_mutex_t stats_lock;

static int * clients;
static pthread_t * ts;
static int front = 0, rear = 0, n_threads = 0;
static pthread_mutex_t mutex;
static sem_t slots, items;

static void * consumer(void * arg);

static int prepare_socket(int port) {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Error creating socket");
        exit(1);
    }

    static struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &(server.sin_addr));

    int optval = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (bind(server_socket, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Error on bind");
        exit(1);
    }

    if (listen(server_socket, 10) < 0) {
        perror("Error on listen");
        exit(1);
    }

    return server_socket;
}

int create_server_socket(int port, int threads) {
    int server_socket = prepare_socket(port);

    if (pthread_mutex_init(&write_lock, NULL) != 0) {
        perror("Error creating mutex");
        exit(1);
    }

    if (pthread_mutex_init(&stats_lock, NULL) != 0) {
        perror("Error creating mutex");
        exit(1);
    }

    clients = malloc(threads*sizeof(int));
    ts = malloc(threads*sizeof(pthread_t));
    n_threads = threads;
    front = 0;
    rear = 0;

    if (pthread_mutex_init(&mutex, NULL) != 0) {
        perror("Error creating mutex");
        exit(1);
    }

    if (sem_init(&slots, 0, n_threads) != 0) {
        perror("Error creating semaphore");
        exit(1);
    }

    if (sem_init(&items, 0, 0) != 0) {
        perror("Error creating semaphore");
        exit(1);
    }   

    for (int i = 0 ; i < n_threads ; i++) {
        int ret = pthread_create(&ts[i], NULL, consumer, NULL);
        if (ret != 0) {
            perror("Thread create");
            exit(1);
        }
    }

    return server_socket;
}

static void send_response(int sockfd, char * head, char * body, int head_size, int body_size) {
    int head_sent = send_fully(sockfd, head, head_size, 0);
    int body_sent = 0;

    while (body_sent != body_size) {
        body_sent += send_fully(sockfd, body+body_sent, body_size-body_sent, 0);
    }
    
    assert(head_sent == head_size);
    assert(body_sent == body_size);

    pthread_mutex_lock(&stats_lock);

    reqs += 1;
    head_bytes += head_size;
    body_bytes += body_size;

    pthread_mutex_unlock(&stats_lock);
}

static void send_error(int sockfd, const char * error) {
    int len = strlen(error);
    int sent = send(sockfd, error, len, 0);
    
    assert(sent == len);

    pthread_mutex_lock(&stats_lock);

    errs += 1;
    err_bytes += len;

    pthread_mutex_unlock(&stats_lock);
}

static void handle_ping(int sockfd, char * request) {
    int head_size = strlen(ping_header);
    char head[1024];
    memcpy(head, ping_header, head_size);

    int body_size = strlen(ping_body);
    char body[1024];
    memcpy(body, ping_body, body_size);

    send_response(sockfd, head, body, head_size, body_size);
}

static void handle_echo(int sockfd, char * request) {
    char body[1024];
    char head[1024];

    char * end = strstr(request, "\r\n\r\n");
    if (end == NULL) {
        end = request + 1024;
    }

    *end = '\0';

    char * start = strstr(request, "\r\n");
    assert(start != NULL);

    start += 2;

    int body_size = strlen(start);
    memcpy(body, start, body_size);

    int head_size = snprintf(head, sizeof(head), ok200_response, body_size);

    send_response(sockfd, head, body, head_size, body_size);
}

static void handle_read(int sockfd, char * request) {
    char body[1024];
    char head[1024];

    pthread_mutex_lock(&write_lock);

    int body_size = written_size;
    memcpy(body, written, body_size);

    pthread_mutex_unlock(&write_lock);

    int head_size = snprintf(head, sizeof(head), ok200_response, body_size);

    send_response(sockfd, head, body, head_size, body_size);
}


static void handle_write(int sockfd, char * request) {
    char * start = strstr(request, "\r\n\r\n");
    assert(start != NULL);
    start += 4; // Jump over new line characters

    char * ptr;
    char * tok = strtok_r(request, "\r\n", &ptr);

    assert(tok != NULL);

    // Skip first line
    tok = strtok_r(NULL, "\r\n", &ptr);

    int length = 0;
    while (tok != NULL) {
        if (sscanf(tok, content_len_header, &length) != 0) {
            // Found the content-length header
            break;
        }
        tok = strtok_r(NULL, "\r\n", &ptr);
    }
    
    assert (length != 0);

    if (length > 1024)
        length = 1024;

    pthread_mutex_lock(&write_lock);

    // Saves posted data
    written_size = length;
    memcpy(written, start, written_size);
    
    pthread_mutex_unlock(&write_lock);

    handle_read(sockfd, request);
}

static void handle_file(int sockfd, char *request) {
    char body[1024];
    char head[1024];
    static char path[128];
    int found = sscanf(request, file_request, path);
    assert(found > 0);

    int fd = open(path, O_RDONLY);

    if(fd < 0) {
        // File does not exist
        send_error(sockfd, err404_response);
        close(sockfd);
        return;
    }

    struct stat s;
    fstat(fd, &s);

    int file_size = s.st_size;

    int head_size = snprintf(head, sizeof(head), ok200_response, file_size);
    int sent = send_fully(sockfd, head, head_size, 0);

    assert(sent == head_size); // Sent header in full

    pthread_mutex_lock(&stats_lock);

    head_bytes += head_size;

    pthread_mutex_unlock(&stats_lock);

    int file_read = 0;
    int file_sent = 0;
    int total_sent = 0;

    while (total_sent < file_size) {
        file_read = read(fd, body, sizeof(body));
        file_sent = 0;
        file_sent = send_fully(sockfd, body, file_read, 0);

        while (file_sent != file_read) {
            file_sent += send_fully(sockfd, body+file_sent, file_read-file_sent, 0);
        }

        assert(file_sent == file_read); // We were able to send all data

        pthread_mutex_lock(&stats_lock);

        total_sent += file_sent;
        body_bytes += file_sent;

        pthread_mutex_unlock(&stats_lock);
    }

    pthread_mutex_lock(&stats_lock);
    reqs += 1;
    pthread_mutex_unlock(&stats_lock);

    close(fd);
    close(sockfd);
}

static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

static void handle_stats(int sockfd) {
    char body[1024];
    char head[1024];

    pthread_mutex_lock(&stats_lock);

    int body_size = snprintf(body, sizeof(body), stats_response_body,
        reqs, head_bytes, body_bytes, errs, err_bytes);

    pthread_mutex_unlock(&stats_lock);

    int head_size = snprintf(head, sizeof(head), ok200_response, body_size);

    send_response(sockfd, head, body, head_size, body_size);
}

static void * handle_client_request(int sockfd) {
    //int sockfd = (int) (long) arg;
    char request[2048];
    int len = recv_http_request(sockfd, request, sizeof(request), 0);

    if (len == 0) {
        // No request
        return NULL;
    }

    assert(len > 0);

    if (!strncmp(request, ping_request, strlen(ping_request))) {
        handle_ping(sockfd, request);
        close(sockfd);
        return NULL;
    } else if (!strncmp(request, echo_request, strlen(echo_request))) {
        handle_echo(sockfd, request);
        close(sockfd);
        return NULL;
    } else if (!strncmp(request, write_request, strlen(write_request))) {
        handle_write(sockfd, request);
        close(sockfd);
        return NULL;
    } else if (!strncmp(request, read_request, strlen(read_request))) {
        handle_read(sockfd, request);
        close(sockfd);
        return NULL;
    } else if (!strncmp(request, stats_request, strlen(stats_request))) {
        handle_stats(sockfd);
        close(sockfd);
        return NULL;
    } else if (!strncmp(request, "GET ", 4)) {
        handle_file(sockfd, request);
        return NULL;
    }

    send_error(sockfd, err400_response);
    close(sockfd);
    return NULL;
}

static void * consumer(void * arg) {
    while (1) {
        sem_wait(&items);

        pthread_mutex_lock(&mutex);

        int sockfd = clients[front];

        front = (front + 1) % n_threads;

        pthread_mutex_unlock(&mutex);

        sem_post(&slots);

        handle_client_request(sockfd);
    }
    assert(0);
}

void accept_client(int server_socket) {
    static struct sockaddr_in client;
    static socklen_t client_size;

    memset(&client, 0, sizeof(client));
    memset(&client_size, 0, sizeof(client_size));

    int client_socket = accept(server_socket, (struct sockaddr *)&client, &client_size);
    if (client_socket < 0)
    {
        perror("Error on accept");
        exit(1);
    }

    sem_wait(&slots);

    pthread_mutex_lock(&mutex);

    clients[rear] = client_socket;

    rear = (rear + 1) % n_threads;

    pthread_mutex_unlock(&mutex);

    sem_post(&items);

    // pthread_t thread;

    // pthread_create(&thread, NULL, handle_client_request, ((void*) ((long) client_socket)));

}
