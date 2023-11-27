#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/select.h>
#include <fcntl.h>

#define SERVER_DATA_PORT 5000
#define SERVER_PORT 21
#define BUF_SIZE 1024
#define MAX_USERS 10

typedef struct {
    char username[BUF_SIZE];
    char password[BUF_SIZE];
} User;

User users[MAX_USERS];
int user_count = 0;
int authenticated_sockets[FD_SETSIZE];

// Function Declarations
int parse_config_file();
int authenticate_user(const char *username, const char *password);
void handle_user_command(int client_sock, fd_set *master_set, int *max_fd);
void execute_command(int client_sock, const char *command, const char *argument);
void send_file(const char *filename, int sockfd);
void receive_file(const char *filename, int sockfd);

/**
 * Parses the configuration file to load usernames and passwords.
 *
 * @return int Returns 0 on successful parsing, 1 on file open error.
 */
int parse_config_file() {
    FILE *file = fopen("users.txt", "r");
    if (!file) {
        perror("Error opening file");
        return 1; // File open error
    }

    char line[BUF_SIZE];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = 0; // Remove newline character

        // Tokenize the line to get username and password
        char *token = strtok(line, " ");
        if (token != NULL && user_count < MAX_USERS) {
            strncpy(users[user_count].username, token, BUF_SIZE - 1);
            token = strtok(NULL, " ");
            if (token != NULL) {
                strncpy(users[user_count].password, token, BUF_SIZE - 1);
                user_count++; // Increment user count
            }
        }
    }
    fclose(file);
    return 0; // Successful parsing
}

/**
 * Authenticates a user based on username and password.
 *
 * @param username The username to authenticate.
 * @param password The password to authenticate.
 * @return int Returns 1 if authentication is successful, 0 otherwise.
 */
int authenticate_user(const char *username, const char *password) {
    for (int i = 0; i < user_count; i++) {
        // Check if username and password match
        if (strcmp(users[i].username, username) == 0 && 
            strcmp(users[i].password, password) == 0) {
            return 1; // Authentication successful
        }
    }
    return 0; // Authentication failed
}

/**
 * Handles commands received from clients.
 *
 * @param client_sock The socket file descriptor for the client connection.
 * @param master_set Pointer to the master file descriptor set.
 * @param max_fd Pointer to the maximum file descriptor value.
 */
void handle_user_command(int client_sock, fd_set *master_set, int *max_fd) {
    char buffer[BUF_SIZE];
    ssize_t bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);

    // Check for client disconnection or reception error
    if (bytes_received <= 0) {
        close(client_sock); // Close the client socket
        FD_CLR(client_sock, master_set); // Remove from the master set
        return;
    }

    buffer[bytes_received] = '\0'; // Ensure the received data is null-terminated

    // Parse the command and its arguments
    char *command = strtok(buffer, " ");
    char *argument = strtok(NULL, "\n");

    // Execute the command if it is valid
    if (command != NULL) {
        execute_command(client_sock, command, argument);
    }
}

/**
 * Executes specific FTP commands received from the client.
 *
 * @param client_sock The socket file descriptor for the client connection.
 * @param command The FTP command to execute.
 * @param argument The argument for the FTP command.
 */
void execute_command(int client_sock, const char *command, const char *argument) {
    if (strcmp(command, "RETR") == 0) {
        send_file(argument, client_sock); // Handle file sending to the client
    } else if (strcmp(command, "STOR") == 0) {
        receive_file(argument, client_sock); // Handle file receiving from the client
    }
    // Additional FTP commands can be handled here
}

/**
 * Sends a file to the client over the socket.
 *
 * @param filename The name of the file to be sent.
 * @param sockfd The socket file descriptor for sending the file.
 */
void send_file(const char *filename, int sockfd) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Error opening file");
        send(sockfd, "550 File not found.\r\n", 20, 0);
        return;
    }

    char buffer[BUF_SIZE];
    size_t bytes_read;

    // Read from the file and send data in chunks
    while ((bytes_read = fread(buffer, 1, BUF_SIZE, file)) > 0) {
        if (send(sockfd, buffer, bytes_read, 0) < 0) {
            perror("Error sending file");
            break;
        }
    }

    fclose(file); // Close the file after sending
    send(sockfd, "226 Transfer complete.\r\n", 24, 0); // Notify the client of completion
}

/**
 * Receives a file from a client over a socket and saves it to the server's filesystem.
 *
 * @param filename The name of the file to be saved on the server.
 * @param sockfd The socket file descriptor for communication with the client.
 */
void receive_file(const char *filename, int sockfd) {
    // Open the file for writing in binary mode
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        perror("Error opening file");
        // If unable to open the file, send an error message to the client
        send(sockfd, "550 Cannot create file.\r\n", 24, 0);
        return;
    }

    char buffer[BUF_SIZE];
    ssize_t bytes_received;

    // Receive data from the client until the entire file is received
    while ((bytes_received = recv(sockfd, buffer, BUF_SIZE, 0)) > 0) {
        // Write the received bytes to the file
        size_t bytes_written = fwrite(buffer, 1, bytes_received, file);
        if (bytes_written < bytes_received) {
            perror("Error writing to file");
            break; // Break the loop if there is a file writing error
        }
    }

    // Check for errors in receiving data
    if (bytes_received < 0) {
        perror("Error receiving file");
    }

    // Close the file after receiving all data
    fclose(file);

    // Optionally, send a completion message to the client
    send(sockfd, "226 Transfer complete.\r\n", 24, 0);
}



int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len;
    fd_set master_set, read_fds;
    int fd_max;

    // Load users from configuration file
    if (parse_config_file() != 0) {
        fprintf(stderr, "Failed to load user configurations.\n");
        exit(EXIT_FAILURE);
    }

    // Create a server socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Failed to create socket");
        exit(EXIT_FAILURE);
    }

    // Bind the socket to the specified port
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to bind socket");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_sock, MAX_USERS) < 0) {
        perror("Failed to listen on socket");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    // Initialize the master file descriptor set
    FD_ZERO(&master_set);
    FD_SET(server_sock, &master_set);
    fd_max = server_sock;

    printf("FTP Server started on port %d\n", SERVER_PORT);

    // Server main loop
    while (1) {
        read_fds = master_set;
        if (select(fd_max + 1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select error");
            continue;
        }

        // Iterate over file descriptors
        for (int i = 0; i <= fd_max; i++) {
            if (FD_ISSET(i, &read_fds)) {
                if (i == server_sock) {
                    // Accept new client connections
                    client_addr_len = sizeof(client_addr);
                    client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
                    if (client_sock < 0) {
                        perror("accept failed");
                        continue;
                    }

                    // Add new client to the master set
                    FD_SET(client_sock, &master_set);
                    if (client_sock > fd_max) {
                        fd_max = client_sock; // Update the max descriptor
                    }

                    printf("New connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                } else {
                    // Handle commands from connected clients
                    handle_user_command(i, &master_set, &fd_max);
                }
            }
        }
    }

    // Clean up
    close(server_sock);
    return 0;
}