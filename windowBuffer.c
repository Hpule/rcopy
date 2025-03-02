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

    // Initialize all buffer slots as invalid
    for (int i = 0; i < window_size; i++) {
        buffer.entries[i].valid = false;
    }
}

// ---- Free Buffer Memory ----
void destroy_window_buffer() {
    if (buffer.entries) {
        free(buffer.entries);
        buffer.entries = NULL;
    }
}

// ---- Buffer a Packet ----
void buffer_packet(uint32_t seq, uint8_t *data, int length) {
    int index = seq % buffer.window_size;

    if (buffer.entries[index].valid && buffer.entries[index].sequence_num != seq) {
        fprintf(stderr, "Error: Buffer Overflow - SEQ %d is overwriting an unprocessed packet at index %d\n",
                seq, index);
        return;
    }

    memcpy(buffer.entries[index].data, data, length);
    buffer.entries[index].sequence_num = seq;
    buffer.entries[index].valid = true;
    buffer.entries[index].length = length;

    printf("Buffered SEQ=%d at index=%d\n", seq, index);
}

// ---- Flush Buffered Packets to File ----
void flush_buffer(FILE *outputFile, uint32_t *RR) {
    int index = *RR % buffer.window_size;

    while (buffer.entries[index].valid) {
        printf("Flushing SEQ=%d to file (index=%d)\n", buffer.entries[index].sequence_num, index);

        fwrite(buffer.entries[index].data, 1, buffer.entries[index].length, outputFile);
        fflush(outputFile);

        buffer.entries[index].valid = false;  // Mark slot as empty
        (*RR)++;
        index = *RR % buffer.window_size;
    }
}

// ---- Invalidate ACKed Packets ----
void invalidate_packets(uint32_t RR) {
    for (int i = 0; i < buffer.window_size; i++) {
        if (buffer.entries[i].valid && buffer.entries[i].sequence_num < RR) {
            buffer.entries[i].valid = false;
        }
    }
}

// ---- Check If Window is Full (for Sender) ----
bool is_window_full() {
    return buffer.current >= buffer.upper;
}

// ---- Get Lowest Unacknowledged SEQ (for Sender) ----
uint32_t get_lowest_unack_seq() {
    return buffer.lower;
}

// ---- Print Window Buffer (For Debugging) ----
void print_window_buffer() {
    if (buffer.entries == NULL) {
        printf("Window Buffer Empty\n");
        return;
    }

    printf("\n========= WINDOW BUFFER STATE =========\n");
    printf("Window Size: %d | Lower: %d | Upper: %d | Current: %d\n",
           buffer.window_size, buffer.lower, buffer.upper, buffer.current);

    for (int i = 0; i < buffer.window_size; i++) {
        if (buffer.entries[i].valid) {
            printf("[Index %d] SEQ=%d | VALID\n", i, buffer.entries[i].sequence_num);
        } else {
            printf("[Index %d] EMPTY\n", i);
        }
    }
    printf("=======================================\n\n");
}
