#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define MAX_IP_LENGTH 46
#define MAX_PORT_LENGTH 6


static int socket_create(int family);
static int connect_to_server(const char *ip, int port);
static void socket_close(int sockfd);
static void read_file(const char *file_path, char **file_data, size_t *file_size);
static void send_file_metadata(int server_fd, const char *filename, size_t file_size);
static void send_file_content(int server_fd, const char *file_data, size_t file_size);
static char *parse_filename(const char *file_path);
static int determine_address_family(const char *ip);

int main(int argc, char *argv[]) {
    printf("Starting Client\n");
    if (argc < 4) {
        printf("Usage: %s <IPv4/IPv6> <port> <file1> [<file2> ...]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);
    printf("Going to connect_to_server\n");
    int sockfd = connect_to_server(server_ip, server_port);

    for (int i = 3; i < argc; i++) {
        char *file_data;
        size_t file_size;
        read_file(argv[i], &file_data, &file_size);

        char *filename = parse_filename(argv[i]);
        send_file_metadata(sockfd, filename, file_size);
        send_file_content(sockfd, file_data, file_size);

        free(file_data);
        free(filename);
    }

    socket_close(sockfd);
    return EXIT_SUCCESS;
}

static int socket_create(int family) {
    printf("In Create Socket\n");
    int sockfd = socket(family, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    else{
        printf("Socket Created \n");
    }
    return sockfd;
}

static void read_file(const char *file_path, char **file_data, size_t *file_size) {
    FILE *file = fopen(file_path, "rb");
    if (file == NULL) {
        perror("Error opening file for reading");
        *file_data = NULL;
        *file_size = 0;
        return;
    }

    fseek(file, 0, SEEK_END);
    *file_size = ftell(file);
    rewind(file);

    *file_data = (char *)malloc(*file_size);
    if (*file_data == NULL) {
        perror("Memory allocation error");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    if (fread(*file_data, 1, *file_size, file) != *file_size) {
        perror("Error reading file data");
        free(*file_data);
        *file_data = NULL;
        *file_size = 0;
    }

    fclose(file);
}

static void send_file_metadata(int server_fd, const char *filename, size_t file_size) {
    uint8_t filename_size = strlen(filename);

    if (send(server_fd, &filename_size, sizeof(uint8_t), 0) == -1 ||
        send(server_fd, filename, filename_size, 0) == -1 ||
        send(server_fd, &file_size, sizeof(uint32_t), 0) == -1) {
        perror("Error sending file metadata");
    }
}

static void send_file_content(int server_fd, const char *file_data, size_t file_size) {
    size_t remaining = file_size;
    const char *data_ptr = file_data;

    while (remaining > 0) {
        size_t to_send = remaining < 1024 ? remaining : 1024;

        ssize_t bytes_sent = send(server_fd, data_ptr, to_send, 0);

        if (bytes_sent <= 0) {
            perror("Error sending file data");
            break;
        }

        remaining -= bytes_sent;
        data_ptr += bytes_sent;
        // Add Usleep for testing purposes
        // usleep(100000);
    }

    printf("Sent file content.\n");
}

static char *parse_filename(const char *file_path) {
    const char *filename = file_path + strlen(file_path);
    
    while (filename > file_path && (*(filename - 1) != '/')) {
        filename--;
    }

    size_t len = strlen(filename);
    char *result = (char *)malloc(len + 1);
    if (result != NULL) {
        memcpy(result, filename, len);
        result[len] = '\0';
    }
    return result;
}

static int connect_to_server(const char *ip, int port) {
    printf("In connect_to_server\n");
    printf("Going to determine_address_family\n");
    int family = determine_address_family(ip);
    if (family == -1) {
        fprintf(stderr, "Invalid IP address format.\n");
        exit(EXIT_FAILURE);
    }
    printf("Going to socket_create\n");
    int sockfd = socket_create(family);
    printf("Back in connect_to_server\n");

    if (family == AF_INET) {
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        inet_pton(AF_INET, ip, &(server_addr.sin_addr));
        server_addr.sin_port = htons(port);

        if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
            perror("Connect failed. Make sure the server is running and the IP address and port are correct.");
            exit(EXIT_FAILURE);
        }
    } else {
        struct sockaddr_in6 server_addr6;
        memset(&server_addr6, 0, sizeof(server_addr6));
        server_addr6.sin6_family = AF_INET6;
        inet_pton(AF_INET6, ip, &(server_addr6.sin6_addr));
        server_addr6.sin6_port = htons(port);

        if (connect(sockfd, (struct sockaddr *)&server_addr6, sizeof(server_addr6)) == -1) {
            perror("Connect failed. Make sure the server is running and the IP address and port are correct.");
            exit(EXIT_FAILURE);
        }
    }
    
    printf("Connected to the server.\n");
    return sockfd;
}

static int determine_address_family(const char *ip) {
    struct in6_addr buf;
    if (inet_pton(AF_INET, ip, &buf)) {
        return AF_INET;
    } else if (inet_pton(AF_INET6, ip, &buf)) {
        return AF_INET6;
    } else {
        return -1; 
    }
}

static void socket_close(int sockfd) {
    if (close(sockfd) == -1) {
        perror("Error closing socket");
    }
}