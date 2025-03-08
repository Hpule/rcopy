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
    
    buffer.window_size = window_size;
    buffer.lower = 0;
    buffer.upper = window_size;
    buffer.current = 0;
    buffer.last_seq = 0;
}

// ---- Free Buffer Memory ----
void destroy_window_buffer() {
    if (buffer.entries) {
        for (int i = 0; i < buffer.window_size; i++) {
            if (buffer.entries[i].data) {
                free(buffer.entries[i].data);
            }
        }
        free(buffer.entries);
        buffer.entries = NULL;
    }
}

bool buffer_packet(uint32_t seq, uint8_t *data, int length, pdu_header header) {
    if (length > MAX_PAYLOAD) {
        fprintf(stderr, "Error: Packet size %d exceeds maximum buffer size %d\n", length, MAX_PAYLOAD);
        return false;
    }
    
    int index = seq % buffer.window_size;
    printf("buffer_packet: Attempting to buffer SEQ=%u at index=%d\n", seq, index);
    if (buffer.entries[index].valid) {
        printf("buffer_packet: Slot at index=%d is currently VALID with stored SEQ=%u\n",
               index, buffer.entries[index].sequence_number);
    } else {
        printf("buffer_packet: Slot at index=%d is EMPTY\n", index);
    }
    
    if (buffer.entries[index].valid && buffer.entries[index].sequence_number != seq) {
        fprintf(stderr, "Error: Buffer Overflow - SEQ %u is overwriting an unprocessed packet at index %d\n",
                seq, index);
        return false;
    }
    
    // Clear the slot before writing (to avoid residual data).
    memset(buffer.entries[index].data, 0, MAX_PAYLOAD);
    memcpy(buffer.entries[index].data, data, length);
    buffer.entries[index].sequence_number = seq;
    buffer.entries[index].valid = true;
    buffer.entries[index].length = length;
    memcpy(&buffer.entries[index].header, &header, sizeof(pdu_header));
    
    if (seq > buffer.last_seq) {
        buffer.last_seq = seq;
    }
    
    if (seq >= buffer.current) {
        buffer.current = seq + 1;
    }
    
    printf("buffer_packet: Successfully buffered SEQ=%u at index=%d\n", seq, index);
    return true;
}


// ---- Retrieve a Packet from Buffer ----
bool get_packet_from_window(uint32_t seq, pdu_header *header, void *data, size_t *dataSize) {
    int index = seq % buffer.window_size;
    if (!buffer.entries[index].valid || buffer.entries[index].sequence_number != seq) {
        return false;
    }
    
    if (header) {
        memcpy(header, &buffer.entries[index].header, sizeof(pdu_header));
    }
    if (data && dataSize) {
        *dataSize = buffer.entries[index].length;
        memcpy(data, buffer.entries[index].data, buffer.entries[index].length);
    }
    
    return true;
}

// ---- Flush Buffered Packets to File ----
void flush_buffer(FILE *outputFile, uint32_t *RR) {
    int index = *RR % buffer.window_size;
    printf("flush_buffer: Starting flush, current RR = %u\n", *RR);
    
    while (buffer.entries[index].valid && buffer.entries[index].sequence_number == *RR) {
        printf("flush_buffer: Flushing packet at index %d, SEQ=%d, Length=%d bytes\n",
               index, buffer.entries[index].sequence_number, buffer.entries[index].length);
        
        if (outputFile) {
            size_t bytesWritten = fwrite(buffer.entries[index].data, 1, buffer.entries[index].length, outputFile);
            fflush(outputFile);
            printf("flush_buffer: Wrote %zu bytes to file\n", bytesWritten);
        }
        
        buffer.entries[index].valid = false;  // Mark slot as empty.
        (*RR)++;
        index = *RR % buffer.window_size;
        printf("flush_buffer: Updated RR to %u, next index = %d\n", *RR, index);
    }
    
    printf("flush_buffer: Flush complete, final RR = %u\n", *RR);
}

// ---- Invalidate ACKed Packets ----
void invalidate_packets(uint32_t RR) {
    printf("invalidate_packets: Called with RR = %u, Current Lower = %u\n", RR, buffer.lower);
    while (buffer.lower < RR) {
        int index = buffer.lower % buffer.window_size;
        if (buffer.entries[index].valid) {
            printf("invalidate_packets: Clearing entry at index %d (SEQ=%d, Length=%d)\n",
                   index, buffer.entries[index].sequence_number, buffer.entries[index].length);
            buffer.entries[index].valid = false;
        } else {
            printf("invalidate_packets: Entry at index %d already empty\n", index);
        }
        buffer.lower++;
    }
    if (buffer.current < buffer.lower) {
        buffer.current = buffer.lower;
    }
    printf("invalidate_packets: After invalidation, Lower = %u, Upper = %u, Current = %u\n",
           buffer.lower, buffer.upper, buffer.current);
}

// ---- Check If Window is Full (for Sender) ----
bool is_window_full() {
    return (buffer.current - buffer.lower) >= buffer.window_size;
}

// ---- Advance Window (after receiving RR) ----
void update_window(uint32_t RR) {
    printf("update_window: Called with RR = %u. Old Lower = %u, Upper = %u\n",
           RR, buffer.lower, buffer.upper);
    if (RR > buffer.lower) {
        buffer.lower = RR;
        buffer.upper = buffer.lower + buffer.window_size;
        printf("update_window: Window updated: New Lower = %u, Upper = %u\n",
               buffer.lower, buffer.upper);
    } else {
        printf("update_window: No update needed, RR (%u) <= lower (%u)\n", RR, buffer.lower);
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

// ---- Print Circular Buffer with Expected Window Range ----
void print_window_buffer() {
    if (!buffer.entries) {
        printf("Buffer empty.\n");
        return;
    }
    
    printf("\n========= WINDOW BUFFER STATE =========\n");
    printf("Window Size: %d\n", buffer.window_size);
    printf("Lower: %u, Upper: %u\n", buffer.lower, buffer.upper);
    printf("Next window range: [%u - %u)\n", buffer.lower, buffer.lower + buffer.window_size);
    printf("Current (next seq to send): %u, Last Seq: %u\n", buffer.current, buffer.last_seq);
    
    // For each expected sequence number in the window, print the entry.
    for (int i = 0; i < buffer.window_size; i++) {
        uint32_t expected = buffer.lower + i;
        int index = expected % buffer.window_size;
        if (buffer.entries[index].valid && buffer.entries[index].sequence_number == expected)
            printf("Index %d: Expected %u, Stored %u, Length %d\n",
                   index, expected, buffer.entries[index].sequence_number, buffer.entries[index].length);
        else
            printf("Index %d: Expected %u, EMPTY\n", index, expected);
    }
    
    printf("=======================================\n\n");
}
