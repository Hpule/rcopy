#include "windowBuffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ---- Global Buffer ----
static window_buffer_t buffer;

// ---- Initialize Circular Buffer ----
void init_window_buffer(int window_size) {
    buffer.entries = (buffer_entry_t *)malloc(window_size * sizeof(buffer_entry_t));
    if (!buffer.entries) {
        fprintf(stderr, "Error: Memory allocation failed for window buffer\n");
        exit(EXIT_FAILURE);
    }
    
    // Allocate memory for data in each entry
    for (int i = 0; i < window_size; i++) {
        buffer.entries[i].data = (uint8_t *)malloc(MAX_DATA_SIZE);
        if (!buffer.entries[i].data) {
            fprintf(stderr, "Error: Memory allocation failed for buffer entry data\n");
            
            // Clean up previously allocated memory
            for (int j = 0; j < i; j++) {
                free(buffer.entries[j].data);
            }
            free(buffer.entries);
            exit(EXIT_FAILURE);
        }
        buffer.entries[i].valid = false;
    }
    
    buffer.window_size = window_size;
    buffer.lower = 0;
    buffer.upper = window_size;
    buffer.current = 0;
    buffer.last_seq = 0;
}

// ---- Free Buffer Memory ----
void destroy_window_buffer() {
    if (buffer.entries) {
        // Free data for each entry
        for (int i = 0; i < buffer.window_size; i++) {
            if (buffer.entries[i].data) {
                free(buffer.entries[i].data);
            }
        }
        free(buffer.entries);
        buffer.entries = NULL;
    }
}

// ---- Buffer a Packet ----
bool buffer_packet(uint32_t seq, uint8_t *data, int length, pdu_header header) {
    if (length > MAX_DATA_SIZE) {
        fprintf(stderr, "Error: Packet size %d exceeds maximum buffer size %d\n", 
                length, MAX_DATA_SIZE);
        return false;
    }
    
    int index = seq % buffer.window_size;
    
    if (buffer.entries[index].valid && buffer.entries[index].sequence_num != seq) {
        fprintf(stderr, "Error: Buffer Overflow - SEQ %d is overwriting an unprocessed packet at index %d\n",
                seq, index);
        return false;
    }
    
    // Store packet data
    memcpy(buffer.entries[index].data, data, length);
    buffer.entries[index].sequence_num = seq;
    buffer.entries[index].valid = true;
    buffer.entries[index].length = length;
    
    // Store header information for potential retransmission
    memcpy(&buffer.entries[index].header, &header, sizeof(pdu_header));
    
    // Update last_seq if this is a higher sequence number
    if (seq > buffer.last_seq) {
        buffer.last_seq = seq;
    }
    
    // Update current pointer for window management
    if (seq >= buffer.current) {
        buffer.current = seq + 1;
    }
    
    printf("Buffered SEQ=%d at index=%d\n", seq, index);
    return true;
}

// ---- Retrieve a Packet from Buffer ----
bool get_packet_from_window(uint32_t seq, pdu_header *header, void *data, size_t *dataSize) {
    int index = seq % buffer.window_size;
    
    if (!buffer.entries[index].valid || buffer.entries[index].sequence_num != seq) {
        return false; // Packet not found
    }
    
    // Copy header if requested
    if (header) {
        memcpy(header, &buffer.entries[index].header, sizeof(pdu_header));
    }
    
    // Copy data if requested
    if (data && dataSize) {
        *dataSize = buffer.entries[index].length;
        memcpy(data, buffer.entries[index].data, buffer.entries[index].length);
    }
    
    return true;
}

// ---- Flush Buffered Packets to File ----
void flush_buffer(FILE *outputFile, uint32_t *RR) {
    int index = *RR % buffer.window_size;
    
    while (buffer.entries[index].valid && buffer.entries[index].sequence_num == *RR) {
        printf("Flushing SEQ=%d to file (index=%d)\n", buffer.entries[index].sequence_num, index);
        
        if (outputFile) {
            fwrite(buffer.entries[index].data, 1, buffer.entries[index].length, outputFile);
            fflush(outputFile);
        }
        
        buffer.entries[index].valid = false;  // Mark slot as empty
        (*RR)++;
        index = *RR % buffer.window_size;
    }
}

// ---- Invalidate ACKed Packets ----
void invalidate_packets(uint32_t RR) {
    while (buffer.lower < RR) {
        int index = buffer.lower % buffer.window_size;
        if (buffer.entries[index].valid) {
            buffer.entries[index].valid = false;
        }
        buffer.lower++;
    }
    
    // Update current pointer if needed
    if (buffer.current < buffer.lower) {
        buffer.current = buffer.lower;
    }
}

// ---- Check If Window is Full (for Sender) ----
bool is_window_full() {
    return (buffer.current - buffer.lower) >= buffer.window_size;
}

// ---- Advance Window (after receiving RR) ----
void update_window(uint32_t RR) {
    // Update lower bound of window
    if (RR > buffer.lower) {
        buffer.lower = RR;
        
        // Update upper bound based on new lower bound
        buffer.upper = buffer.lower + buffer.window_size;
        
        printf("Window updated: Lower=%d, Upper=%d\n", buffer.lower, buffer.upper);
    }
}

// ---- Get Lowest Unacknowledged SEQ (for Sender) ----
uint32_t get_lowest_unack_seq() {
    return buffer.lower;
}

// ---- Get Next Sequence Number to Send ----
uint32_t get_next_seq_to_send() {
    return buffer.current;
}

// ---- Check if Sequence is Within Window ----
bool is_seq_in_window(uint32_t seq) {
    return (seq >= buffer.lower && seq < buffer.upper);
}

void print_window_buffer() {
    if (buffer.entries == NULL) {
        printf("Window Buffer Empty\n");
        return;
    }
    
    printf("\n========= WINDOW BUFFER STATE =========\n");
    printf("Window Size: %d\n", buffer.window_size);
    printf("Lower: %d, Upper: %d\n", buffer.lower, buffer.upper);
    printf("Next window range: [%d - %d)\n", buffer.lower, buffer.lower + buffer.window_size);
    printf("Current (next seq to send): %d, Last Seq: %d\n", buffer.current, buffer.last_seq);
    
    // For each position in the window, calculate the expected sequence number.
    for (int i = 0; i < buffer.window_size; i++) {
        int expected_seq = buffer.lower + i;
        int index = expected_seq % buffer.window_size;
        // Check if the slot holds the packet with the expected sequence.
        if (buffer.entries[index].valid && buffer.entries[index].sequence_num == expected_seq) {
            printf("[Index %d] Expected SEQ=%d | Stored SEQ=%d | Length=%d | VALID\n",
                   index, expected_seq, buffer.entries[index].sequence_num, buffer.entries[index].length);
        } else {
            printf("[Index %d] Expected SEQ=%d | EMPTY\n", index, expected_seq);
        }
    }
    
    printf("=======================================\n\n");
}