#include "circularBuffer.h"
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

static void process_text_buffer(CircularBuffer* cb, int reachedEOF, int* printed) {
    char numbuf[64];
    while (1) {
        int elem_size = buffer_size_next_element(cb, ',', reachedEOF);
        if (elem_size == -1) break;

        int idx = 0;
        for (int i = 0; i < elem_size; i++) {
            unsigned char c = buffer_pop(cb);
            if (c == ',') continue; //skip delimiter
            if (idx < (int)sizeof(numbuf) - 1) {
                numbuf[idx++] = (char)c;
            }
        }
        numbuf[idx] = '\0';

        if (*printed < 10) {
            int val = atoi(numbuf);
            printf("%d ", val);
        }
        (*printed)++;
    }
}

static void handle_text_file(const char* path, int bufSize) {
    int fd = open(path, O_RDONLY);
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

    unsigned char chunk[4096];
    int printed = 0;
    int bytes_read;
    while ((bytes_read = read(fd, chunk, sizeof(chunk))) > 0) {
        for (int i = 0; i < bytes_read; i++) {
            while (buffer_free_bytes(&cb) == 0) {
                process_text_buffer(&cb, 0, &printed);
                if (buffer_free_bytes(&cb) == 0) break; //avoid infinite loop if no delimiter
            }
            if (buffer_free_bytes(&cb) > 0) {
                buffer_push(&cb, chunk[i]);
            }
        }
        process_text_buffer(&cb, 0, &printed);
    }

    process_text_buffer(&cb, 1, &printed);

    buffer_deallocate(&cb);
    close(fd);
}

static void handle_binary_file(const char* path, int bufSize) {
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        perror("Error opening binary file");
        return;
    }

    if (bufSize % 4 != 0) {
        bufSize -= bufSize % 4;
    }
    if (bufSize <= 0) bufSize = 4;

    unsigned char* chunk = (unsigned char*)malloc(bufSize);
    if (!chunk) {
        printf("Failed to allocate chunk buffer\n");
        close(fd);
        return;
    }

    int printed = 0;
    int bytes_read;
    while ((bytes_read = read(fd, chunk, bufSize)) > 0 && printed < 10) {
        int limit = bytes_read - (bytes_read % 4);
        for (int i = 0; i < limit && printed < 10; i += 4) {
            unsigned int val = chunk[i] |
                              (chunk[i+1] << 8) |
                              (chunk[i+2] << 16) |
                              (chunk[i+3] << 24);
            printf("%u ", val);
            printed++;
        }
    }

    free(chunk);
    close(fd);
}

static void usage(void) {
    printf("Usage: ./main text|binary pathToFile sizeOfBuffer\n");
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        usage();
        return 1;
    }

    const char* mode = argv[1];
    const char* path = argv[2];
    int bufSize = atoi(argv[3]);
    if (bufSize <= 0) {
        printf("Buffer size must be > 0\n");
        return 1;
    }

    if (bufSize % 4 != 0) {
        printf("Buffer size %d is not divisible by 4, adjusting to %d\n", bufSize, bufSize - (bufSize % 4));
        bufSize -= bufSize % 4;
        if (bufSize <= 0) bufSize = 4;
    }

    printf("=== File Reading (chunked, circular buffer for text) ===\n\n");

    if (strcmp(mode, "text") == 0) {
        printf("Mode: text\nFile: %s\nBuffer: %d bytes\nFirst 10 integers: ", path, bufSize);
        handle_text_file(path, bufSize);
        printf("\n");
    } else if (strcmp(mode, "binary") == 0) {
        printf("Mode: binary\nFile: %s\nBuffer: %d bytes\nFirst 10 integers: ", path, bufSize);
        handle_binary_file(path, bufSize);
        printf("\n");
    } else {
        usage();
        return 1;
    }

    return 0;
}