#include "helperFunctions.h"
#include "cpe464.h"     // For in_cksum()
#include "safeUtil.h"   // For sendtoErr()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

void printHexDump(const char *label, const char *buffer, int len) {
    if (label && label[0] != '\0')
        printf("%s (len=%d):\n", label, len);
    else
        printf("Hex dump (len=%d):\n", len);
    for (int i = 0; i < len; i++) {
        printf("%02x ", (unsigned char)buffer[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    printf("\n");
}

int sendPdu(int sock, struct sockaddr_in6 *dest, pdu_header header, const char *payload, int payload_len) {
    int header_size = sizeof(pdu_header);
    int packet_len = header_size + payload_len;
    char packet[MAX_PDU_SIZE];
    
    if(packet_len > MAX_PDU_SIZE) {
        fprintf(stderr, "Error: packet length (%d) exceeds maximum (%d)\n", packet_len, MAX_PDU_SIZE);
        return -1;	
    }
    
    // Ensure destination address is valid
    if (dest == NULL) {
        fprintf(stderr, "Error: Invalid destination address in sendPdu()\n");
        return -1;
    }
    
    // Create a local copy of the header.
    pdu_header temp_header = header;
    
    // Copy header and payload into the packet.
    memcpy(packet, &temp_header, header_size);
    if (payload != NULL && payload_len > 0) {
        memcpy(packet + header_size, payload, payload_len);
    }
    
    // Compute checksum over the entire packet.
    uint16_t cksum = in_cksum((unsigned short *)packet, packet_len);
    temp_header.checksum = cksum;
    memcpy(packet, &temp_header, header_size);
    
    // Optionally print the hex dump.
    // printHexDump("", packet, packet_len);
    
    int destLen = sizeof(struct sockaddr_in6);
    int sent = sendtoErr(sock, packet, packet_len, 0, (struct sockaddr *)dest, destLen);
    if (sent < 0) {
        perror("sendtoErr");
        return -1;
    }
    return sent;
}

int sendAck(int sock, struct sockaddr_in6 *dest, uint32_t seq, const char *ackPayload) {
    pdu_header ack;
    ack.seq = htonl(seq);  // Use the given sequence number.
    ack.flag = 9;          // ACK flag.
    ack.checksum = 0;
    int payload_len = (ackPayload != NULL) ? strlen(ackPayload) : 0;
    return sendPdu(sock, dest, ack, ackPayload, payload_len);
}