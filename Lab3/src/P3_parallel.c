#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include "parsePGM.h"

#define BUFF_SIZE 1024              // max num of bytes read per chunk
#define HISTOGRAM_SIZE 256          // max possible bins for 1-byte grayscale images


// include bins to store num of valid histogram bins (maxval + 1).
typedef struct {
    char* path;          // path to PGM image file
    int offset;          // starting byte offset in the file for this thread
    int bytesToRead;     // num of bytes this thread must process
    int* histogram;      // pointer to this thread's private histogram

    int bins;            // num of histogram bins (maxval + 1)
} ThreadInfo;


// thread function:
// 1. initialises its private histogram
// 2. opens file independently
// 3. seeks to its assigned offset
// 4. reads its portion of the data in chunks
// 5. updates only its private histogram
// no locks needed since each thread works on independent memory
static void* compute_histogram_thread(void* arg) {
    ThreadInfo* info = (ThreadInfo*)arg;

    // initialise private histogram to zero
    for (int i = 0; i < info->bins; i++) {
        info->histogram[i] = 0;
    }

    // open file independently to avoid shared file descriptor issues
    int fd = open(info->path, O_RDONLY);
    if (fd < 0) {
        perror("failed to open file in thread");
        return NULL;
    }

    // move file pointer to assigned offset
    if (lseek(fd, (off_t)info->offset, SEEK_SET) == (off_t)-1) {
        perror("failed to seek in thread");
        close(fd);
        return NULL;
    }

    unsigned char buffer[BUFF_SIZE];
    int bytes_left = info->bytesToRead;

    // read assigned region in chunks
    while (bytes_left > 0) {
        int want = bytes_left > BUFF_SIZE ? BUFF_SIZE : bytes_left;
        ssize_t got = read(fd, buffer, (size_t)want);
        if (got <= 0) break;

        // update private histogram
        for (ssize_t i = 0; i < got; i++) {
            unsigned char v = buffer[i];
            if ((int)v < info->bins) {
                info->histogram[v]++;
            }
        }

        bytes_left -= (int)got;
    }

    close(fd);
    return NULL;
}


int main(int argc, char* argv[]) {

    // validate num of args
    if (argc != 4) {
        printf("Args needed: ./computeHistogramParallel pathToImage pathToHistogramOut numThreads\n");
        return 1;
    }

    char* imagePath = argv[1];
    char* outputPath = argv[2];
    int numThreads = atoi(argv[3]);

    if (numThreads <= 0) {
        printf("numThreads must be positive\n");
        return 1;
    }

    // parse PGM header to obtain width, height, maxval
    // header_bytes tells us where pixel data begins
    int width = 0, height = 0, maxval = 0;
    int header_bytes = parse_pgm_header(imagePath, &width, &height, &maxval);
    if (header_bytes < 0) {
        printf("Failed to parse PGM header.\n");
        return 1;
    }

    // 1-byte grayscale images
    if (maxval <= 0 || maxval > 255) {
        printf("Expecting 1 byte ints, maxval must be 255 or less\n");
        return 1;
    }

    int bins = maxval + 1;
    if (bins > HISTOGRAM_SIZE) {
        printf("Invalid bins size\n");
        return 1;
    }

    // determine total file size to compute size of data segment
    int fd_tmp = open(imagePath, O_RDONLY);
    if (fd_tmp < 0) {
        perror("Failed to open image file");
        return 1;
    }

    off_t total_file_size = lseek(fd_tmp, 0, SEEK_END);
    close(fd_tmp);

    if (total_file_size <= 0) {
        printf("Failed to get file size.\n");
        return 1;
    }

    off_t data_size_off = total_file_size - (off_t)header_bytes;
    if (data_size_off <= 0) {
        printf("Invalid data size.\n");
        return 1;
    }

    int data_size = (int)data_size_off;

    // allocate thread structures dynamically
    pthread_t* threads = (pthread_t*)malloc((size_t)numThreads * sizeof(pthread_t));
    ThreadInfo* infos = (ThreadInfo*)malloc((size_t)numThreads * sizeof(ThreadInfo));
    int** partials = (int**)malloc((size_t)numThreads * sizeof(int*));

    if (!threads || !infos || !partials) {
        perror("Failed to allocate thread structures");
        free(threads);
        free(infos);
        free(partials);
        return 1;
    }

    // divide data segment into contiguous chunks
    // last thread receives any remaining bytes
    int bytes_per_thread = data_size / numThreads;
    int current_offset = header_bytes;

    for (int i = 0; i < numThreads; i++) {

        // allocate private histogram for this thread
        partials[i] = (int*)malloc((size_t)bins * sizeof(int));
        if (!partials[i]) {
            perror("Failed to allocate partial histogram");
            for (int k = 0; k < i; k++) free(partials[k]);
            free(partials);
            free(infos);
            free(threads);
            return 1;
        }

        // fill ThreadInfo structure
        infos[i].path = imagePath;
        infos[i].offset = current_offset;
        infos[i].bytesToRead = (i == numThreads - 1)
                               ? (data_size - bytes_per_thread * (numThreads - 1))
                               : bytes_per_thread;
        infos[i].histogram = partials[i];
        infos[i].bins = bins;

        current_offset += infos[i].bytesToRead;

        // create thread
        if (pthread_create(&threads[i], NULL, compute_histogram_thread, &infos[i]) != 0) {
            perror("Failed to create thread");
            for (int k = 0; k <= i; k++) free(partials[k]);
            free(partials);
            free(infos);
            free(threads);
            return 1;
        }
    }

    // allocate and initialise final histogram
    // merge happens after all threads finish
    unsigned int* final_histogram =
        (unsigned int*)malloc((size_t)bins * sizeof(unsigned int));

    if (!final_histogram) {
        perror("Failed to allocate final histogram");
        for (int i = 0; i < numThreads; i++) pthread_join(threads[i], NULL);
        for (int i = 0; i < numThreads; i++) free(partials[i]);
        free(partials);
        free(infos);
        free(threads);
        return 1;
    }

    for (int i = 0; i < bins; i++) {
        final_histogram[i] = 0;
    }

    // wait for all threads and merge their private histograms
    // no synchronisation needed because merge is sequential
    for (int i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);

        for (int j = 0; j < bins; j++) {
            final_histogram[j] += (unsigned int)infos[i].histogram[j];
        }

        free(partials[i]);
    }

    free(partials);
    free(infos);
    free(threads);

    // write output file
    // to match professor sequential reference implementation
    // we output values from 0 to maxval - 1
    int fd_out = open(outputPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_out < 0) {
        perror("Failed to open output file");
        free(final_histogram);
        return 1;
    }

    for (int i = 0; i < maxval; i++) {
        char line[80];
        int len = snprintf(line, sizeof(line), "%d,%u\n", i, final_histogram[i]);

        if (len < 0 || len >= (int)sizeof(line)) {
            perror("Failed to format output line");
            close(fd_out);
            free(final_histogram);
            return 1;
        }

        if (write(fd_out, line, (size_t)len) != (ssize_t)len) {
            perror("Failed to write to output file");
            close(fd_out);
            free(final_histogram);
            return 1;
        }
    }

    close(fd_out);
    free(final_histogram);

    printf("Histogram computed successfully and saved to %s\n", outputPath);
    return 0;
}