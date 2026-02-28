#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>

#define HISTOGRAM_SIZE 256

// --- Data Structures ---

typedef struct {
    unsigned char *data;
    int size;
} BufferItem;

// --- Global Variables ---

//buffer
BufferItem *buffer;
int nBuffer; //capacity
int elementsInBuffer = 0; //count
int buffer_in = 0;
int buffer_out = 0;

//synchronization for Buffer
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_not_full = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_not_empty = PTHREAD_COND_INITIALIZER;

//file Reading
int blockSize = 1024 * 16;
// Position from which the Producer will try to read a block. This needs to be initialised to just after the header.
int readPos; 
pthread_mutex_t lock_read = PTHREAD_MUTEX_INITIALIZER;

//histogram
unsigned int histogram[HISTOGRAM_SIZE] = {0};
pthread_mutex_t histogram_mutex = PTHREAD_MUTEX_INITIALIZER;

//termination
int active_producers = 0;
int producers_finished = 0;

//helper functions for PGM parsing

static int read_byte(int fd, char *c, int *count) {
    int r = read(fd, c, 1);
    if (r != 1) return -1;
    (*count)++;
    return 0;
}

static int read_nonspace(int fd, char *out, int *count) {
    char c;
    while (1) {
        if (read_byte(fd, &c, count) < 0) return -1;
        if (isspace((unsigned char)c)) continue;
        if (c == '#') { //skip comments
            do {
                if (read_byte(fd, &c, count) < 0) return -1;
            } while (c != '\n');
            continue;
        }
        *out = c;
        return 0;
    }
}

static int read_int(int fd, int *value, int *count) {
    char c;
    int v = 0;
    if (read_nonspace(fd, &c, count) < 0) return -1;
    if (!isdigit((unsigned char)c)) return -1;
    do {
        v = v * 10 + (c - '0');
        if (read_byte(fd, &c, count) < 0) break;
    } while (isdigit((unsigned char)c));
    *value = v;
    return 0;
}

int parse_pgm_header(const char *path, int *width, int *height, int *maxval) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    
    int bytes = 0;
    char c1, c2;
    
    if (read_nonspace(fd, &c1, &bytes) < 0) { close(fd); return -1; }
    if (read_byte(fd, &c2, &bytes) < 0) { close(fd); return -1; }
    if (c1 != 'P' || c2 != '5') { close(fd); return -1; }
    
    if (read_int(fd, width, &bytes) < 0) { close(fd); return -1; }
    if (read_int(fd, height, &bytes) < 0) { close(fd); return -1; }
    if (read_int(fd, maxval, &bytes) < 0) { close(fd); return -1; }
    
    //the loop in read_int reads one extra byte (the delimiter).
    //we don't need to seek back because readPos will be set to 'bytes' 
    //which tracks exactly how many bytes were consumed INCLUDING the delimiter.
    
    close(fd);
    return bytes;
}

// --- Thread Functions ---

void * Producer (void* arg) {
    char * path = (char *) arg; // Need to pass the file as a path
    int fd = open(path, O_RDONLY);
    int nBytesRead; 
    int readPosLocal;

    if (fd < 0) { 
        perror("Producer failed to open file"); 
        //decrement active producers if file open fails
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
        pthread_mutex_lock(&lock_read);
        readPosLocal = readPos;
        readPos += blockSize;
        pthread_mutex_unlock(&lock_read);

        // Produce an item by reading a block
        lseek(fd, readPosLocal, SEEK_SET);
        unsigned char* buff = malloc(blockSize);
        if (!buff) {
            perror("Failed to allocate memory");
            break;
        }
        nBytesRead = read(fd, buff, blockSize);
        if (nBytesRead <= 0) {
            free(buff);
            break;
        }
        
        //Consumer part of adding to the buffer: need to 
        pthread_mutex_lock(&buffer_mutex);
        while(nBuffer == elementsInBuffer) {
            pthread_cond_wait(&cond_not_full, &buffer_mutex);
        }
        
        /* 
        To complete by the student: should insert the element to the buffer
        */
        buffer[buffer_in].data = buff;
        buffer[buffer_in].size = nBytesRead;
        buffer_in = (buffer_in + 1) % nBuffer;
        elementsInBuffer++;
        
        pthread_cond_signal(&cond_not_empty);
        pthread_mutex_unlock(&buffer_mutex);
    }
    // If exiting, make sure you wake up all sleeping threads before exiting 
    // (and that they don't go to sleep if the finishes)
    
    close(fd);
    
    pthread_mutex_lock(&buffer_mutex);
    active_producers--;
    if (active_producers == 0) {
        producers_finished = 1;
        pthread_cond_broadcast(&cond_not_empty);
    }
    pthread_mutex_unlock(&buffer_mutex);

    return NULL;
}

void * Consumer(void *arg) {
    while (1) {
        pthread_mutex_lock(&buffer_mutex);

        //wait while buffer is empty
        while (elementsInBuffer == 0) {
            if (producers_finished) {
                pthread_mutex_unlock(&buffer_mutex);
                return NULL;
            }
            pthread_cond_wait(&cond_not_empty, &buffer_mutex);
        }

        //remove item
        BufferItem item = buffer[buffer_out];
        buffer_out = (buffer_out + 1) % nBuffer;
        elementsInBuffer--;

        //signal producers
        pthread_cond_signal(&cond_not_full);
        pthread_mutex_unlock(&buffer_mutex);

        //process item (accumulate to local histogram first to minimize lock time)
        unsigned int local_histogram[HISTOGRAM_SIZE] = {0};
        for (int i = 0; i < item.size; i++) {
            local_histogram[item.data[i]]++;
        }

        //release memory for the block
        free(item.data);

        //update global histogram
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
        printf("Error: Arguments must be positive integers.\n");
        return 1;
    }

    // 1. Parse Header to find start of data
    int width, height, maxval;
    int header_size = parse_pgm_header(image_path, &width, &height, &maxval);
    if (header_size < 0) {
        printf("Error: Failed to parse PGM header.\n");
        return 1;
    }
    readPos = header_size;

    // 2. Initialize Buffer
    buffer = malloc(sizeof(BufferItem) * nBuffer);
    if (!buffer) {
        perror("Failed to allocate buffer");
        return 1;
    }

    // 3. Create Threads
    pthread_t *prod_threads = malloc(sizeof(pthread_t) * n_producers);
    pthread_t *cons_threads = malloc(sizeof(pthread_t) * n_consumers);
    active_producers = n_producers;

    for (int i = 0; i < n_producers; i++) {
        pthread_create(&prod_threads[i], NULL, Producer, image_path);
    }
    for (int i = 0; i < n_consumers; i++) {
        pthread_create(&cons_threads[i], NULL, Consumer, NULL);
    }

    // 4. Join Threads
    for (int i = 0; i < n_producers; i++) {
        pthread_join(prod_threads[i], NULL);
    }
    for (int i = 0; i < n_consumers; i++) {
        pthread_join(cons_threads[i], NULL);
    }

    // 5. Write Output
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

    // 6. Cleanup
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
