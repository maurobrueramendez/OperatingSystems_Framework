#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "parsePGM.h"

#define BUFF_SIZE 1024
#define HISTOGRAM_SIZE 256

typedef struct { 
    char* path; 
    int offset;       // Offset from the beginning of the file (including header) 
    int bytesToRead; 
    int* histogram; //each thread will have its own histogram
} ThreadInfo; 

//executed by each thread to compute a partial histogram
void* compute_histogram_thread(void* arg) {
    ThreadInfo* info = (ThreadInfo*)arg;
    
    //init local histogram to zeros
    for (int i = 0; i < HISTOGRAM_SIZE; i++) {
        info->histogram[i] = 0;
    }

    //each thread opens the file independently
    int fd = open(info->path, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open file in thread");
        return NULL;
    }

    //seek to the assigned offset
    if (lseek(fd, info->offset, SEEK_SET) == -1) {
        perror("Failed to seek in thread");
        close(fd);
        return NULL;
    }

    unsigned char buffer[BUFF_SIZE];
    int bytes_left = info->bytesToRead;
    int bytes_to_read_now;
    int nBytesRead;

    //read the assigned portion of the file in chunks
    while (bytes_left > 0) {
        if (bytes_left > BUFF_SIZE) {
            bytes_to_read_now = BUFF_SIZE;
        } else {
            bytes_to_read_now = bytes_left;
        }
        nBytesRead = read(fd, buffer, bytes_to_read_now);
        if (nBytesRead <= 0) {
            //if read returns 0 or -1, stop processing
            break; 
        }

        //update the local histogram
        for (int i = 0; i < nBytesRead; i++) {
            info->histogram[buffer[i]]++;
        }
        bytes_left -= nBytesRead;
    }

    close(fd);
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        printf("Args needed: ./computeHistogramParallel pathToImage pathToHistogramOut numThreads\n");
        exit(1);
    }

    char* imagePath = argv[1];
    char* outputPath = argv[2];
    int numThreads = atoi(argv[3]);
    
    //read the PGM header to get image metadata
    int width, height, maxval;
    int header_bytes = parse_pgm_header(imagePath, &width, &height, &maxval);
    if (header_bytes < 0) {
        printf("Failed to parse PGM header.\n");
        exit(1);
    }

    //open file to get the total size of the image
    int fd_tmp = open(imagePath, O_RDONLY);
    if (fd_tmp < 0) {
        perror("Failed to open image file");
        exit(1);
    }
    long total_file_size = lseek(fd_tmp, 0, SEEK_END);
    close(fd_tmp);
    
    long data_size = total_file_size - header_bytes;

    //thread management
    pthread_t threads[numThreads];
    ThreadInfo thread_infos[numThreads];
    int* partial_histograms[numThreads];

    long bytes_per_thread = data_size / numThreads;
    long current_offset = header_bytes;

    //create threads
    for (int i = 0; i < numThreads; i++) {
        partial_histograms[i] = (int*)malloc(HISTOGRAM_SIZE * sizeof(int));
        if (partial_histograms[i] == NULL) {
            perror("Failed to allocate memory for partial histogram");
            exit(1);
        }

        thread_infos[i].path = imagePath;
        thread_infos[i].offset = current_offset;
        thread_infos[i].histogram = partial_histograms[i];
        
        //the last thread gets the remaining bytes
        if (i == numThreads - 1) {
            thread_infos[i].bytesToRead = data_size - (bytes_per_thread * (numThreads - 1));
        } else {
            thread_infos[i].bytesToRead = bytes_per_thread;
        }

        current_offset += thread_infos[i].bytesToRead;

        //create the thread
        if (pthread_create(&threads[i], NULL, compute_histogram_thread, &thread_infos[i]) != 0) {
            perror("Failed to create thread");
            exit(1);
        }
    }

    int final_histogram[HISTOGRAM_SIZE];
    for(int i = 0; i < HISTOGRAM_SIZE; i++) {
        final_histogram[i] = 0;
    }
    
    //wait for threads to complete
    for (int i = 0; i < numThreads; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            perror("Failed to join thread");
            exit(1);
        }
        //add partial histogram to the final one
        for (int j = 0; j < HISTOGRAM_SIZE; j++) {
            final_histogram[j] += thread_infos[i].histogram[j];
        }
        //free memory for the partial histogram
        free(partial_histograms[i]);
    }

    //write the final histogram to the output file
    int fd_out = open(outputPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_out < 0) {
        perror("Failed to open output file");
        exit(1);
    }

    for (int i = 0; i < HISTOGRAM_SIZE; i++) {
        char line[80];
        int len = sprintf(line, "%d,%d\n", i, final_histogram[i]);
        if (write(fd_out, line, len) != len) {
            perror("Failed to write to output file");
            close(fd_out);
            exit(1);
        }
    }

    close(fd_out);
    printf("Histogram computed successfully and saved to %s\n", outputPath);

    return 0;
}
