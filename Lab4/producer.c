#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "parsePGM.h"

#define HISTOGRAM_SIZE 256
#define BLOCK_SIZE (1024 * 16)

// --- Data Structures ---

typedef struct {
    unsigned char *data;
    int size;
} BufferItem;

// --- Global Variables ---

// Buffer
BufferItem *buffer;
int nBuffer;                  // capacity
int elementsInBuffer = 0;     // count
int buffer_in = 0;
int buffer_out = 0;

// Synchronization for buffer
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_not_full = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_not_empty = PTHREAD_COND_INITIALIZER;

// File reading
int readPos;                  // initialized to just after the header
pthread_mutex_t lock_read = PTHREAD_MUTEX_INITIALIZER;

// Histogram
unsigned int histogram[HISTOGRAM_SIZE] = {0};
pthread_mutex_t histogram_mutex = PTHREAD_MUTEX_INITIALIZER;

// Termination
int active_producers = 0;
int producers_finished = 0;

// --- Thread Functions ---

void *Producer(void *arg) {
    char *path = (char *)arg;
    int fd = open(path, O_RDONLY);
    int nBytesRead;
    int readPosLocal;

    if (fd < 0) {
        perror("Producer failed to open file");

        pthread_mutex_lock(&buffer_mutex);
        active_producers--;
        if (active_producers == 0) {
            producers_finished = 1;
            pthread_cond_broadcast(&cond_not_empty);
        }
        pthread_mutex_unlock(&buffer_mutex);

        return NULL;
    }

    while (1) {
        // Claim next block position
        pthread_mutex_lock(&lock_read);
        readPosLocal = readPos;
        readPos += BLOCK_SIZE;
        pthread_mutex_unlock(&lock_read);

        // Read block from file
        if (lseek(fd, readPosLocal, SEEK_SET) < 0) {
            break;
        }

        unsigned char *buff = malloc(BLOCK_SIZE);
        if (!buff) {
            perror("Failed to allocate memory");
            break;
        }

        nBytesRead = (int)read(fd, buff, BLOCK_SIZE);
        if (nBytesRead <= 0) {
            free(buff);
            break;
        }

        // Add block to shared buffer
        pthread_mutex_lock(&buffer_mutex);
        while (elementsInBuffer == nBuffer) {
            pthread_cond_wait(&cond_not_full, &buffer_mutex);
        }

        buffer[buffer_in].data = buff;
        buffer[buffer_in].size = nBytesRead;
        buffer_in = (buffer_in + 1) % nBuffer;
        elementsInBuffer++;

        pthread_cond_signal(&cond_not_empty);
        pthread_mutex_unlock(&buffer_mutex);
    }

    close(fd);

    // Mark this producer as finished
    pthread_mutex_lock(&buffer_mutex);
    active_producers--;
    if (active_producers == 0) {
        producers_finished = 1;
        pthread_cond_broadcast(&cond_not_empty);
    }
    pthread_mutex_unlock(&buffer_mutex);

    return NULL;
}

void *Consumer(void *arg) {
    (void)arg;

    while (1) {
        BufferItem item;

        pthread_mutex_lock(&buffer_mutex);

        while (elementsInBuffer == 0) {
            if (producers_finished) {
                pthread_mutex_unlock(&buffer_mutex);
                return NULL;
            }
            pthread_cond_wait(&cond_not_empty, &buffer_mutex);
        }

        // Remove item from buffer
        item = buffer[buffer_out];
        buffer_out = (buffer_out + 1) % nBuffer;
        elementsInBuffer--;

        pthread_cond_signal(&cond_not_full);
        pthread_mutex_unlock(&buffer_mutex);

        // Process block with local histogram to reduce lock time
        unsigned int local_histogram[HISTOGRAM_SIZE] = {0};
        for (int i = 0; i < item.size; i++) {
            local_histogram[item.data[i]]++;
        }

        free(item.data);

        pthread_mutex_lock(&histogram_mutex);
        for (int i = 0; i < HISTOGRAM_SIZE; i++) {
            histogram[i] += local_histogram[i];
        }
        pthread_mutex_unlock(&histogram_mutex);
    }

    return NULL;
}

// --- Main ---

int main(int argc, char *argv[]) {
    if (argc != 6) {
        printf("Usage: %s Data/heart.pgm Data/histogram.txt N_producers N_consumers sizeBuffer\n", argv[0]);
        return 1;
    }

    char *image_path = argv[1];
    char *output_path = argv[2];
    int n_producers = atoi(argv[3]);
    int n_consumers = atoi(argv[4]);
    nBuffer = atoi(argv[5]);

    if (n_producers <= 0 || n_consumers <= 0 || nBuffer <= 0) {
        printf("Error: arguments must be positive integers.\n");
        return 1;
    }

    // Parse PGM header to find start of pixel data
    int width, height, maxval;
    int header_size = parse_pgm_header(image_path, &width, &height, &maxval);
    if (header_size < 0) {
        printf("Error: failed to parse PGM header.\n");
        return 1;
    }

    readPos = header_size;

    // Initialize buffer
    buffer = malloc(sizeof(BufferItem) * nBuffer);
    if (!buffer) {
        perror("Failed to allocate buffer");
        return 1;
    }

    // Create threads
    pthread_t *prod_threads = malloc(sizeof(pthread_t) * n_producers);
    pthread_t *cons_threads = malloc(sizeof(pthread_t) * n_consumers);

    if (!prod_threads || !cons_threads) {
        perror("Failed to allocate thread arrays");
        free(buffer);
        free(prod_threads);
        free(cons_threads);
        return 1;
    }

    active_producers = n_producers;

    for (int i = 0; i < n_producers; i++) {
        if (pthread_create(&prod_threads[i], NULL, Producer, image_path) != 0) {
            perror("Failed to create producer thread");
            return 1;
        }
    }

    for (int i = 0; i < n_consumers; i++) {
        if (pthread_create(&cons_threads[i], NULL, Consumer, NULL) != 0) {
            perror("Failed to create consumer thread");
            return 1;
        }
    }

    // Join threads
    for (int i = 0; i < n_producers; i++) {
        pthread_join(prod_threads[i], NULL);
    }

    for (int i = 0; i < n_consumers; i++) {
        pthread_join(cons_threads[i], NULL);
    }

    // Write output histogram
    int out_fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        perror("Failed to open output file");
    } else {
        char line[64];
    for (int i = 0; i < HISTOGRAM_SIZE - 1; i++) {
        sprintf(line, "%d,%u\n", i, histogram[i]);
        write(out_fd, line, strlen(line));
    }   
        close(out_fd);
    }

    // Cleanup
    free(buffer);
    free(prod_threads);
    free(cons_threads);

    pthread_mutex_destroy(&buffer_mutex);
    pthread_mutex_destroy(&lock_read);
    pthread_mutex_destroy(&histogram_mutex);
    pthread_cond_destroy(&cond_not_full);
    pthread_cond_destroy(&cond_not_empty);

    return 0;
}