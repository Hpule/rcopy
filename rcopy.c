#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <math.h>
#include <stdbool.h>


#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "cpe464.h"
#include "pollLib.h"
#include "windowBuffer.h"
#include "helperFunctions.h"

// ----- Rcopy FSM States ----- 
typedef enum {
    STATE_INIT,
    STATE_HANDSHAKE,
    STATE_FILE_RECEIVE,
    STATE_DONE
} rcopy_state_t;



// ----- Rcopy Context Structure ----- 
typedef struct {
    int socketNum;
    char *server_filename;
    char *rcopy_filename; 
    int windowsize;
    int buffersize;
    double error_rate; 
    struct sockaddr_in6 server;  // Changed from pointer to struct
    int portNumber;
    char *remoteMachine;
    int attempts;
    bool ackReceived;
    bool eof; 
} RcopyContext;

typedef struct {
    FILE *outputFile;
    WindowBuffer buffer;
    uint32_t expectedSeq;
    int attempts;
    bool complete;
} ReceiveState;


// ----- Other stuctures ----- 
// Circular Buffer = new circular buffer 

// ----- FSM Functions -----
rcopy_state_t stateInit(RcopyContext *rcopy);
rcopy_state_t stateHandshake(RcopyContext *rcopy);
rcopy_state_t stateFileReceive(RcopyContext *rcopy);

// ----- Rcopy Functions  -----
int checkArgs(int argc, char * argv[]);

void rcopyFSM(RcopyContext *rcopy);
void sendFilename(int socketNum, struct sockaddr_in6 *server, char* filename, int buffersize, int windowsize);

void initReceiveState(ReceiveState *state, int windowSize, int bufferSize, FILE *file); 
bool receivePacket(RcopyContext *rcopy, ReceiveState *state); 
bool handlePacketByType(RcopyContext *rcopy, ReceiveState *state, pdu_header *header, char *packet, int packetLen); 
bool processDataPacket(RcopyContext *rcopy, ReceiveState *state, char *packet, int packetLen, uint32_t seq); 

void sendRR(int RR, RcopyContext *rcopy); 
void sendSREJ(int SREJ, RcopyContext *rcopy); 
void sendEOF(uint32_t seqNum, RcopyContext *rcopy); 

void cleanup(RcopyContext *rcopy);




// ----- Main Function -----
int main(int argc, char *argv[])
{
    RcopyContext rcopy;
    memset(&rcopy, 0, sizeof(RcopyContext));
    rcopy.portNumber = checkArgs(argc, argv);
    rcopy.server_filename = argv[1];
    rcopy.rcopy_filename = argv[2];
    rcopy.windowsize = atoi(argv[3]);
    rcopy.buffersize = atoi(argv[4]);
    rcopy.error_rate = atof(argv[5]);  // Fix: Use atof instead of atoi for double values.
    rcopy.remoteMachine = argv[6];

    setupPollSet();  // Initialize poll set.
    rcopyFSM(&rcopy);
    return 0;
}

int checkArgs(int argc, char * argv[])
{
	int portNumber = 0;
	printf("argc: %d", argc);
    
    /* Check command-line arguments */
    if (argc != 8) {
        fprintf(stderr, "Usage: %s <from-filename> <to-filename> <window-size> <buffer-size> <error-rate> <remote-machine> <remote-port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Check that the from-filename is less than MAX_FILENAME_LENGTH characters
    if (strlen(argv[1]) >= MAX_FILENAME) {
        fprintf(stderr, "Error: from-filename '%s' is too long. Maximum allowed is %d characters.\n", argv[1], MAX_FILENAME - 1);
        exit(EXIT_FAILURE);
    }

    // Check that the to-filename is less than MAX_FILENAME_LENGTH characters
    if (strlen(argv[2]) >= MAX_FILENAME) {
        fprintf(stderr, "Error: to-filename '%s' is too long. Maximum allowed is %d characters.\n", argv[2], MAX_FILENAME - 1);
        exit(EXIT_FAILURE);
    }

	    // Check that the to-filename is less than MAX_FILENAME_LENGTH characters
    if (atoi(argv[3]) > pow(2,30)) {
        fprintf(stderr, "Error: window-size '%s' is too large. Max allowed is 2 ^ 30.\n", argv[3]);
        exit(EXIT_FAILURE);
    }
	
	double errorRate = 0.0; 
    errorRate = atof(argv[5]); 
    if(errorRate < 0 || errorRate > 1)
    {
        fprintf(stderr, "Error: %s <error-rate> must be between 0.0 and 1.0\n", argv[5]); 
        exit(EXIT_FAILURE); 
    }
    
    portNumber = atoi(argv[7]);
    return portNumber;
}

void rcopyFSM(RcopyContext *rcopy)
{
    rcopy_state_t state = STATE_INIT;

    while (state != STATE_DONE) {
        switch (state) {
            case STATE_INIT:
                printf("----- FSM: STATE_INIT -----\n"); 

                state = stateInit(rcopy);
                break;

            case STATE_HANDSHAKE:
                printf("----- FSM: STATE_HANDSHAKE -----\n"); 
                state = stateHandshake(rcopy);
                break;

            case STATE_FILE_RECEIVE:
                printf("----- FSM: STATE_FILE_RECEIVE -----\n"); 
                state = stateFileReceive(rcopy);
                break;
                
            default:
                printf("----- FSM: DEFAULT -----\n"); 
                state = STATE_DONE;
                break;
        }
    }

    printf("----- File Transfer done! -----\n"); 
    cleanup(rcopy);
}

rcopy_state_t stateInit(RcopyContext *rcopy)
{
    rcopy->socketNum = setupUdpClientToServer(&rcopy->server, rcopy->remoteMachine, rcopy->portNumber);
    addToPollSet(rcopy->socketNum);
    sendErr_init(rcopy->error_rate, DROP_ON, FLIP_OFF, DEBUG_ON, RSEED_ON);
    return STATE_HANDSHAKE;
}

void cleanup(RcopyContext *rcopy)
{
    removeFromPollSet(rcopy->socketNum);
    close(rcopy->socketNum);
    printf("Client shutdown complete.\n");
}

rcopy_state_t stateHandshake(RcopyContext *rcopy)
{
    rcopy->attempts = 0;
    char pdu[MAX_PDU_SIZE];
    struct sockaddr_in6 src;
    socklen_t srcLen = sizeof(src);

    while (rcopy->attempts < MAX_ATTEMPTS) {
        // Send filename packet.
        sendFilename(rcopy->socketNum, &rcopy->server, rcopy->server_filename, rcopy->buffersize, rcopy->windowsize);
        printf("Handshake attempt %d...\n", rcopy->attempts + 1);

         if (pollCall(POLL_ONE_SEC) >= 0) {
            int pduLen = safeRecvfrom(rcopy->socketNum, pdu, MAX_PDU_SIZE, 0, (struct sockaddr *)&src, (int *)&srcLen);
            if (pduLen > 0) {
                // Read header from received data.
                pdu_header header;
                memcpy(&header, pdu, HEADER_SIZE);

                switch (header.flag) {
                    case 9:  // ACK flag
                        printf("Received ACK from server.\n");

                        int payload_len = pduLen - HEADER_SIZE;
                        char ackPayload[MAX_PAYLOAD - HEADER_SIZE + 1] = {0};
                        if (payload_len > 0) {
                            memcpy(ackPayload, pdu + HEADER_SIZE, payload_len);
                            ackPayload[payload_len] = '\0';
                            printf("ACK Payload: \"%s\"\n", ackPayload);
                        }

                        if (strcmp(ackPayload, "Ok") == 0) { 
                            memcpy(&rcopy->server, &src, sizeof(struct sockaddr_in6));
                            uint16_t port = ntohs(rcopy->server.sin6_port);
                            printf("Handshake successful! Proceeding to file reception at port: %u\n", port); 
                            return STATE_FILE_RECEIVE;
                        } 
                        else { 
                            perror("Server responded with 'Not Ok'. Terminating.\n");
                            close(rcopy->socketNum); 
                            exit(EXIT_FAILURE); 
                        }
                    default:
                        printf("Unexpected packet (flag=%d) received. Ignoring.\n", header.flag);
                        break;
                }
            }
        }

        rcopy->attempts++;
        printf("No ACK received. Retrying handshake...\n");
        close(rcopy->socketNum);
        rcopy->socketNum = setupUdpClientToServer(&rcopy->server, rcopy->remoteMachine, rcopy->portNumber);
        pollCall(POLL_ONE_SEC);  // Backoff delay.
    }
    printf("Max handshake attempts reached. Terminating.\n");
    return STATE_DONE;
}

void sendFilename(int socketNum, struct sockaddr_in6 *server, char* filename, int buffersize, int windowsize)
{
    char pdu[MAX_PDU_SIZE - HEADER_SIZE];
    int pduLen = 0;
    uint8_t filename_len = strlen(filename);    
    uint32_t buffSize = htonl(buffersize);
    uint32_t windSize = htonl(windowsize);

    // ----- Window Size -----
    memcpy(pdu + pduLen, &windSize, sizeof(uint32_t));
    pduLen += sizeof(uint32_t);

    // ----- Buffer Size -----
    memcpy(pdu + pduLen, &buffSize, sizeof(uint32_t));
    pduLen += sizeof(uint32_t);

    // ----- Payload ----- 
    pdu[pduLen++] = filename_len;
    memcpy(pdu + pduLen, filename, filename_len);
    pduLen += filename_len;   
    
    pdu_header header;
    header.seq = htonl(0);
    header.flag = 8; 
    header.checksum = 0;
    
    int bytesSent = sendPdu(socketNum, server, header, pdu, pduLen);
    if (bytesSent < 0) {
        fprintf(stderr, "sendPdu failed in sendFilename\n"); 
        close(socketNum); 
        exit(EXIT_FAILURE);
    }
    printf("Sent filename packet: %s\n", filename);
}

rcopy_state_t stateFileReceive(RcopyContext *rcopy) {
    FILE *outputFile = fopen(rcopy->rcopy_filename, "wb");
    if(!outputFile) {
        perror("Error opening file for writing");
        close(rcopy->socketNum); 
        exit(EXIT_FAILURE); 
    }
    
    WindowBuffer wb;            init_window(&wb, rcopy->windowsize, rcopy->buffersize, NULL);
    
    uint32_t expectedSeq = 0;   rcopy->attempts = 0;
    
    while(!rcopy->eof && rcopy->attempts < MAX_ATTEMPTS) {
        int pollResult = pollCall(POLL_ONE_SEC);
        
        if(pollResult <= 0) {
            rcopy->attempts++;
            printf("Timeout %d/%d\n", rcopy->attempts, MAX_ATTEMPTS);
            sendRR(expectedSeq, rcopy); // Resend current RR on timeout
            continue;
        }
        rcopy->attempts = 0;
        
        // Receive and check packet
        char packet[MAX_PDU_SIZE];
        struct sockaddr_in6 src;
        socklen_t srcLen = sizeof(src);
        int packetLen = safeRecvfrom(rcopy->socketNum, packet, MAX_PDU_SIZE, 0, (struct sockaddr*)&src, (int*)&srcLen);
        
        if(packetLen < 0 || in_cksum((uint16_t*)packet, packetLen) != 0) {
            sendSREJ(expectedSeq, rcopy);
            continue;
        }
        
        // Process packet
        pdu_header header;
        memcpy(&header, packet, HEADER_SIZE);
        uint32_t seqNum = ntohl(header.seq);
        
        if(header.flag == 10) { // EOF
            if(seqNum == expectedSeq) {
                printf("EOF received and acknowledged\n");
                fwrite(packet + HEADER_SIZE, 1, packetLen - HEADER_SIZE, outputFile);
                sendEOF(seqNum, rcopy);
                rcopy->eof = true;
            } else {
                sendSREJ(expectedSeq, rcopy);
            }
        }
        else if(seqNum >= expectedSeq && seqNum < expectedSeq + rcopy->windowsize) {
            uint32_t index = seqNum % wb.window_size;
            wb.panes[index].seq_num = seqNum;
            memcpy(wb.panes[index].data, packet + HEADER_SIZE, packetLen - HEADER_SIZE);
            
            // Process all contiguous packets
            if(seqNum == expectedSeq) {
                // Write current packet and all consecutive buffered packets
                while(wb.panes[expectedSeq % wb.window_size].seq_num == expectedSeq) {
                    printf("Writing packet %u to file\n", expectedSeq);
                    fwrite(wb.panes[expectedSeq % wb.window_size].data, 1, wb.buffer_size, outputFile);
                    expectedSeq++;
                }
                sendRR(expectedSeq, rcopy);
            } else {
                // Out-of-order packet - request missing packet
                printf("Out-of-order packet %u, expecting %u\n", seqNum, expectedSeq);
                sendSREJ(expectedSeq, rcopy);
            }
        } else if(seqNum < expectedSeq) {
            printf("Duplicate packet %u\n", seqNum);        sendRR(expectedSeq, rcopy);
        } else {
            printf("Packet %u outside window\n", seqNum);   sendSREJ(expectedSeq, rcopy);
        }
    }
    fclose(outputFile);     free_window(&wb);       return STATE_DONE;
}

void sendRR(int RR, RcopyContext *rcopy) {
    pdu_header rr; 
    rr.seq = htonl(RR); 
    rr.flag = 5; 
    rr.checksum = 0; 
    sendPdu(rcopy->socketNum, &rcopy->server, rr, "RR", strlen("RR"));
}

void sendSREJ(int SREJ, RcopyContext *rcopy) {
    pdu_header srej; 
    srej.seq = htonl(SREJ); 
    srej.flag = 6; 
    srej.checksum = 0; 
    sendPdu(rcopy->socketNum, &rcopy->server, srej, "SREJ", strlen("SREJ"));
}

void sendEOF(uint32_t seqNum, RcopyContext *rcopy) {
    pdu_header eof; 
    eof.seq = htonl(0); 
    eof.flag = 10; 
    eof.checksum = 0; 
    sendPdu(rcopy->socketNum, &rcopy->server, eof, "EOF_ACK", strlen("EOF_ACK"));
    printf("Sent EOF ACK with seq=%u\n", seqNum);

}

