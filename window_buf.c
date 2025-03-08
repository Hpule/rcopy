#include "window_buf.h"


CircularBuffer* initCircularBuffer(int capacity, uint16_t buffer_size) {
    CircularBuffer* buffer = malloc(sizeof(CircularBuffer));
    buffer->packets = malloc(capacity * sizeof(Packet)); // Allocate memory for packets
    buffer->capacity = capacity;
    buffer->lower = 0;
    buffer->current = 0;
    buffer->upper = capacity;
    buffer->sequence_value = 0; // Initialize global sequence number
    buffer->window_open = true; // Assuming buffer is initially open

    // Initialize each packet's payload size
    for (int i = 0; i < capacity; i++) {
        buffer->packets[i].payload_size = buffer_size;
    }

    return buffer;
}

// Function to free memory allocated for the circular buffer
void freeCircularBuffer(CircularBuffer* buffer) {
    free(buffer->packets); // Free memory for packets
    free(buffer);
}

// Function to write a packet to the circular buffer
void writeCircularBuffer(CircularBuffer* buffer, uint8_t* payload, int payload_length) {
    // Write the packet into the buffer
    int index = buffer->current % buffer->capacity;

    // Reset payload buffer before copying new data
    memset(buffer->packets[index].payload, 0, MAXSIZE);
    
    // Copy new data into payload buffer
    memcpy(buffer->packets[index].payload, payload, payload_length);
    buffer->packets[index].sequence_number = buffer->sequence_value;
    buffer->packets[index].payload_size = payload_length;

    // Increment the sequence value and current position
    buffer->sequence_value++;
    buffer->current++;
    //printf("WINDOW: the buffer current is this: %d\n", buffer->current);
}

// Function to print the circular buffer
void printCircularBuffer(CircularBuffer* buffer) {
    printf("Printing Circular Buffer:\n");
    for (int i = 0; i < buffer->capacity; i++) {
        int index = (buffer->lower + i) % buffer->capacity;
        printf("Index #%d: ", index);
        printf("Received Seq num from rcopy: %d\n", buffer->packets[index].sequence_number);
    }
    printf("Lower: %d, Current: %d, Upper: %d\n", buffer->lower, buffer->current, buffer->upper);
}

void print_luc(CircularBuffer* buffer)
{
    printf("Lower: %d, Current: %d, Upper: %d\n", buffer->lower, buffer->current, buffer->upper);
}

// Function to increase the lower boundary of the circular buffer by a given value
void shift_window(CircularBuffer* buffer, int amount) 
{
    buffer->lower += amount;
    buffer->upper += amount;
}

bool is_full(CircularBuffer* buffer) 
{
    return buffer->current == buffer->upper;
}

uint8_t* lowest_pdu_payload(CircularBuffer* buffer) 
{
    int index = buffer->lower % buffer->capacity;
    //printf("index is: %d\n", index);
    return buffer->packets[index].payload;
}