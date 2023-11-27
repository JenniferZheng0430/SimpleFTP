#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <dirent.h>

#define SERVER_CONTROL_PORT 21 // FTP standard control port
#define BUF_SIZE 1024

// Forward declarations of functions
void handle_retr_command(int server_sd, const char *filename);
void receive_file(int sockfd, const char *filename);
void send_file(int sockfd, const char *filename);
void handle_stor_command(int server_sd, const char *filename);
void handle_list_command(int server_sd);
void execute_command(int server_sd, const char *command);
void print_local_directory();
void change_local_directory(const char *path);
void print_working_directory(int sockfd);

int main() {
    int server_sd;
    struct sockaddr_in server_addr;
    char command[BUF_SIZE];

    // Create and connect the socket
    server_sd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_CONTROL_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Replace with server IP

    if (connect(server_sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connect failed");
        close(server_sd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to FTP server.\n");

    // Main client loop
    while (1) {
        printf("ftp> ");
        fgets(command, BUF_SIZE, stdin);
        command[strcspn(command, "\n")] = 0; // Remove newline character

        if (strcmp(command, "QUIT") == 0) {
            printf("Exiting FTP client.\n");
            break;
        }

        execute_command(server_sd, command);
    }

    close(server_sd);
    return 0;
}

void execute_command(int server_sd, const char *command) {
    char buffer[BUF_SIZE];
    char *cmd, *arg;
    strcpy(buffer, command);

    cmd = strtok(buffer, " ");
    arg = strtok(NULL, " ");

    if (strcmp(cmd, "RETR") == 0 && arg) {
        handle_retr_command(server_sd, arg);
    } else if (strcmp(cmd, "STOR") == 0 && arg) {
        handle_stor_command(server_sd, arg);
    } else if (strcmp(cmd, "LIST") == 0) {
        handle_list_command(server_sd);
    } else if (strcmp(cmd, "!LIST") == 0) {
        print_local_directory();
    } else if (strcmp(cmd, "!CWD") == 0 && arg) {
        change_local_directory(arg);
    } else if (strcmp(cmd, "PWD") == 0) {
        print_working_directory(server_sd);
    } else {
        // Send other commands to server
        send(server_sd, command, strlen(command), 0);
        recv(server_sd, buffer, BUF_SIZE, 0);
        printf("%s\n", buffer);
    }
}

// Function definitions for handle_retr_command, handle_stor_command, etc.
// ... [Implement the details of these functions]


/**
 * Handles the RETR command to retrieve a file from the server.
 *
 * @param server_sd The socket file descriptor connected to the server.
 * @param filename The name of the file to retrieve from the server.
 */
void handle_retr_command(int server_sd, const char *filename) {
    char buffer[BUF_SIZE];

    // Send the RETR command to the server
    sprintf(buffer, "RETR %s", filename);
    if (send(server_sd, buffer, strlen(buffer), 0) < 0) {
        perror("Error sending RETR command");
        return;
    }

    // Wait for server's response to proceed
    if (recv(server_sd, buffer, BUF_SIZE, 0) < 0) {
        perror("Error receiving server response");
        return;
    }

    // Check server's response
    if (strncmp(buffer, "150", 3) == 0) {  // File transfer starts
        receive_file(server_sd, filename);
    } else {
        printf("Server response: %s\n", buffer);  // Handle other responses
    }
}

void receive_file(int sockfd, const char *filename) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("Error opening file");
        return;
    }

    char buffer[BUF_SIZE];
    ssize_t bytes_received;

    // Receive the file data
    while ((bytes_received = recv(sockfd, buffer, BUF_SIZE, 0)) > 0) {
        fwrite(buffer, 1, bytes_received, file);
    }

    if (bytes_received < 0) {
        perror("Error receiving file data");
    }

    fclose(file);
    printf("File %s received successfully.\n", filename);
}

/**
 * Handles the STOR command to send a file to the server.
 *
 * @param server_sd The socket file descriptor connected to the server.
 * @param filename The name of the file to send to the server.
 */
void handle_stor_command(int server_sd, const char *filename) {
    char buffer[BUF_SIZE];

    // Send the STOR command to the server
    sprintf(buffer, "STOR %s", filename);
    if (send(server_sd, buffer, strlen(buffer), 0) < 0) {
        perror("Error sending STOR command");
        return;
    }

    // Wait for server's response to proceed
    if (recv(server_sd, buffer, BUF_SIZE, 0) < 0) {
        perror("Error receiving server response");
        return;
    }

    // Check server's response
    if (strncmp(buffer, "150", 3) == 0) {  // Server is ready to receive the file
        send_file(server_sd, filename);
    } else {
        printf("Server response: %s\n", buffer);  // Handle other responses
    }
}

void send_file(int sockfd, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Error opening file");
        return;
    }

    char buffer[BUF_SIZE];
    size_t bytes_read;

    // Send the file data
    while ((bytes_read = fread(buffer, 1, BUF_SIZE, file)) > 0) {
        if (send(sockfd, buffer, bytes_read, 0) < 0) {
            perror("Error sending file data");
            break;
        }
    }

    if (ferror(file)) {
        perror("Error reading from file");
    }

    fclose(file);
    printf("File %s sent successfully.\n", filename);
}

/**
 * Handles the LIST command to request a list of files and directories from the server.
 *
 * @param server_sd The socket file descriptor connected to the server.
 */
void handle_list_command(int server_sd) {
    char buffer[BUF_SIZE];

    // Send the LIST command to the server
    strcpy(buffer, "LIST");
    if (send(server_sd, buffer, strlen(buffer), 0) < 0) {
        perror("Error sending LIST command");
        return;
    }

    // Wait for server's response to proceed
    if (recv(server_sd, buffer, BUF_SIZE, 0) < 0) {
        perror("Error receiving server response");
        return;
    }

    // Check server's response
    if (strncmp(buffer, "150", 3) == 0) {  // Server is sending the list
        printf("Server file list:\n");
        while (1) {
            ssize_t bytes_received = recv(server_sd, buffer, BUF_SIZE, 0);
            if (bytes_received <= 0) {
                break; // Break the loop on error or no more data
            }
            fwrite(buffer, 1, bytes_received, stdout); // Print the received list
        }
        printf("\nList transfer complete.\n");
    } else {
        printf("Server response: %s\n", buffer);  // Handle other responses
    }
}

/**
 * Lists the files and directories in the current local directory of the client.
 */
void print_local_directory() {
    DIR *dir;
    struct dirent *entry;

    dir = opendir("."); // Open the current directory
    if (dir == NULL) {
        perror("Unable to open local directory");
        return;
    }

    printf("Local directory listing:\n");
    while ((entry = readdir(dir)) != NULL) {
        printf("%s\n", entry->d_name); // Print each directory entry's name
    }

    closedir(dir); // Close the directory
}

/**
 * Changes the current local working directory of the client.
 *
 * @param path The path to the new directory.
 */
void change_local_directory(const char *path) {
    if (chdir(path) != 0) {
        // If chdir returns a non-zero value, an error occurred
        perror("Failed to change local directory");
    } else {
        printf("Local directory changed to: %s\n", path);
    }
}

/**
 * Requests and prints the current working directory from the server.
 *
 * @param sockfd The socket file descriptor connected to the server.
 */
void print_working_directory(int sockfd) {
    char buffer[BUF_SIZE];

    // Send the PWD command to the server
    strcpy(buffer, "PWD");
    if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
        perror("Error sending PWD command");
        return;
    }

    // Wait for server's response
    ssize_t bytes_received = recv(sockfd, buffer, BUF_SIZE, 0);
    if (bytes_received < 0) {
        perror("Error receiving server response");
        return;
    }

    buffer[bytes_received] = '\0'; // Null-terminate the received string

    // Print the server's response
    printf("Server's Current Working Directory: %s\n", buffer);
}