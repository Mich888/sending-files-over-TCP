#ifndef HELPER_H
#define HELPER_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include <errno.h>

#define BUFFER_SIZE 524288 // 512 KiB

struct __attribute__((__packed__)) commandNumber {
    uint16_t number;
};

struct __attribute__((__packed__)) sendFileFragment {
    uint32_t beginningAddress;
    uint32_t bytesToSend;
    uint16_t fileNameLength;
};

struct __attribute__((__packed__)) serverResponse {
    uint16_t first;
    uint32_t second;
};

int receiveBytes(int sock, void *pointer, size_t bytesToReceive) {
    int prevLen = 0; // number of bytes already in the buffer
    ssize_t len, remains;

    do {
        remains = bytesToReceive - prevLen; // number of bytes to be read
        len = read(sock, ((char*)pointer) + prevLen, remains);
        if (len < 0) {
            syserr("reading from client socket");
        }
        else if (len > 0) {
            prevLen += len;

            if (prevLen == bytesToReceive) {
                // We have received a whole structure.
                return 0;
            }
        }
    } while (len > 0);

    return 1;
}

#endif //HELPER_H
