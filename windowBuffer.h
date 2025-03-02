#ifndef WINDOW_BUFFER_H
#define WINDOW_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "helperFunctions.h"

// Data structure for a buffered packet
typedef struct {
    uint8_t data[MAXBUF];
    uint32_t sequence_num;
    bool valid;
    int length;
} buffer_entry_t;

// Circular Buffer Structure
typedef struct {
    buffer_entry_t *entries;  // Dynamic array of buffer entries
    int window_size;
    int lower;   // Lowest unacknowledged packet
    int upper;   // Highest allowed in window
    int current; // Current sequence number
} window_buffer_t;

// ---- Function Prototypes ----

// Initialize the buffer (malloc'd array of `window_size`)
void init_window_buffer(int window_size);

// Destroy the buffer (free allocated memory)
void destroy_window_buffer();

// Add a packet to the buffer (for out-of-order storage)
void buffer_packet(uint32_t seq, uint8_t *data, int length);

// Flush the buffer when `RR` is incremented
void flush_buffer(FILE *outputFile, uint32_t *RR);

// Invalidate packets that are ACKed
void invalidate_packets(uint32_t RR);

// Check if window is full (for sender)
bool is_window_full();

// Get the current lowest unacknowledged SEQ (for sender)
uint32_t get_lowest_unack_seq();

// Print window buffer state (debugging)
void print_window_buffer();

#endif  // WINDOW_BUFFER_H
