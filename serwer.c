#include <netinet/in.h>
#include <unistd.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>

#include "err.h"
#include "helper.h"

#define QUEUE_LENGTH 5

void getFilesInDir(int msg_sock, char *folderName) {
    int totalSize, freeSize;
    totalSize = freeSize = 2048;
    char *files = malloc(totalSize);

    if (files == NULL) {
        syserr("malloc error");
    }
    files[0] = '\0';
    printf("Folder name: %s\n", folderName);
    DIR *dir;
    struct dirent *ent;
    bool first = true;
    struct stat fileInfo;
    if ((dir = opendir(folderName)) != NULL) {
        /* print all the files and directories within directory */
        while ((ent = readdir(dir)) != NULL) {
            char path[512];
            path[0] = 0;
            strcat(path, folderName);
            strcat(path, "/");
            strcat(path, ent->d_name);
            lstat(path, &fileInfo);

            if (S_ISREG(fileInfo.st_mode)) {
                size_t fileLength = strlen(ent->d_name);
                if (fileLength + 1 > freeSize) {
                    freeSize += totalSize;
                    totalSize *= 2;
                    files = realloc(files, totalSize);
                }
                if (!first) {
                    strcat(files, "|");
                }

                strcat(files, ent->d_name);
                first = false;
                freeSize -= fileLength + 1;
            }
        }
        closedir(dir);
    } else {
        /* could not open directory */
        perror ("");
        exit(EXIT_FAILURE);
    }

    size_t listLength = strlen(files);
    struct serverResponse files;
    files.first = htons(1);
    files.second = htonl(listLength);
    write(msg_sock, &files, sizeof(files)); // Send files list to client.
    write(msg_sock, files, listLength);
    free(files);
}

// Odczytujemy dane ze struktury i wysyłamy fragment odpowiedniego pliku.
void sendFileFragment(int msg_sock, struct sendFileFragment *structure, char *folderName,
        char *buffer) {
    uint32_t beginningAddress = ntohl(structure->beginningAddress);
    uint32_t bytesToSend = ntohl(structure->bytesToSend);
    uint16_t fileNameLength = ntohs(structure->fileNameLength);

    char fileName[256];
    receiveBytes(msg_sock, fileName, (size_t)fileNameLength);
    fileName[fileNameLength] = '\0';
    printf("Received request for file: %s\n", fileName);

    // Open file name received from client.
    char filePath[512];
    filePath[0] = '\0';
    strcat(filePath, folderName);
    strcat(filePath, "/");
    strcat(filePath, fileName);

    struct serverResponse response;
    response.first = htons(2);

    FILE *f = fopen(filePath, "r"); // File must exist.
    if (f == NULL) {
        // Send denial to client.
        response.second = htonl(1);
        printf("Sending denial to client - no such file\n");
        write(msg_sock, &response, sizeof(response));
        return;
    }

    fseek(f, 0, SEEK_END);
    int fileSize = ftell(f);
    fseek(f, beginningAddress, SEEK_SET);

    if (beginningAddress > fileSize - 1) {
        printf("Sending denial to client - wrong beginning address\n");
        response.second = htonl(2);
        write(msg_sock, &response, sizeof(response));
        return;
    }

    if (bytesToSend == 0 || fileSize == 0) {
        printf("Sending denial to client -  zero sized fragment\n");
        response.second = htonl(3);
        write(msg_sock, &response, sizeof(response));
        return;
    }

    // We can read data from file.
    if (beginningAddress + bytesToSend > fileSize) {
        // Change bytesSend to end of file.
        bytesToSend = fileSize - beginningAddress;
    }

    printf("Bytes to send: %" PRIu32 "\n", bytesToSend);
    printf("Sending data to client\n");

    // Send message to client.
    response.first = htons(3);
    response.second = htonl(bytesToSend);
    write(msg_sock, &response, sizeof(response));

    // Send data to client.
    while (bytesToSend > 0) {
        int bytesToWrite = bytesToSend < BUFFER_SIZE ? bytesToSend : BUFFER_SIZE;
        fread(buffer, 1, bytesToWrite, f);
        if (write(msg_sock, buffer, bytesToWrite) != bytesToWrite) {
            syserr("partial / failed write");
        }

        bytesToSend -= bytesToWrite;
    }

    printf("All data was sent to client\n");
    fclose(f);
}

int setSocket(struct sockaddr_in *server_address) {
    int sock = socket(PF_INET, SOCK_STREAM, 0); // Creating IPv4 TCP socket.

    if (sock < 0)
        syserr("socket");

    server_address->sin_family = AF_INET; // IPv4
    server_address->sin_addr.s_addr = htonl(INADDR_ANY); // listening on all interfaces
    server_address->sin_port = htons(port_number); // listening on port PORT_NUM

    // bind the socket to a concrete address
    if (bind(sock, (struct sockaddr *) server_address, sizeof(*server_address)) < 0)
        syserr("bind");

    // switch to listening (passive open)
    if (listen(sock, QUEUE_LENGTH) < 0)
        syserr("listen");

    return sock;
}

// argv[1] - dirName
// argv[2] - port number (optional)
int main(int argc, char **argv) {

    int port_number;
    char *folder_name = argv[1];

    if (argc == 3) {
        port_number = atoi(argv[2]);
    }
    else {
        port_number = 6543; // Default port number.
    }

    int sock, msg_sock;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    socklen_t client_address_len;

    char buffer[BUFFER_SIZE];

    sock = socket(PF_INET, SOCK_STREAM, 0); // Creating IPv4 TCP socket.

    if (sock < 0)
        syserr("socket");

    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr = htonl(INADDR_ANY); // listening on all interfaces
    server_address.sin_port = htons(port_number); // listening on port PORT_NUM

    // bind the socket to a concrete address
    if (bind(sock, (struct sockaddr *) &server_address, sizeof(server_address)) < 0)
        syserr("bind");

    // switch to listening (passive open)
    if (listen(sock, QUEUE_LENGTH) < 0)
        syserr("listen");

    printf("Accepting client connections on port %hu\n", ntohs(server_address.sin_port));
    for (;;) {
        client_address_len = sizeof(client_address);
        // get client connection from the socket
        msg_sock = accept(sock, (struct sockaddr *) &client_address, &client_address_len);
        if (msg_sock < 0)
            syserr("accept");

        printf("==================================================\n");
        struct commandNumber data_read;

        int retCode = receiveBytes(msg_sock, &data_read, sizeof(data_read));
        if (retCode == 1) {
            printf("Wrong data from client - ending connection\n");
            continue;
        }
        int commandNumber = ntohs(data_read.number);

        if (commandNumber == 1) {
            printf("Received request for files list\n");
            getFilesInDir(msg_sock, folder_name);
            retCode = receiveBytes(msg_sock, &data_read, sizeof(data_read));
            if (retCode == 1) {
                printf("Wrong data from client - ending connection\n");
                continue;
            }

            struct sendFileFragment file;
            retCode = receiveBytes(msg_sock, &file, sizeof(file));
            if (retCode == 1) {
                printf("wrong data from client - ending connection\n");
                continue;
            }
            sendFileFragment(msg_sock, &file, folder_name, buffer);
        } else if (commandNumber == 2) {
            printf("Received request for file fragment\n");
            struct sendFileFragment sf;
            retCode = receiveBytes(msg_sock, &sf, sizeof(sf));
            if (retCode == 1) {
                printf("Wrong data from client - ending connection\n");
                continue;
            }
            sendFileFragment(msg_sock, &sf, folder_name, buffer);
        } else {
            printf("No such command\n");
        }

        read(msg_sock, &retCode, 1);
        printf("Ending connection with client\n");

        if (close(msg_sock) < 0)
            syserr("close");
    }

    return 0;
}