#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>



#define SOCKET_PATH "/tmp/ServerSocket"

static int socket_create(void);
static int connect_to_server(const char *path);
static void socket_close(int sockfd);
static void read_file(const char *file_path, char **file_data, size_t *file_size);
static void send_file_data(int server_fd, const char *filename, const char *file_data, size_t file_size);
static char *parse_filename(const char *file_path);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Please specifiy the files to send to server");
        fprintf(stderr, "Usage: %s <file1> [<file2> ...]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sockfd = connect_to_server(SOCKET_PATH);

    for (int i = 1; i < argc; i++) {
        char *file_data;
        size_t file_size;
        read_file(argv[i], &file_data, &file_size);

        char *filename = parse_filename(argv[i]);
        send_file_data(sockfd, filename, file_data, file_size);

        free(file_data);
        free(filename);
    }

    socket_close(sockfd);

    return EXIT_SUCCESS;
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

static void send_file_data(int server_fd, const char *filename, const char *file_data, size_t file_size) {
    uint8_t filename_size = strlen(filename);

    if (send(server_fd, &filename_size, sizeof(uint8_t), 0) == -1 ||
        send(server_fd, filename, filename_size, 0) == -1 ||
        send(server_fd, &file_size, sizeof(uint32_t), 0) == -1) {
        perror("Error sending file metadata");
        return;
    }

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
        //Add Usleep for testing purposes
        //usleep(100000);
    }

    printf("Sent file: %s\n", filename);
}

static char *parse_filename(const char *file_path) {
    const char *filename = file_path + strlen(file_path);
    
    while (filename > file_path && (*(filename - 1) != '/')) {
        filename--;
    }
    
    return strdup(filename);
}

static int socket_create(void) {
    int sockfd;

#ifdef SOCK_CLOEXEC
    sockfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd != -1 ) {
        printf("Socket created sucessfully:%d\n", sockfd);
    }
#endif

    if (sockfd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

static int connect_to_server(const char *path) {
    int sockfd = socket_create();
    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, path, sizeof(server_addr.sun_path) - 1);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Connect failed Make sure Server is running and Socket address is correct");
        exit(EXIT_FAILURE);
    }
    else {
        printf("Connected to server %d\n", sockfd);
    }

    return sockfd;
}

static void socket_close(int sockfd) {
    if (close(sockfd) == -1) {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}
