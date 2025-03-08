#ifndef WINDOW_BUF_H
#define WINDOW_BUF_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <netinet/in.h>


#define MAXSIZE 1407

typedef enum State {
    DONE, FILENAME, FILE_OK, START_STATE, SEND_DATA, DATA_RESEND, SENT_EOF, WAIT_ACK, RECV_EOF_ACK, RCV_DATA
} STATE;

// Define the Packet structure
typedef struct {
    uint32_t sequence_number;
    uint8_t payload[MAXSIZE]; // Assuming payload size is fixed at 1400 characters
    uint16_t payload_size;
} Packet;

// Define the CircularBuffer structure
typedef struct {
    Packet* packets;    // Array of packets
    int capacity;       // Capacity of the buffer
    uint32_t lower;          // Lower boundary of the circular buffer
    uint32_t current;        // Current position within the circular buffer
    uint32_t upper;          // Upper boundary of the circular buffer
    int sequence_value; // Current sequence number
    bool window_open;   // checking if buffer is open
} CircularBuffer;

// Function declarations
CircularBuffer* initCircularBuffer(int capacity, uint16_t buffer_size);
void freeCircularBuffer(CircularBuffer* buffer);
void writeCircularBuffer(CircularBuffer* buffer, uint8_t* payload, int payload_length);
void dequeue_lowest(CircularBuffer* buffer);
void printCircularBuffer(CircularBuffer* buffer);
void shift_window(CircularBuffer* buffer, int amount);
int createPDU(uint8_t* pdu_buffer, uint32_t sequence_number, uint8_t flag, uint8_t* payload, int payload_len);
void printPDU(uint8_t* aPDU, int pduLength);
bool is_full(CircularBuffer* buffer);
uint8_t* lowest_pdu_payload(CircularBuffer* buffer);
void print_luc(CircularBuffer * buffer);

#endif /* WINDOW_BUF_H */
