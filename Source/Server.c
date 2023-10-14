#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void setup_signal_handler(void);
static void sigint_handler(int signum);
static int socket_create(void);
static void socket_bind(int sockfd, const char *path);
static void start_listening(int server_fd, int backlog);
static int socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len);
static void handle_file(int client_sockfd, const char *directory);
static void socket_close(int sockfd);
static void receive_and_store_files(int client_sockfd, const char *directory);

#define SOCKET_PATH "/tmp/ServerSocket"

static volatile sig_atomic_t exit_flag = 0;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Please Specify a directory to save files to \nUsage: %s <directory_path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *directory = argv[1];
    unlink(SOCKET_PATH);

    int sockfd = socket_create();
    socket_bind(sockfd, SOCKET_PATH);
    start_listening(sockfd, SOMAXCONN);
    setup_signal_handler();

    while (!exit_flag) {
        int client_sockfd;
        struct sockaddr_storage client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        client_sockfd = socket_accept_connection(sockfd, &client_addr, &client_addr_len);

        if (client_sockfd == -1) {
            if (exit_flag) {
                break;
            }
            continue;
        }

        receive_and_store_files(client_sockfd, directory);
        socket_close(client_sockfd);
    }

    socket_close(sockfd);
    unlink(SOCKET_PATH);

    return EXIT_SUCCESS;
}

static void setup_signal_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = sigint_handler;

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

static void sigint_handler(int signum) {
    exit_flag = 1;
    printf("\nServer closing\n");
    fflush(stdout);
}

static int socket_create(void) {
    int sockfd;

#ifdef SOCK_CLOEXEC
    sockfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
#endif
    if (sockfd != -1 ) {
        printf("Socket created sucessfully: %d\n", sockfd);
    }
    if (sockfd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

static void socket_bind(int sockfd, const char *path) {
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));

    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    printf("Bound to domain socket: %s\n", path);
}

static void start_listening(int server_fd, int backlog) {
    if (listen(server_fd, backlog) == -1) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Listening for incoming connections...\n");
}

static int socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len) {
    int client_fd;
    errno = 0;
    client_fd = accept(server_fd, (struct sockaddr *)client_addr, client_addr_len);

    if (client_fd == -1) {
        if (errno != EINTR) {
            perror("accept failed");
        }

        return -1;
    }
    else {
        printf("Connection Accepted %d\n", client_fd);
    }

    return client_fd;
}

static void socket_close(int sockfd) {
    if (close(sockfd) == -1) {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}

static void receive_and_store_files(int client_sockfd, const char *directory) {
    uint8_t filename_size;
    while (recv(client_sockfd, &filename_size, sizeof(uint8_t), 0) > 0) {
        char filename[256];
        char file_path[512];
        recv(client_sockfd, filename, filename_size, 0);
        filename[filename_size] = '\0';

        
        char *name, *extension;
        name = strtok(filename, ".");
        extension = strtok(NULL, ".");

        // Check for duplicate files and append
        int file_suffix = 0;
        while (1) {
            
            if (file_suffix > 0) {
                snprintf(file_path, sizeof(file_path), "%s/%s#%d.%s", directory, name, file_suffix, extension);
            } else {
                snprintf(file_path, sizeof(file_path), "%s/%s.%s", directory, name, extension);
            }

            if (access(file_path, F_OK) != 0) {
                break;
            }

            file_suffix++;
        }

        uint32_t file_size;
        recv(client_sockfd, &file_size, sizeof(uint32_t), 0);

        FILE *file = fopen(file_path, "wb");
        if (file == NULL) {
            perror("Error opening file for writing");
            return;
        }

        char buffer[1024];
        size_t remaining = file_size;
        while (remaining > 0) {
            size_t to_read = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            ssize_t bytes_received = recv(client_sockfd, buffer, to_read, 0);
            if (bytes_received <= 0) {
                perror("Error receiving file data");
                fclose(file);
                return;
            }
            fwrite(buffer, 1, bytes_received, file);
            remaining -= bytes_received;
        }

        fclose(file);
        if (file_suffix > 0) {
            printf("Received file: %s (duplicate #%d)\n", file_path, file_suffix);
        } else {
            printf("Received file: %s\n", file_path);
        }
    }
}
