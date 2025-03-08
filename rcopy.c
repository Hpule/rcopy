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

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "cpe464.h"
#include "pollLib.h"
// #include "windowBuffer.h"
#include "window_buf.h"
#include "helperFunctions.h"

// ----- Rcopy FSM States ----- 
typedef enum {
    STATE_INIT,
    STATE_HANDSHAKE,
    STATE_FILE_RECEIVE,
    STATE_RETRY,
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

// ----- Other stuctures ----- 
// Circular Buffer = new circular buffer 

// ----- FSM Functions -----
rcopy_state_t stateInit(RcopyContext *rcopy);
rcopy_state_t stateHandshake(RcopyContext *rcopy);
rcopy_state_t stateFileReceive(RcopyContext *rcopy);
rcopy_state_t stateRetry(RcopyContext *rcopy);

// ----- Rcopy Functions  -----
void runRcopyFSM(RcopyContext *rcopy);
void cleanup(RcopyContext *rcopy);
void sendFilename(int socketNum, struct sockaddr_in6 *server, char* filename, int buffersize, int windowsize);
void sendRR(int RR, RcopyContext *rcopy); 
    void sendSREJ(int RR, RcopyContext *rcopy); 
void sendEOF(); 
int checkArgs(int argc, char * argv[]);


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
    runRcopyFSM(&rcopy);
    return 0;
}

// ----- FSM Loop -----
void runRcopyFSM(RcopyContext *rcopy)
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
                
            case STATE_RETRY:
                printf("----- FSM: STATE_RETRY -----\n"); 
                state = stateRetry(rcopy);
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

    // Array Buffer 

    // ----- Start Recieving data ----- 
    // While we don't get an EOF
    // if we get an expected packet
    //      send RR to server, increase rr to next data packet we want
    //      Write to file - should remove item from buffer. 
    //      
    // else if a packet if larger than the one we expect
    //      send SREJ of the packet that we are missing
    //      Buffer any data recieved until we get missing data packet
    //      increase attempts
    // If we get an EOF
    //      send EOF ack
    //      close file
    //      Change state to done
    // 
    // If poll times out 
    //      print error 
    //      next state is done 
    // If we get duplicate data 
    //      send Ack
    //      poll for 10 secs


    rcopy_state_t stateFileReceive(RcopyContext *rcopy){
        printf("----- RCOPY: STATE FILE RECEIVE -----\n"); 
    
        struct sockaddr_in6 src; 
        socklen_t srcLen = sizeof(src);
        char dataPacket[MAX_PDU_SIZE]; 
        uint32_t RR = 0; // Expected sequence number
       
        char filePath[256];
        snprintf(filePath, sizeof(filePath), "rcopy_download_files/%s.txt", rcopy->rcopy_filename);
    
        FILE *outputFile = fopen(filePath, "wb");
        if(!outputFile){
            perror("Error opening file for writing");
            return STATE_DONE;    
        } 
    
        // Initialize circular buffer for incoming packets.
        CircularBuffer* cb = initCircularBuffer(rcopy->windowsize, rcopy->buffersize);
    
        while(rcopy->attempts < MAX_ATTEMPTS){
            printf("----- RCOPY: DATA TRANSFER -----\n");
            int dataLen = safeRecvfrom(rcopy->socketNum, dataPacket, MAX_PDU_SIZE, 0, (struct sockaddr*)&src, (int *)&srcLen); 
            if (dataLen < 0) {
                fprintf(stderr, "Error or no data received\n");
                continue;
            }
            if (in_cksum((uint16_t *)dataPacket, dataLen) != 0) {
                fprintf(stderr, "Checksum error. Sending SREJ for %u\n", RR);
                sendSREJ(RR, rcopy);
                continue;
            }
            
            pdu_header header; 
            memcpy(&header, dataPacket, HEADER_SIZE); 
            uint32_t seq = ntohl(header.seq);

            if(RR == 0 ){
                rcopy->server.sin6_port = src.sin6_port; 
                printf("Updated server port to child's port: %d\n", ntohs(src.sin6_port));
            }
            
            switch (header.flag) {
                case 10: // EOF packet
                    printf("----- RCOPY: EOF RECEIVED -----\n");
                    sendEOF(rcopy);
                    // Flush any buffered packets before closing, if needed.
                    // (You may write a flush function for the CircularBuffer here.)
                    freeCircularBuffer(cb);
                    fclose(outputFile);
                    return STATE_DONE;
                case 16: // Data packet
                    if (seq == RR) { // Expected data
                        int payloadSize = dataLen - HEADER_SIZE;
                        printf("Received expected packet: seq=%u, expected=%u, payload=%d bytes\n", seq, RR, payloadSize);
                        // Write directly to file.
                        fwrite(dataPacket + HEADER_SIZE, 1, payloadSize, outputFile);
                        fflush(outputFile);
                        RR++;
                        // Optionally flush buffered packets from cb if you implement that.
                        sendRR(RR, rcopy);
                    } else if (seq > RR) { // Out-of-order packet, buffer it.
                        int payloadSize = dataLen - HEADER_SIZE;
                        printf("Received out-of-order packet: seq=%u, expected=%u, payload=%d bytes. Buffering.\n", seq, RR, payloadSize);
                        // Write the packet into the circular buffer.
                        writeCircularBuffer(cb, (uint8_t*)(dataPacket + HEADER_SIZE), payloadSize);
                        sendSREJ(RR, rcopy);
                    } else { // Duplicate packet.
                        printf("Received duplicate packet: seq=%u, expected=%u. Resending RR.\n", seq, RR);
                        sendRR(RR, rcopy);
                    }
                    break;
                default:
                    printf("Unknown flag %d received. Ignoring.\n", header.flag);
                    break;
            }
        }
    
        freeCircularBuffer(cb);
        fclose(outputFile);
        return STATE_DONE;
    }


void sendRR(int RR, RcopyContext *rcopy){
    pdu_header rr; 
    rr.seq  = htonl(RR); 
    rr.flag = 5; 
    rr.checksum = 0; 

    printf("Sending RR to port: %d\n", ntohs(rcopy->server.sin6_port));
    int sent = sendPdu(rcopy->socketNum, &rcopy->server, rr, "RR", strlen("RR")); 
    
    if(sent < 0)
        fprintf(stderr, "Failed to send RR for seq %d\n", RR);
    else
        printf("Sent RR for seq %d\n", RR);
} 

void sendSREJ(int SREJ, RcopyContext *rcopy){
    pdu_header srej; 
    srej.seq  = htonl(SREJ); 
    srej.flag = 6; 
    srej.checksum = 0; 

    int sent = sendPdu(rcopy->socketNum, &rcopy->server, srej, "SREJ", strlen("SREJ")); 

    if(sent < 0) fprintf(stderr, "Failed to send SREJ for seq %d\n", SREJ);
    else printf("Sent SREJ for seq %d\n", SREJ);
}

void sendEOF(RcopyContext *rcopy){
    pdu_header eof; 
    eof.seq = htonl(0); 
    eof.flag = 10; 
    eof.checksum = 0; 

    int sent = sendPdu(rcopy->socketNum, &rcopy->server, eof, "EOF_ACK", strlen("EOF_ACK"));  

    if(sent < 0) fprintf(stderr, "Failed to send EOF\n");
    else printf("Sent EOF ACK\n");
}

rcopy_state_t stateRetry(RcopyContext *rcopy){
    return STATE_DONE; 
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
    char buffer[MAX_PDU_SIZE];
    struct sockaddr_in6 src;
    socklen_t srcLen = sizeof(src);

    while (rcopy->attempts < MAX_ATTEMPTS) {
        // Send filename packet.
        sendFilename(rcopy->socketNum, &rcopy->server, rcopy->server_filename, rcopy->buffersize, rcopy->windowsize);
        printf("Handshake attempt %d...\n", rcopy->attempts + 1);

         if (pollCall(POLL_ONE_SEC) >= 0) {
            int dataLen = safeRecvfrom(rcopy->socketNum, buffer, MAX_PDU_SIZE, 0, (struct sockaddr *)&src, (int *)&srcLen);
            if (dataLen > 0) {
                // Read header from received data.
                pdu_header header;
                memcpy(&header, buffer, HEADER_SIZE);

                switch (header.flag) {
                    case 9:  // ACK flag
                        printf("Received ACK from server.\n");

                        int payload_len = dataLen - HEADER_SIZE;
                        char ackPayload[MAX_PAYLOAD - HEADER_SIZE + 1] = {0};
                        if (payload_len > 0) {
                            memcpy(ackPayload, buffer + HEADER_SIZE, payload_len);
                            ackPayload[payload_len] = '\0';
                            printf("ACK Payload: \"%s\"\n", ackPayload);
                        }

                        if (strcmp(ackPayload, "Ok") == 0) { 
                            printf("Handshake successful! Proceeding to file reception.\n"); 
                            return STATE_FILE_RECEIVE;} 
                        else { 
                            printf("Server responded with 'Not Ok'. Terminating.\n"); 
                            return STATE_DONE;}

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
        exit(EXIT_FAILURE);
    }
    printf("Sent filename packet: %s\n", filename);
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