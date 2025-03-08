#ifndef WINDOWBUFFER_H
#define WINDOWBUFFER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "helperFunctions.h"  // Assuming this defines pdu_header and MAX_DATA_SIZE

// Define the buffer entry structure.
typedef struct {
    uint32_t sequence_number;
    uint8_t *data;         // Dynamically allocated payload buffer
    int length;            // Length of the stored payload
    bool valid;
    pdu_header header;     // Header for potential retransmission
} buffer_entry_t;

// Define the window buffer structure.
typedef struct {
    buffer_entry_t *entries;    // Array of buffer entries (packets)
    int window_size;            // Window size (number of packets)
    uint32_t lower;             // Lower bound of the window (smallest unacknowledged sequence)
    uint32_t current;           // Next sequence number to send
    uint32_t upper;             // Upper bound of the window
    uint32_t last_seq;          // Highest sequence number stored so far
} window_buffer_t;

// Function declarations:
void init_window_buffer(int window_size);
void destroy_window_buffer();
bool buffer_packet(uint32_t seq, uint8_t *data, int length, pdu_header header);
bool get_packet_from_window(uint32_t seq, pdu_header *header, void *data, size_t *dataSize);
void flush_buffer(FILE *outputFile, uint32_t *RR);
void invalidate_packets(uint32_t RR);
bool is_window_full();
void update_window(uint32_t RR);
uint32_t get_lowest_unack_seq();
uint32_t get_next_seq_to_send();
bool is_seq_in_window(uint32_t seq);
void print_window_buffer();
void print_luc(window_buffer_t *buffer);  // Your existing print, if needed

#endif /* WINDOWBUFFER_H */
