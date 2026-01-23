#include "circularBuffer.h"

int main() {
    printf("=== File Reading ===\n\n");

    printf("Reading text file: int_text_small.txt\n");
    LinearBuffer *text_buffer = read_text_file("../Data/int_text_small.txt");
    if (text_buffer != NULL) {
        printf("Successfully read %d bytes from text file\n", text_buffer->size);
        printf("Content: ");
        for (int i = 0; i < text_buffer->size && i < 100; i++) {
            printf("%c", text_buffer->data[i]);
        }
        printf("\n\n");
        linear_buffer_free(text_buffer);
    } else {
        printf("Failed to read text file\n\n");
    }

    printf("Reading large text file: int_text_big.txt\n");
    LinearBuffer *text_big = read_text_file("../Data/int_text_big.txt");
    if (text_big != NULL) {
        printf("Successfully read %d bytes from large text file\n", text_big->size);
        printf("First 10 integers: ");
        
        int count = 0;
        int num = 0;
        for (int i = 0; i < text_big->size && count < 10; i++) {
            if (text_big->data[i] >= '0' && text_big->data[i] <= '9') {
                num = num * 10 + (text_big->data[i] - '0');
            } else if (text_big->data[i] == ',') {
                printf("%d ", num);
                num = 0;
                count++;
            }
        }
        if (count < 10 && num > 0) {
            printf("%d ", num);
        }
        
        printf("\n\n");
        linear_buffer_free(text_big);
    } else {
        printf("Failed to read large text file\n\n");
    }

    printf("Reading binary file: test_small.dat\n");
    LinearBuffer *binary_small = read_binary_file("../Data/test_small.dat");
    if (binary_small != NULL) {
        printf("Successfully read %d bytes from binary file\n", binary_small->size);
        printf("First 10 integers: ");
        for (int i = 0; i < 10 && (i * 4 + 3) < binary_small->size; i++) {
            unsigned int val = binary_small->data[i*4] |
                              (binary_small->data[i*4+1] << 8) |
                              (binary_small->data[i*4+2] << 16) |
                              (binary_small->data[i*4+3] << 24);
            printf("%u ", val);
        }
        printf("\n\n");
        linear_buffer_free(binary_small);
    } else {
        printf("Failed to read binary file\n\n");
    }

    printf("Reading large binary file: test_big.dat\n");
    LinearBuffer *binary_big = read_binary_file("../Data/test_big.dat");
    if (binary_big != NULL) {
        printf("Successfully read %d bytes from large binary file\n", binary_big->size);
        printf("Number of integers: %d\n", binary_big->size / 4);
        printf("First 10 integers: ");
        for (int i = 0; i < 10 && (i * 4 + 3) < binary_big->size; i++) {
            unsigned int val = binary_big->data[i*4] |
                              (binary_big->data[i*4+1] << 8) |
                              (binary_big->data[i*4+2] << 16) |
                              (binary_big->data[i*4+3] << 24);
            printf("%u ", val);
        }
        printf("\n\n");
        linear_buffer_free(binary_big);
    } else {
        printf("Failed to read large binary file\n");
    }

    return 0;
}