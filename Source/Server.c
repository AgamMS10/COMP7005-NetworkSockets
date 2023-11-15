#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>

static void setup_signal_handler(void);
static void sigint_handler(int signum);
static int socket_create(void);
static void socket_bind(int sockfd, int port, const char *ip_addr);
static void start_listening(int server_fd, int backlog);
static int socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len);
static void socket_close(int sockfd);
static void receive_and_store_files(int client_sockfd, const char *directory);
static int setup_poll(struct pollfd *fds, int server_fd);
static void handle_client_activity(struct pollfd *fds, int nfds, const char *directory);


static volatile sig_atomic_t exit_flag = 0;

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <IPv4Addr> <PortNumber> ./Directory/to/store/files\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *ip_addr = argv[1];
    char *port_str = argv[2];
    const char *directory = argv[3];

    unlink(port_str);


    int port = atoi(port_str); 
    int sockfd = socket_create();
    socket_bind(sockfd, port, ip_addr);
    start_listening(sockfd, SOMAXCONN);
    setup_signal_handler();

    struct pollfd *fds = (struct pollfd *)malloc(sizeof(struct pollfd) * 64);
    if (fds == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    fds[0].fd = sockfd;
    fds[0].events = POLLIN;

    int num_clients = 1;

    while (!exit_flag) {
        int num_events = poll(fds, num_clients, -1);

        if (num_events == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                perror("poll");
                exit(EXIT_FAILURE);
            }
        }

        if (fds[0].revents & POLLIN) {
            int client_sockfd;
            struct sockaddr_storage client_addr;
            socklen_t client_addr_len = sizeof(client_addr);

            client_sockfd = socket_accept_connection(sockfd, &client_addr, &client_addr_len);

            if (client_sockfd != -1) {
                if (num_clients >= 64) { 
                    fds = (struct pollfd *)realloc(fds, sizeof(struct pollfd) * (num_clients * 2)); 
                    if (fds == NULL) {
                        perror("realloc");
                        exit(EXIT_FAILURE);
                    }
                }

                fds[num_clients].fd = client_sockfd;
                fds[num_clients].events = POLLIN;
                num_clients++;
                printf("New client connected: %d\n", client_sockfd);
            }
        }


        handle_client_activity(fds, num_clients, directory);
    }


    for (int i = 1; i < num_clients; i++) {
        close(fds[i].fd);
    }

    socket_close(sockfd);
    unlink(port_str);
    
    free(fds); 
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
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Socket created successfully: %d\n", sockfd);

    return sockfd;
}

static void socket_bind(int sockfd, int port, const char *ip_addr) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip_addr, &addr.sin_addr) <= 0) {
    perror("inet_pton");
    exit(EXIT_FAILURE);
    }

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    printf("Bound to IP: %s, Port: %d\n", ip_addr, port);
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
        extension = strtok(NULL, ".") ? : ""; // Handle files without an extension

        int file_suffix = 0;
        int need_suffix = 0; // Flag to check if suffix is needed

        // Check if the filename already exists to determine if a suffix is needed
        snprintf(file_path, sizeof(file_path), "%s/%s.%s", directory, name, extension);
        if (access(file_path, F_OK) == 0) { // File exists
            need_suffix = 1; // Set flag to start suffixing
        }

        while (need_suffix) {
            // Check if the combined name, suffix, and directory will exceed file_path's size
            int written = snprintf(file_path, sizeof(file_path), "%s/%s#%d.%s", directory, name, file_suffix, extension);
            if (written >= sizeof(file_path)) {
                fprintf(stderr, "Error: filename with directory and suffix is too long\n");
                return; // Error: Path is too long after adding suffix
            }

            if (access(file_path, F_OK) != 0) {
                break; // File doesn't exist, we can create it with this name
            }

            file_suffix++; // Increment suffix and check again
        }

        uint32_t file_size;
        recv(client_sockfd, &file_size, sizeof(uint32_t), 0);
        file_size = ntohl(file_size); // Convert to host byte order

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
            printf("Received and stored file with suffix: %s (duplicate #%d)\n", file_path, file_suffix);
        } else {
            printf("Received and stored file: %s\n", file_path);
        }
    }
}


static int setup_poll(struct pollfd *fds, int server_fd) {

    fds[0].fd = server_fd;
    fds[0].events = POLLIN;

    return 1; 
}

static void handle_client_activity(struct pollfd *fds, int num_clients, const char *directory) {
    for (int i = 1; i < num_clients; i++) {
        if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
            if (fds[i].revents & POLLIN) {
                receive_and_store_files(fds[i].fd, directory);
            }

            printf("Client disconnected: %d\n", fds[i].fd);
            close(fds[i].fd);
            for (int j = i; j < num_clients - 1; j++) {
                fds[j] = fds[j + 1];
            }
            num_clients--;

            i--;

            break;
        }
    }
}
