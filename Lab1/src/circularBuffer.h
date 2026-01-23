#pragma once

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    unsigned char * data;
    int size;
    int start;
    int end;
} CircularBuffer;

int buffer_init(CircularBuffer* buffer, int size);
void buffer_deallocate(CircularBuffer* buffer);

int buffer_used_bytes(CircularBuffer* buffer);
int buffer_free_bytes(CircularBuffer* buffer);

void buffer_push(CircularBuffer* buffer, unsigned char c);
unsigned char buffer_pop(CircularBuffer* buffer);
int buffer_size_next_element(CircularBuffer* buffer, unsigned char delimeter, int reachedEOF);

//file reading
typedef struct {
    unsigned char * data;
    int size;
    int capacity;
} LinearBuffer;

LinearBuffer* read_text_file(const char* filename);
LinearBuffer* read_binary_file(const char* filename);
void linear_buffer_free(LinearBuffer* buffer);
