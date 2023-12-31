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

// Global variable for the data connection socket
int data_socket = -1;
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
void send_file(const char *filename);
void receive_file(const char *filename);
void handle_PORT(int client_sock, char *command);
/**
 * Parses the configuration file to load usernames and passwords.
 *
 * @return int Returns 0 on successful parsing, 1 on file open error.
 */
int parse_config_file() {
    FILE *file = fopen("users.csv", "r");
    if (!file) {
        perror("Error opening file");
        return 1; // File open error
    }

    char line[BUF_SIZE];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = 0; // Remove newline character

        // Tokenize the line to get username and password
        char *token = strtok(line, ",");
        if (token != NULL && user_count < MAX_USERS) {
            strncpy(users[user_count].username, token, BUF_SIZE - 1);
            token = strtok(NULL, ",");
            if (token != NULL) {
                strncpy(users[user_count].password, token, BUF_SIZE - 1);
                user_count++; // Increment user count
            }
        }
        printf("Loaded user: %s\n", users[user_count - 1].password);
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
 * Find whether user exist
 */
 int findUsername(char name[]){ 
	for(int i=0;i<user_count;i++){
		if(strcmp(name, users[i].username)==0){
			return i;
		}
	}
	return -1;
}
/**
 * CHeck whether password is correct
 
 */
int matchPassword(int userIdx, char* pass){
	if(strcmp(users[userIdx].password, pass)==0){
		return 1;
	}
	else{
		return 0;
	}
}
/**
 * change directory [when user login]
*/
void changeWorkingDirectory(const char *username) {
    char userFolder[256];
    snprintf(userFolder, sizeof(userFolder), "server/%s", username);

    if (chdir(userFolder) != 0) {
        perror("chdir");
        exit(EXIT_FAILURE);
    }
}
// Function to handle PORT command
void handle_PORT(int client_sock, char *command) {
    char client_ip[INET_ADDRSTRLEN];
    int client_port;
    int h1, h2, h3, h4, p1, p2;

    // Parse the IP address and port number from the PORT command
    if (sscanf(command, "PORT %d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        send(client_sock, "501 Syntax error in parameters or arguments.\r\n", BUF_SIZE, 0);
        return;
    }

    // Reconstruct the IP address and port
    snprintf(client_ip, sizeof(client_ip), "%d.%d.%d.%d", h1, h2, h3, h4);
    client_port = p1 * 256 + p2;

    // Create a data socket
    int data_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (data_sock < 0) {
        perror("Failed to create data socket");
        send(client_sock, "425 Can't open data connection.\r\n", BUF_SIZE, 0);
        return;
    }

    // Set up the data server address
    struct sockaddr_in data_addr;
    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    data_addr.sin_port = htons(client_port);
    if (inet_pton(AF_INET, client_ip, &data_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(data_sock);
        send(client_sock, "425 Can't open data connection.\r\n", BUF_SIZE, 0);
        return;
    }

    // Connect to the client on the specified port
    if (connect(data_sock, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
        perror("Connection to data port failed");
        close(data_sock);
        send(client_sock, "425 Can't open data connection.\r\n", BUF_SIZE, 0);
        return;
    }
    // Connection established
    send(client_sock, "200 PORT command successful.\r\n", BUF_SIZE, 0);
    // close the data socket after the transfer is complete
    // close(data_sock);
}

/**
 * handle PASS command after user has logged in
 */
void handlePASS(int client_sock, int userIdx){
	char pass_command[BUF_SIZE];
	int bytes_received=recv(client_sock,pass_command,sizeof(pass_command),0);
    pass_command[bytes_received] = '\0';
	char* key_word = strtok(pass_command, " ");
    char* pass = strtok(NULL, "\n");
	if(strcmp(key_word, "PASS")==0){
        printf("pass: %s\n", pass);
		if(matchPassword(userIdx, pass)==1){
			send(client_sock, "230 User logged in, proceed.", BUF_SIZE, 0);
            authenticated_sockets[client_sock] = 1;
			printf("Server sent \" 230 User logged in, proceed.\" \n");
		}
		else{
			send(client_sock, "530 Not logged in.", BUF_SIZE, 0);
			printf("Server sent \" 530 Not logged in.\" \n");
		}
	}
	else{
		send(client_sock, "530 Not logged in.", BUF_SIZE, 0);
		printf("Server sent \" 530 Not logged in.\" \n");
	}
    if (authenticated_sockets[client_sock] == 1) {
        changeWorkingDirectory(users[userIdx].username);
    }
}

/**
 * handle USER command
 */
int handle_USER(int client_sock,char name[]){
    int userIdx = findUsername(name);
    if(userIdx==-1){
        send(client_sock, "530 Not logged in.\n", BUF_SIZE, 0);
        printf("530 Not logged in\n");
        return 0;
    }
    else{
        send(client_sock, "331 Username OK, need password\n", BUF_SIZE, 0);
        printf("331 Username OK, need password\n");
        handlePASS(client_sock, userIdx);
        return 1;
    }
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
    int userIdx;

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
    if (strcmp(command, "PORT") == 0) {
        handle_PORT(client_sock, argument);
    } else if (strcmp(command, "RETR") == 0) {
        if (data_socket != -1) {
            send_file(argument); // Use data_socket for file transfer
            close(data_socket); // Close the data socket after the operation
            data_socket = -1;
        } else {
            send(client_sock, "425 Use PORT command first.\r\n", 30, 0);
        }
    } else if (strcmp(command, "STOR") == 0) {
        if (data_socket != -1) {
            receive_file(argument); // Use data_socket for file transfer
            close(data_socket); // Close the data socket after the operation
            data_socket = -1;
        } else {
            send(client_sock, "425 Use PORT command first.\r\n", 30, 0);
        }
    } else if (strcmp(command, "LIST") == 0) {
        if (data_socket != -1) {
            // Implement LIST command handling here
            close(data_socket); // Close the data socket after the operation
            data_socket = -1;
        } else {
            send(client_sock, "425 Use PORT command first.\r\n", 30, 0);
        }
    }
    else{
        send(client_sock, "Syntax error, command unrecognized.\r\n", 40, 0);
    }
    // Commands not related to file transfer are not handled here
}


/**
 * Sends a file to the client over the socket.
 *
 * @param filename The name of the file to be sent.
 * @param sockfd The socket file descriptor for sending the file.
 */
void send_file(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Error opening file");
        send(data_socket, "550 File not found.\r\n", 20, 0);
        return;
    }

    char buffer[BUF_SIZE];
    size_t bytes_read;

    // Read from the file and send data in chunks
    while ((bytes_read = fread(buffer, 1, BUF_SIZE, file)) > 0) {
        if (send(data_socket, buffer, bytes_read, 0) < 0) {
            perror("Error sending file");
            break;
        }
    }

    fclose(file); // Close the file after sending
    send(data_socket, "226 Transfer complete.\r\n", 24, 0); // Notify the client of completion
}

/**
 * Receives a file from a client over a socket and saves it to the server's filesystem.
 *
 * @param filename The name of the file to be saved on the server.
 * @param sockfd The socket file descriptor for communication with the client.
 */
void receive_file(const char *filename) {
    // Open the file for writing in binary mode
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        perror("Error opening file");
        // If unable to open the file, send an error message to the client
        send(data_socket, "550 Cannot create file.\r\n", 24, 0);
        return;
    }

    char buffer[BUF_SIZE];
    ssize_t bytes_received;

    // Receive data from the client until the entire file is received
    while ((bytes_received = recv(data_socket, buffer, BUF_SIZE, 0)) > 0) {
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
    send(data_socket, "226 Transfer complete.\r\n", 24, 0);
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
                    char buffer[BUF_SIZE];
                    ssize_t bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);

                    // Check for client disconnection or reception error
                    if (bytes_received <= 0) {
                        close(client_sock); // Close the client socket
                        FD_CLR(client_sock, &master_set); // Remove from the master set
                        break;
                    }

                    buffer[bytes_received] = '\0'; // Ensure the received data is null-terminated

                    // Parse the command and its arguments
                    char *command = strtok(buffer, " ");
                    char *argument = strtok(NULL, "\n");
                    char server_res[BUF_SIZE];
                    if (strcmp(command, "USER") == 0) {
                        int userIdx =findUsername(argument);
                        handle_USER(client_sock,argument);
                        continue;
                    } else if (strcmp(command, "PASS") == 0) {
                        send(client_sock, "Please enter username first", BUF_SIZE, 0);
						printf("Server sent \" 530 Not logged in.\" \n");
                        continue;
                    } 
                    if (authenticated_sockets[client_sock] == 0) {
                        send(client_sock, "530 Not logged in.\n", BUF_SIZE, 0);
                        printf("Server: 530 Not logged in.\" \n");
						continue;
                    }
                    else if (authenticated_sockets[client_sock] == 1) {
                        if (strcmp(command, "USER") == 0){
                            send(client_sock, "You have logged in, please quit before you login again.\n", BUF_SIZE, 0);
                        }
                        else if (strcmp(command, "PASS") == 0){
                            send(client_sock, "You have logged in, please quit before you login again.\n", BUF_SIZE, 0);
                        }
                        else if(strcmp(command, "PWD")==0){
						    char server_pwd[BUF_SIZE];
						    bzero(server_pwd, sizeof(server_pwd));
						    bzero(server_res, sizeof(server_res));
						
						    getcwd(server_pwd, sizeof(server_pwd)); 
						    strcpy(server_res,"257 ");
                            strcat(server_res, server_pwd);

						    // send server 257 and pathname to client
						    send(client_sock, server_res, strlen(server_res), 0);
                        }
                        else if(strcmp(command, "QUIT")==0){
                            send(client_sock, "221 Service closing control connection.\n", BUF_SIZE, 0);
                            printf("Server sent \"221 Service closing control connection.\" \n");
                            close(client_sock);
                            FD_CLR(client_sock, &master_set);
                            break;
                        }
                        else{
                            execute_command(client_sock, command, argument);
                        }
                        
                    // handle_user_command(i, &master_set, &fd_max);
                    }   
                }   
            }
        }
    }      
    // Clean up
    close(server_sock);
    return 0;
}











/**
 * handle PASS command
 */

// int handle_PASS(int client_sock,int userIdx,char pass[]){
//     if(userIdx==-1){
//         send(client_sock, "530 Not logged in, please enter correct username first\n", BUF_SIZE, 0);
//         printf("530 Not logged in, please enter correct username first\n");
//         return 0;
//     }
//     else{
//         if(matchPassword(userIdx, pass)==1){
//             send(client_sock, "230 User logged in, proceed\n", BUF_SIZE, 0);
//             printf("230 User logged in, proceed\n");
//             return 1;
//         }
//         else{
//             send(client_sock, "530 Not logged in\n", BUF_SIZE, 0);
//             printf("530 Not logged in\n");
//             return 0;
//         }
//     }
// }

// void send_file(const char *filename, int sockfd) {
//     FILE *file = fopen(filename, "rb");
//     if (file == NULL) {
//         perror("Error opening file");
//         send(sockfd, "550 File not found.\r\n", 20, 0);
//         return;
//     }

//     char buffer[BUF_SIZE];
//     size_t bytes_read;

//     // Read from the file and send data in chunks
//     while ((bytes_read = fread(buffer, 1, BUF_SIZE, file)) > 0) {
//         if (send(sockfd, buffer, bytes_read, 0) < 0) {
//             perror("Error sending file");
//             break;
//         }
//     }

//     fclose(file); // Close the file after sending
//     send(sockfd, "226 Transfer complete.\r\n", 24, 0); // Notify the client of completion
// }

// void receive_file(const char *filename, int sockfd) {
//     // Open the file for writing in binary mode
//     FILE *file = fopen(filename, "wb");
//     if (file == NULL) {
//         perror("Error opening file");
//         // If unable to open the file, send an error message to the client
//         send(sockfd, "550 Cannot create file.\r\n", 24, 0);
//         return;
//     }

//     char buffer[BUF_SIZE];
//     ssize_t bytes_received;

//     // Receive data from the client until the entire file is received
//     while ((bytes_received = recv(sockfd, buffer, BUF_SIZE, 0)) > 0) {
//         // Write the received bytes to the file
//         size_t bytes_written = fwrite(buffer, 1, bytes_received, file);
//         if (bytes_written < bytes_received) {
//             perror("Error writing to file");
//             break; // Break the loop if there is a file writing error
//         }
//     }

//     // Check for errors in receiving data
//     if (bytes_received < 0) {
//         perror("Error receiving file");
//     }

//     // Close the file after receiving all data
//     fclose(file);

//     // Optionally, send a completion message to the client
//     send(sockfd, "226 Transfer complete.\r\n", 24, 0);
// }

// void execute_command(int client_sock, const char *command, const char *argument) {
//     if (strcmp(command, "RETR") == 0) {
//         send_file(argument, client_sock); // Handle file sending to the client
//     } else if (strcmp(command, "STOR") == 0) {
//         receive_file(argument, client_sock); // Handle file receiving from the client
//     }
//     if(strcmp(command, "USER")==0){
//         handle_USER(client_sock,argument);
//     }
   
//     // Additional FTP commands can be handled here
// }
