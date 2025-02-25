#ifndef WINDOW_BUFFER_H
#define WINDOW_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

// Define the Packet structure.
// Adjust the data field size as needed; here we use 1408 bytes to allow for up to 1407 bytes of data.
typedef struct {
    uint32_t seq_num;   // Packet sequence number.
    int len;            // Length of valid data.
    char data[1408];    // Data buffer.
    bool acked;         // True if the packet has been acknowledged.
} Packet;

// Define the WindowBuffer structure.
typedef struct {
    Packet *buffer;     // Dynamically allocated array of Packet structures.
    int size;           // Capacity of the window (window size).
    int count;          // Current number of packets stored.
    // Note: We do not expose head/tail directly outside this library.
} WindowBuffer;

// API function prototypes:

// Create and initialize a window buffer with the given capacity.
WindowBuffer *wb_create(int capacity);

// Free the window buffer.
void wb_destroy(WindowBuffer *wb);

// Returns true if the buffer is full.
bool wb_isFull(WindowBuffer *wb);

// Returns true if the buffer is empty.
bool wb_isEmpty(WindowBuffer *wb);

// Inserts a packet into the buffer at index = (seq_num % capacity).
// This overwrites any existing packet at that index.
// Returns true on success.
bool wb_insert(WindowBuffer *wb, uint32_t seq_num, const char *data, int data_len);

// Retrieves (peeks) a packet from the buffer by sequence number (using seq_num % capacity).
// Returns true if the packet exists and is valid.
bool wb_get(WindowBuffer *wb, uint32_t seq_num, Packet *packet);

// Removes (invalidates) a packet from the buffer by sequence number.
// Returns true if a packet was removed.
bool wb_remove(WindowBuffer *wb, uint32_t seq_num);

// Marks the packet at index (seq_num % capacity) as acknowledged.
// Returns true if successful.
bool wb_markAcked(WindowBuffer *wb, uint32_t seq_num);

// For debugging: prints the contents of the window buffer.
void wb_print(WindowBuffer *wb);

#endif // WINDOW_BUFFER_H
