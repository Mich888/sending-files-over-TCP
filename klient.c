#include <netdb.h>
#include <unistd.h>

#include "err.h"
#include "helper.h"

// 1 - send files list, 2 - send fragment of specific file.
void sendRequestToServer(int sock, uint16_t number) {
    struct commandNumber command;
    command.number = htons(number);
    ssize_t len = sizeof(command);

    if (write(sock, &command, len) != len) {
        syserr("partial / failed write");
    }
}

void sendFileFragmentRequest(int sock, uint32_t beginningAddress, uint32_t bytesToSend,
        uint16_t fileNameLength, char *fileName) {

    struct sendFileFragment fileFragment;
    fileFragment.beginningAddress = htonl(beginningAddress);
    fileFragment.bytesToSend = htonl(bytesToSend);
    fileFragment.fileNameLength = htons(fileNameLength);

    ssize_t len = sizeof(struct sendFileFragment);
    if (write(sock, &fileFragment, len) != len) {
        syserr("partial / failed write");
    }
}

void printFilesToUserAndGetInput(char *files, struct sendFileFragment *structure,
        char *file) {
    char *copy = malloc(strlen(files) + 1);
    strcpy(copy, files);

    if (strlen(files) == 0) {
        printf("No files in folder - ending connection with server\n");
        exit(0);
    }

    printf("Files in folder:\n");
    char delim = '|';
    char *ptr = strtok(files, &delim);

    uint32_t i = 1;
    while (ptr != NULL) {
        printf("%"PRIu32". %s\n", i, ptr);
        ptr = strtok(NULL, &delim);
        i++;
    }

    printf("\nChoose file number:\n");
    uint32_t number;
    int ret = scanf("%" SCNd32, &number);
    if (ret != 1) {
        printf("Not a number - ending connection with server\n");
        exit(0);
    }
    if (number >= (uint32_t)i || number == 0) {
        printf("Not a number from list - ending connection with server\n");
        exit(0);
    }

    printf("Choose beginning address:\n");
    uint32_t beginning; // o
    ret = scanf("%" SCNd32, &beginning);
    if (ret != 1) {
        printf("Not a number - ending connection with server\n");
        exit(0);
    }

    printf("Choose end address:\n");
    uint32_t end;
    ret = scanf("%" SCNd32, &end);
    if (ret != 1) {
        printf("Not a number - ending connection with server\n");
        exit(0);
    }
    if (end < beginning) {
        printf("Beginning address cannot be greater than end - ending connection with server\n");
        exit(0);
    }
    printf("Sending to server\n");
    printf("file number: %"PRIu32", beginning address: %"PRIu32
    ", end address: %"PRIu32"\n", number, beginning, end);

    uint32_t bytesToSend = end - beginning;

    ptr = strtok(copy, &delim);
    i = 1;
    while (i < number) {
        ptr = strtok(NULL, &delim);
        i++;
    }

    printf("file name: %s\n", ptr);

    uint16_t fileLength = (uint16_t)strlen(ptr);
    structure->beginningAddress = htonl(beginning);
    structure->bytesToSend = htonl(bytesToSend);
    structure->fileNameLength = htons(fileLength);

    strcpy(file, ptr);
}

void checkIfServerDenial(struct serverResponse *response) {
    if (ntohs(response->first) == 2) { // Odmowa
        printf("Received request denial from server: ");
        uint32_t reason = ntohl(response->second);
        if (reason == 1) {
            printf("no such file\n");
            exit(0);
        }
        if (reason == 2) {
            printf("wrong beginning address\n");
            exit(0);
        }
        if (reason == 3) {
            printf("zero sized fragment\n");
            exit(0);
        }
    }
}

void receiveFileAndSaveToTmp(int sock, char *file, uint32_t beginningAddress) {
    struct serverResponse response;
    receiveBytes(sock, &response, sizeof(response));
    checkIfServerDenial(&response);

    if (ntohs(response.first) == 3) { // Otrzymujemy bajty pliku od serwera.
        uint32_t bytesToReceive = ntohl(response.second);
        char buffer[BUFFER_SIZE + 1];

        FILE *f = NULL;
        errno = 0;
        char dirPath[1024] = "";
        strcat(dirPath, "tmp");
        int dir_result = mkdir(dirPath, S_IRWXU | S_IRWXG);
        if (dir_result != 0 && errno != EEXIST) {
            syserr("mkdir error");
        }
        else {
            strcat(dirPath, "/");
            strcat(dirPath, file);
            f = fopen(dirPath, "r+");
            if (f == NULL) {
                f = fopen(dirPath, "w");
                fclose(f);
                f = fopen(dirPath, "r+");
            }
            else {
                printf("File %s opened succesfully!\n", dirPath);
            }

            fseek(f, beginningAddress, SEEK_SET);
        }
        while (bytesToReceive > 0) {
            int bytesToRead = bytesToReceive < BUFFER_SIZE ? bytesToReceive : BUFFER_SIZE;
            receiveBytes(sock, buffer, bytesToRead);
            buffer[bytesToRead] = '\0';
            fwrite(buffer, 1, bytesToRead, f);
            bytesToReceive -= bytesToRead;
        }

        printf("Writing to file completed\n");
        fclose(f);
    }
}

// argv[1] - domain name or IP address
// argv[2] - port number (optional)
int main(int argc, char **argv) {

    char *port_number = "6543"; // Default port number.

    if (argc == 3) {
        port_number = argv[2]; // Second argument is port number.
    }

    int sock;
    struct addrinfo addr_hints;
    struct addrinfo *addr_result;

    int err;
    ssize_t len;

    // 'converting' host/port in string to struct addrinfo
    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_INET; // IPv4
    addr_hints.ai_socktype = SOCK_STREAM;
    addr_hints.ai_protocol = IPPROTO_TCP;
    err = getaddrinfo(argv[1], port_number, &addr_hints, &addr_result);
    if (err == EAI_SYSTEM) { // system error
        syserr("getaddrinfo: %s", gai_strerror(err));
    }
    else if (err != 0) { // other error (host not found, etc.)
        fatal("getaddrinfo: %s", gai_strerror(err));
    }

    // initialize socket according to getaddrinfo results
    sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
    if (sock < 0)
        syserr("socket");

    // connect socket to the server
    if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) < 0)
        syserr("connect");

    freeaddrinfo(addr_result);

    printf("Sending files list request to server\n");
    sendRequestToServer(sock, 1);

    struct serverResponse files;
    receiveBytes(sock, &files, sizeof(files));
    files.first = ntohs(files.first);
    files.second = ntohl(files.second);

    char *fileList = malloc(files.second + 1);
    receiveBytes(sock, fileList, files.second);
    fileList[files.second] = '\0';

    struct sendFileFragment sendFileFr;
    char file[256];
    printFilesToUserAndGetInput(fileList, &sendFileFr, file);

    uint32_t len2 = ntohs(sendFileFr.fileNameLength);
    sendRequestToServer(sock, 2);
    len = sizeof(struct sendFileFragment);
    if (write(sock, &sendFileFr, len) != len) {
        syserr("partial / failed write");
    }

    ssize_t bytesSent = write(sock, file, len2);
    if (bytesSent != len2) {
        syserr("partial / failed write");
    }

    receiveFileAndSaveToTmp(sock, file, ntohl(sendFileFr.beginningAddress));

    (void)close(sock); // socket would be closed anyway when the program ends

    return 0;
}