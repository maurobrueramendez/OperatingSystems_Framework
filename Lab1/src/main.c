#include "circularBuffer.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

// parses complete CSV numbers from circular buff
static void process_text_buffer(CircularBuffer* cb, int reachedEOF, int* printed, int* sum) {
    char numbuf[64];    // temp array to build number strings
    while (1) {
        int elem_size = buffer_size_next_element(cb, ',', reachedEOF);  // returns num bytes of elem, includes commas
        if (elem_size == -1) break;

        int idx = 0;    // tracks num of chars stored in temp buff
        for (int i = 0; i < elem_size; i++) {
            unsigned char c = buffer_pop(cb);   // remove 1 byte from circular buff
            if (c == ',') continue;             // skip delimiter
            if (idx < (int)sizeof(numbuf) - 1) {
                numbuf[idx++] = (char)c;        // append byte to temp buff
            }
        }
        numbuf[idx] = '\0';

        int val = atoi(numbuf);         // converts text digits to int
        *sum += val;
        
        // print 10 first numbers
        if (*printed < 10) {
            printf("%d ", val);
        }
        (*printed)++;
    }
}

// reads text file, feeds bytes to circular buff, computes sum
static void handle_text_file(const char* path, int bufSize) {
    int fd = open(path, O_RDONLY);      // opens file, read-only
    if (fd == -1) {
        perror("Error opening text file");
        return;
    }

    CircularBuffer cb;
    if (buffer_init(&cb, bufSize) != 0) {
        printf("Failed to init circular buffer\n");
        close(fd);
        return;
    }

    unsigned char* chunk = (unsigned char*)malloc((size_t)bufSize);      // linear read buff
    if (!chunk) {
        printf("Failed to allocate chunk buffer\n");
        buffer_deallocate(&cb);
        close(fd);
        return;
    }

    int printed = 0;
    int bytes_read;
    int sum = 0;

    while ((bytes_read = (int)read(fd, chunk, (size_t)bufSize)) > 0) {
        for (int i = 0; i < bytes_read; i++) {
            while (buffer_free_bytes(&cb) == 0) {       // if circular buff full
                process_text_buffer(&cb, 0, &printed, &sum);  // try extracting complete numbers
                if (buffer_free_bytes(&cb) == 0) break; // avoid infinite loop if no delimiter
            }
            if (buffer_free_bytes(&cb) > 0) {
                buffer_push(&cb, chunk[i]);             // push byte into circular buff
            }
        }
        // after pushing all bytes of this chunk, parse any complete numbers available by new bytes
        process_text_buffer(&cb, 0, &printed, &sum);
    }

    process_text_buffer(&cb, 1, &printed, &sum);  // eof

    printf("\nCount: %d", printed); // print count 
    printf("\nSum: %d", sum);     // print sum

    free(chunk);
    buffer_deallocate(&cb);
    close(fd);
}

// reads bin file, prints 10 first ints, computes sum
static void handle_binary_file(const char* path, int bufSize) {
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        perror("Error opening binary file");
        return;
    }

    // adjust buffer size to be multiple of 4 bytes (int size)
    if (bufSize % 4 != 0) {
        bufSize -= bufSize % 4;
    }
    if (bufSize <= 0) bufSize = 4;

    unsigned char* chunk = (unsigned char*)malloc((size_t)bufSize);
    if (!chunk) {
        printf("Failed to allocate chunk buffer\n");
        close(fd);
        return;
    }

    int count = 0;
    int printed = 0;
    int bytes_read;
    int sum = 0;

    // read chunks from file until eof, or until 10 printed
    while ((bytes_read = (int)read(fd, chunk, (size_t)bufSize)) > 0) {
        int limit = bytes_read - (bytes_read % 4);      // truncate bytes to mult of 4, so only parse complete int
        for (int i = 0; i < limit ; i += 4) {
            // rebuild int in little endian order (lowest byte, next shifted by 8, ...)
            unsigned int uval = (unsigned int)chunk[i] |
                              ((unsigned int)chunk[i+1] << 8) |
                              ((unsigned int)chunk[i+2] << 16) |
                              ((unsigned int)chunk[i+3] << 24);
            int val = (int)uval;
            sum += val;
            count++;
            if (printed < 10) {
                printf("%d ", val);
                printed++;
            }
        }
    }

    printf("\nCount: %d", count);
    printf("\nSum: %d", sum);

    free(chunk);
    close(fd);
}

static void usage(void) {
    printf("Usage: ./main text|binary pathToFile sizeOfBuffer\n");
}

int main(int argc, char* argv[]) {
    if (argc != 4) {    // check args count
        usage();
        return 1;
    }

    const char* mode = argv[1];     // parse mode
    const char* path = argv[2];     
    int bufSize = atoi(argv[3]);

    if (bufSize <= 0) {
        printf("Buffer size must be > 0\n");
        return 1;
    }
    


    printf("=== File Reading (chunked, circular buffer for text) ===\n\n");

    // select which handler to call
    if (strcmp(mode, "text") == 0) {
        printf("Mode: text\nFile: %s\nBuffer: %d bytes\nFirst 10 integers: ", path, bufSize);
        handle_text_file(path, bufSize);
        printf("\n");
    } else if (strcmp(mode, "binary") == 0) {
        // adjusts buf size to mult of 4 
        if (bufSize % 4 != 0) {
            printf("Buffer size %d is not divisible by 4, adjusting to %d\n", bufSize, bufSize - (bufSize % 4));
            bufSize -= bufSize % 4;
            if (bufSize <= 0) bufSize = 4;
        }
        printf("Mode: binary\nFile: %s\nBuffer: %d bytes\nFirst 10 integers: ", path, bufSize);
        handle_binary_file(path, bufSize);
        printf("\n");
    } else {
        usage();
        return 1;
    }

    return 0;
}