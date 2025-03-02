#ifndef HELPER_FUNCTIONS_H
#define HELPER_FUNCTIONS_H

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

// Define the PDU header structure.
typedef struct __attribute__((packed)) {
    uint32_t seq;
    uint16_t checksum;
    uint8_t flag;
} pdu_header;

#define HEADER_SIZE sizeof(pdu_header)
#define MAX_PACKET_SIZE 256
#define MAXBUF 1400
#define MAX_FILENAME 100
#define POLL_TIMEOUT 1000
#define POLL_ONE_SEC 1000
#define POLL_TEN_SEC 10000
#define MAX_ATTEMPTS 10

// Print a hex dump of a buffer.
void printHexDump(const char *label, const char *buffer, int len);

// Assemble and send a PDU packet. Returns the number of bytes sent, or -1 on error.
int sendPdu(int sock, struct sockaddr_in6 *dest, pdu_header header, const char *payload, int payload_len);

// Send an ACK packet with flag 9 (with a payload such as "Ok" or "Not Ok").
// Returns the number of bytes sent, or -1 on error.
int sendAck(int sock, struct sockaddr_in6 *dest, uint32_t seq, const char *ackPayload);

int validateChecksum(uint8_t *buffer, int dataLen); 

#endif // HELPER_FUNCTIONS_H
