#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdbool.h>


#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "cpe464.h"
#include "pollLib.h"
#include "windowBuffer.h"
#include "helperFunctions.h"

// ----- Server FSM States ----- 
typedef enum {
    STATE_WAIT_PACKET,
    STATE_TRANSFER_TO_CHILD,
    STATE_DONE
} server_state_t;

// ----- Server Context Structure ----- 
typedef struct {
    int portNum; 
    int socketNum; 
    double error_rate; 
    struct sockaddr_in6 client; 
    socklen_t clientAddrLen; 
    char filename[MAX_FILENAME + 1];
    uint32_t winSize;
    uint32_t bufSize;
    char pduBuffer[MAX_PDU_SIZE + 1];  // Buffer to hold received PDU
    int pduLen;                        // Length of the received PDU
} ServerContext;

// ----- Child Context Structure ----- 
typedef struct{
    int socketNum; 
    double error_rate; 
    struct sockaddr_in6 client; 
    socklen_t clientAddrLen; 
    char filename[MAX_FILENAME + 1];
    uint32_t winSize; 
    uint32_t bufSize; 
    bool eof; 
    bool srej;    
    int attempts;
    char pduBuffer[MAX_PDU_SIZE + 1];  // Buffer to hold received PDU
    int pduLen;                        // Length of the received PDU
} ChildContext; 


// ----- Sever Functions -----
void runServerFSM(ServerContext *server);
void processFilenamePacket(int socketNum, int payload_len, uint8_t *p, struct sockaddr_in6 *client);
int  checkArgs(int argc, char *argv[]);
void childInfo(ChildContext *child, ServerContext *server); 

// ----- Child Functions ----- 
void runChild(ChildContext *child); 

void transferData(ChildContext *child); 
void send_data_packet(ChildContext *child, WindowBuffer *wb, uint32_t seq, bool isEOF); 
void send_next_data(ChildContext *child, WindowBuffer *wb, uint32_t *nextSeq, 
    bool *eofSent, uint32_t *eofSeq, FILE *file);
void retransmit_packets(ChildContext *child, WindowBuffer *wb, uint32_t base, 
                       uint32_t nextSeq, bool eofSent, uint32_t eofSeq); 

// void sendSREJ(ChildContext * child, WindowBuffer *wb); 
// void processRR(); 
// void processSREJ(); 

int processClient(ChildContext *child, int dataLen, char *buffer);  
bool lookupFilename(const char *filename); 



int main ( int argc, char *argv[]  )
{ 
    ServerContext server; 
    memset(&server, 0, sizeof(ServerContext)); // Fixed sizeof issue
    server.portNum = checkArgs(argc, argv);
    server.socketNum = udpServerSetup(server.portNum);  
    server.error_rate = atof(argv[1]); 
    setupPollSet(); 
    sendErr_init(server.error_rate, DROP_ON, FLIP_OFF, DEBUG_ON, RSEED_ON);

    runServerFSM(&server); 
    close(server.socketNum); 

    return 0;
}

int checkArgs(int argc, char *argv[])
{
    // Checks args and returns port number
    int portNumber = 0;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <error-rate> <optional port number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    double errorRate = 0.0; 
    errorRate = atof(argv[1]); 
    if(errorRate < 0 || errorRate > 1)
    {
        fprintf(stderr, "Error: %s <error-rate> must be between 0.0 and 1.0\n", argv[1]); 
        exit(EXIT_FAILURE); 
    }
    
    if (argc > 2) {
        portNumber = atoi(argv[2]);
    } else {
        portNumber = 0;  // Let the OS choose a port
    }

    return portNumber;
}

void runServerFSM(ServerContext *server){
    server_state_t state = STATE_WAIT_PACKET; 
    int pduLen; char pdu[MAX_PDU_SIZE + 1]; 

    while(1){
        switch(state){
            case STATE_WAIT_PACKET:
                printf("----- FSM: STATE_WAIT_PACKET -----\n");
                server->clientAddrLen = sizeof(server->client); 
                pduLen = safeRecvfrom(server->socketNum, pdu, MAX_PDU_SIZE, 0, (struct sockaddr *)&server->client, (int *)(&server->clientAddrLen));

                if(in_cksum((unsigned short *)pdu, pduLen)) { printf("Corrupt packet detected, waiting for next packet\n"); break; }
                if (pduLen < (int)sizeof(pdu_header)) { printf("Received packet too small to contain header\n"); break; }
                
                pdu_header header;
                memcpy(&header, pdu, sizeof(pdu_header));
                
                state = STATE_TRANSFER_TO_CHILD;
                memcpy(server->pduBuffer, pdu, pduLen);
                server->pduLen = pduLen;
                break;
                
            case STATE_TRANSFER_TO_CHILD:
                printf("----- FSM: STATE_TRANSFER_TO_CHILD -----\n");
                pid_t childPid = fork(); 
                if(childPid < 0){
                    perror("fork");
                    state = STATE_WAIT_PACKET;
                } else if (childPid == 0){
                    close(server->socketNum);     
                    ChildContext childCtx;
                    childInfo(&childCtx, server); 

                    memcpy(childCtx.pduBuffer, server->pduBuffer, server->pduLen);
                    childCtx.pduLen = server->pduLen;
                    
                    runChild(&childCtx);
                    exit(0); 
                }
                state = STATE_WAIT_PACKET;  // Keep listening for new clients
                break; 

            default:
                printf("----- FSM: DEFAULT -----\n");
                printf("SERVER: Unexpected state reached. Returning to wait state.\n");
                state = STATE_WAIT_PACKET; 
                break;              
        }
    }
}

void childInfo(ChildContext *child, ServerContext *server){
    // Initialize the child context with server data
    child->socketNum = socket(AF_INET6, SOCK_DGRAM, 0);

    // ------ Delete later, only to debug port ----
    // Bind socket to get a port
    struct sockaddr_in6 childAddr;
    memset(&childAddr, 0, sizeof(childAddr));
    childAddr.sin6_family = AF_INET6;
    childAddr.sin6_addr = in6addr_any;
    childAddr.sin6_port = 0; // Let the OS choose a port
    
    if (bind(child->socketNum, (struct sockaddr *)&childAddr, sizeof(childAddr)) < 0) {
        perror("Child socket bind failed");
        return;
    }
    
    // Get the assigned port number
    socklen_t addrLen = sizeof(childAddr);
    if (getsockname(child->socketNum, (struct sockaddr *)&childAddr, &addrLen) < 0) {
        perror("getsockname failed");
    } else {
        printf("Child process (PID: %d) bound to port: %d\n", 
                getpid(), ntohs(childAddr.sin6_port));
    }
    // -------------------------------------------
    
    child->error_rate = server->error_rate;
    child->client = server->client;
    child->clientAddrLen = server->clientAddrLen;
    child->attempts = 0;
    child->eof = false;
    child->srej = false;
    
    // Copy the PDU buffer and length
    memcpy(child->pduBuffer, server->pduBuffer, server->pduLen);
    child->pduLen = server->pduLen;
    
    // Initialize sendtoErr and polling
    sendErr_init(child->error_rate, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_ON);
    setupPollSet();
    addToPollSet(child->socketNum);
}


void runChild(ChildContext *child) {
    printf("----- CHILD: START -----\n");
    
    // Process the initial packet (should be a filename packet)
    if (processClient(child, child->pduLen, child->pduBuffer) == 0) {
        // Valid filename packet, start data transfer
        transferData(child);

    }

    close(child->socketNum);
    printf("----- CHILD: DONE -----\n");
}



void transferData(ChildContext *child){
    printf("----- CHILD: TRANSFER DATA -----\n");
    char filePath[256]; 
    snprintf(filePath, sizeof(filePath), "test_files/%s.txt", child->filename); 

    FILE *file = fopen(filePath, "rb"); 
    if(!file){ perror("Error opening file"); return; }

    WindowBuffer wb; 
    init_window(&wb, child->winSize, child->bufSize, file);
    uint32_t nextSeq = 0, base = 0;
    bool eofSent = false, eofAcked = false;
    uint32_t eofSeq = 0; 

    // ----- Initial Window of Packets -----
    printf("----- CHILD: SENDING DATA (INITIAL)-----\n");
    while (nextSeq < wb.window_size && !feof(file)) {
        printf("----- CHILD: PACKET - SEQ: -----\n");

        size_t bytesRead = fread(wb.panes[nextSeq].data, 1, wb.buffer_size, file);
        wb.panes[nextSeq].seq_num = nextSeq;
        
        if (bytesRead < wb.buffer_size || feof(file)) {
            eofSent = true;
            eofSeq = nextSeq;
        }
        
        send_data_packet(child, &wb, nextSeq, eofSent);
        nextSeq++;
    }

    printf("----- CHILD: SENDING DATA (MAIN)-----\n");
    // ----- Main Data Transfer Loop -----
   // Main transfer loop
   while (!eofAcked && child->attempts < MAX_ATTEMPTS) {
        int pollResult = pollCall(POLL_ONE_SEC);
        
        if (pollResult > 0) {
            child->attempts = 0;
            uint8_t buffer[MAX_PDU_SIZE];
            int recvLen = safeRecvfrom(child->socketNum, buffer, MAX_PDU_SIZE, 0, 
                            (struct sockaddr *)&child->client, (int *)&child->clientAddrLen);
            
            if (recvLen > 0 && !in_cksum((unsigned short *)buffer, recvLen)) {
                pdu_header header;
                memcpy(&header, buffer, sizeof(pdu_header));
                uint32_t ackSeq = ntohl(header.seq);
                
                if (header.flag == 5) {  // RR
                    if (ackSeq > base) {
                        printf("RR: ack=%u (moving window: %u → %u)\n", ackSeq, base, ackSeq);
                        slide_window(&wb, ackSeq - base);
                        base = ackSeq;
                        
                        // Send more packets if window opened
                        while (nextSeq < base + wb.window_size && !eofSent) {
                            send_next_data(child, &wb, &nextSeq, &eofSent, &eofSeq, file);
                        }
                    } else {
                        printf("RR: Duplicate/Old ack=%u (current base=%u)\n", ackSeq, base);
                    }
                } else if (header.flag == 6) {  // SREJ
                    printf("SREJ: Resending packet seq=%u\n", ackSeq);
                    send_data_packet(child, &wb, ackSeq, (eofSent && ackSeq == eofSeq));
                } else if (header.flag == 10) {  // EOF ACK
                    printf("EOF_ACK: Received for seq=%u\n", ackSeq);
                    eofAcked = true;
                }
            }
        } else { child->attempts++; retransmit_packets(child, &wb, base, nextSeq, eofSent, eofSeq);}
        
    }
    
    fclose(file);
    free_window(&wb);
}

void send_data_packet(ChildContext *child, WindowBuffer *wb, uint32_t seq, bool isEOF) {
    pdu_header header = {
        .seq = htonl(seq),
        .flag = isEOF ? 10 : 16,  // 10=EOF, 16=data
        .checksum = 0
    };

    printf("SEND: seq=%u, flag=%d, %s\n", seq, header.flag, isEOF ? "EOF" : "DATA");

    if(isEOF) {
        printf("\nSERVER - EOF PACKET CONTENT:\n");
        printf("Sequence: %u\n", seq);
        printf("Data length: %d bytes\n", wb->buffer_size);
        printf("Data hex: ");
        for(int i = 0; i < wb->buffer_size; i++) {
        printf("%02x ", (unsigned char)wb->panes[seq % wb->window_size].data[i]);
        }
        printf("\nData text: \"");
        for(int i = 0; i < wb->buffer_size; i++) {
        char c = wb->panes[seq % wb->window_size].data[i];
        if(c >= 32 && c <= 126) printf("%c", c);
        else printf(".");
        }
        printf("\"\n");
    }
    
    // Cast to const char* to resolve the signedness warning
    sendPdu(child->socketNum, &child->client, header, 
           (const char*)wb->panes[seq % wb->window_size].data, wb->buffer_size);
}

void send_next_data(ChildContext *child, WindowBuffer *wb, uint32_t *nextSeq, 
                   bool *eofSent, uint32_t *eofSeq, FILE *file) {
    size_t bytesRead = fread(wb->panes[*nextSeq % wb->window_size].data, 
                            1, wb->buffer_size, file);
    wb->panes[*nextSeq % wb->window_size].seq_num = *nextSeq;
    
    if (bytesRead < wb->buffer_size || feof(file)) {
        *eofSent = true;
        *eofSeq = *nextSeq;
        printf("EOF DETECTED at seq=%u (read %zu bytes)\n", *nextSeq, bytesRead);
    }
    
    send_data_packet(child, wb, *nextSeq, *eofSent && (*nextSeq == *eofSeq));
    (*nextSeq)++;
}

void retransmit_packets(ChildContext *child, WindowBuffer *wb, uint32_t base, 
                       uint32_t nextSeq, bool eofSent, uint32_t eofSeq) {
    for (uint32_t i = base; i < nextSeq; i++) {
        printf("TIMEOUT: Retransmitting packets seq=%u to %u\n", base, nextSeq-1);
        send_data_packet(child, wb, i, eofSent && (i == eofSeq));
    }
}

// int receiveParsePacket(int ){

// }

// void processRR(ChildContext *child, int seq, WindowBuffer *wb){
//     printf("----- CHILD: PRCOCESS RR -----\n");



// }

// void processSREJ(ChildContext *child, int seq, WindowBuffer *wb){
//     printf("----- CHILD: PRCOCESS SREJ -----\n");

// }


// void sendSREJ(ChildContext * child, WindowBuffer *wb){
//     printf("Sending data packet\n"); 
// }





int processClient(ChildContext *child, int dataLen, char *buffer){
    printf("----- CHILD: PRCESSING CLIENT -----\n");

    // Packet corruption check is assumed to be done before this function call
    
    pdu_header header;
    memcpy(&header, buffer, sizeof(pdu_header));
    
    // Process filename packet (flag == 8)
    if (header.flag == 8) {
        // Minimum size check
        if (dataLen < (int)(sizeof(pdu_header) + 9)) {
            fprintf(stderr, "Payload too short for filename packet\n");
            return -1;
        }
        
        // Quick extraction of all needed data
        uint32_t *sizes_ptr = (uint32_t *)(buffer + sizeof(pdu_header));
        child->winSize = ntohl(sizes_ptr[0]);
        child->bufSize = ntohl(sizes_ptr[1]);
        
        // Get filename
        uint8_t name_len = buffer[sizeof(pdu_header) + 8];
        memcpy(child->filename, buffer + sizeof(pdu_header) + 9, name_len);
        child->filename[name_len] = '\0';
        
        // Create and send ACK/NACK
        pdu_header ack = {.seq = htonl(0), .flag = 9, .checksum = 0};
        const char *msg = lookupFilename(child->filename) ? "Ok" : "Not Ok";
        
        if (sendPdu(child->socketNum, &child->client, ack, msg, strlen(msg)) < 0) {
            fprintf(stderr, "ERROR: Failed to send %s\n", msg);
            return -1;
        }
        
        // Return success only if file exists
        return strcmp(msg, "Ok") == 0 ? 0 : -1;
    }
    
    return -1;  // Unrecognized packet type
}

bool lookupFilename(const char *filename) {
    char filepath[MAX_FILENAME + 12]; // +12 for "test_files/" and null terminator
    
    // Check if there's a '.' in the filename
    if (strchr(filename, '.') == NULL) {
        snprintf(filepath, sizeof(filepath), "test_files/%s.txt", filename);
    } else {
        snprintf(filepath, sizeof(filepath), "test_files/%s", filename);
    }
    
    // Simply check if the file exists and is readable
    if (access(filepath, R_OK) == 0) {
        printf("File \"%s\" found and readable.\n", filepath);
        return true;
    } else {
        printf("File \"%s\" not found or not readable.\n", filepath);
        return false;
    }
}


