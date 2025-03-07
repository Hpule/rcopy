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
    STATE_PROCESS_FILENAME,
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
} ServerContext;

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
} ChildContent; 


// ----- Sever Functions -----
void runServerFSM(ServerContext *server);
void processFilenamePacket(int socketNum, int payload_len, uint8_t *p, struct sockaddr_in6 *client);
int  checkArgs(int argc, char *argv[]);
int processClient(ServerContext *server, int dataLen, char *buffer); 
bool lookupFilename(const char *filename); 

// ----- Child Functions ----- 
void runChild(ChildContent *child); 
void transmitData(ChildContent *child, FILE *file); 
void processRR(); 
void processSREJ(); 

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


void runServerFSM(ServerContext *server){
    server_state_t state = STATE_WAIT_PACKET; 
    char buffer[MAX_PDU_SIZE + 1]; 
    int dataLen; 

    while(1){
        switch(state){
            case STATE_WAIT_PACKET:
                printf("----- FSM: STATE_WAIT_PACKET -----\n");
                server->clientAddrLen = sizeof(server->client); 


                dataLen = safeRecvfrom(server->socketNum, buffer, MAX_PDU_SIZE, 0,
                                (struct sockaddr *)&server->client, (int *)(&server->clientAddrLen));

                if (dataLen >= (int)sizeof(pdu_header)) {
                    state = STATE_PROCESS_FILENAME;
                }
                break;
            case STATE_PROCESS_FILENAME:
                printf("----- FSM: STATE_PROCESS_FILENAME -----\n");

                if (processClient(server, dataLen, buffer) == 0) {
                    state = STATE_TRANSFER_TO_CHILD;
                } else {
                    printf("SERVER: File not found. Returning to waiting state.\n");
                    state = STATE_WAIT_PACKET;
                }
                break;
            case STATE_TRANSFER_TO_CHILD:
                printf("----- FSM: STATE_TRANSFER_TO_CHILD -----\n");
                pid_t child = fork(); 
                if(child < 0){
                    perror("fork"); 
                } else if (child == 0){
                    // close the connection betweem server and rcopt client
                    close(server->socketNum); 
                    
                    ChildContent child;
                    child.socketNum = socket(AF_INET6, SOCK_DGRAM, 0);  // New child socket
                    child.error_rate = server->error_rate;
                    child.client = server->client;
                    child.clientAddrLen = server->clientAddrLen;
                    child.bufSize = server->bufSize;
                    child.winSize = server->winSize;
                    child.attempts = 0;
                    child.eof = false;
                    strcpy(child.filename, server->filename);  // Add filename to child struct
                    addToPollSet(child.socketNum); 
                    runChild(&child);
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

void runChild(ChildContent *child){
    printf("----- CHILD: BEGIN -----\n");

    sendErr_init(child->error_rate, DROP_ON, FLIP_OFF, DEBUG_ON, RSEED_ON);

    struct sockaddr_in6 src;
    socklen_t srcLen = sizeof(src);
    // uint32_t seq = 0;
    char filePath[256];
    snprintf(filePath, sizeof(filePath), "test_files/%s.txt", child->filename);


    FILE *file = fopen(filePath, "rb");
    if (!file) {
        perror("Error opening file");
        close(child->socketNum);
        return;
    }

    printf("----- CHILD: PACKET TRANSFER -----\n");
    init_window_buffer(child->winSize); 

    while (!child->eof && child->attempts < MAX_ATTEMPTS) {

        while(!is_window_full() && !child->eof){
            printf("----- CHILD: SENDING PACKET (WINDOW OPEN) -----\n"); 

            transmitData(child, file); 
            
            while (pollCall(0) > 0) {                
                printf("----- CHILD: PROCESSING PACKET (WINDOW OPEN) -----\n"); 
                    char ackPacket[MAX_PDU_SIZE]; 
                    int ackLen = safeRecvfrom(child->socketNum, ackPacket, MAX_PDU_SIZE, 0, (struct sockaddr*)&child->client, (int *)&child->clientAddrLen);

                    if (ackLen < 0) { fprintf(stderr, "Error or no data received\n"); break;  }
                    // Check for corruption later
                    
                    pdu_header ack_header; 
                    memcpy(&ack_header, ackPacket, HEADER_SIZE);
                    
                    switch(ack_header.flag){
                        case 5: // RR packet
                            processRR(child, ack_header.seq); 
                            break; 
                        case 6: // SREJ packet
                            processSREJ(child, ack_header.seq); 
                            break;
                        case 10: // EOF ACK from sender 
                            // Close connection
                            printf("Received EOF ACK from client.\n");
                            destroy_window_buffer(); 
                            fclose(file); 
                            return;  
                        default:
                        fprintf(stderr, "Unknown packet flag: %d\n", ack_header.flag);

                            break; 
                    }
                
            }
            printf("----- CHILD: NO ACK (WINDOW OPEN) -----\n");
        }

        while(is_window_full()){
            printf("----- CHILD: WINDOW CLOSED -----\n"); 
            while(pollCall(POLL_ONE_SEC)){
                printf("----- CHILD: RESEND LOWEST PACKET -----\n"); 
                // Resend Lowest Packet 
                get_lowest_unack_seq(); 
                // Attempts++
                child->attempts++; 
                if(child->attempts > 10){
                    return; 
                }
            }

            printf("----- CHILD: PROCESSING PACKET (WINDOW CLOSED) -----\n"); 

            char ackPacket[MAX_PDU_SIZE]; 
            int ackLen = safeRecvfrom(child->socketNum, ackPacket, MAX_PDU_SIZE, 0, (struct sockaddr*)&src, (int *)&srcLen);

            if (ackLen < 0) { fprintf(stderr, "Error or no data received\n"); continue;  }
            // Check for corruption later
            pdu_header ack_header; 
            memcpy(&ack_header, ackPacket, HEADER_SIZE);
            
            switch(ack_header.flag){
                case 5: // RR packet
                    processRR(child, ack_header.seq); 
                    break; 
                case 6: // SREJ packet
                    processSREJ(child, ack_header.seq); 
                    break;
                case 10: // EOF ACK from sender 
                    // Close connection
                    destroy_window_buffer(); 
                    fclose(file); 
                    return;  
                default:
                fprintf(stderr, "Unknown packet flag: %d\n", ack_header.flag);
                    break; 
            }
            

        }
    }

    printf("----- CHILD: DONE -----\n");
    fclose(file);
    printf("File transfer complete.\n");
    return; 
}

void processRR(ChildContent *child, int seq){
    printf("----- CHILD: PROCESS RR -----\n");
    // Convert sequence number from network byte order to host order.
    uint32_t rr = ntohl(seq);
    printf("Received RR for %d, updating window.\n", rr);
    
    // Update the window buffer to mark packets < rr as acknowledged.
    update_window(rr);

    // Invalidate all packets below the acknowledged sequence.
    invalidate_packets(rr);

    print_window_buffer();
}

void processSREJ(ChildContent *child, int seq){
    printf("----- CHILD: PROCESS SREJ -----\n");
    printf("Received SREJ for %d, retransmitting packet.\n", seq);
    // Retrieve the packet from the window buffer for retransmission
    pdu_header header;
    uint8_t data[MAX_PAYLOAD];
    size_t dataSize;
    if(get_packet_from_window(seq, &header, data, &dataSize)) {
        // Retransmit the packet
        int sent = sendPdu(child->socketNum, &child->client, header, (char *)data, dataSize);
        if(sent < 0)
            fprintf(stderr, "Retransmission for seq %d failed.\n", seq);
        else
            printf("Retransmitted packet seq %d.\n", seq);
    } else {
        printf("No buffered packet for seq %d found.\n", seq);
    }
}

void transmitData(ChildContent *child, FILE *file) {
    char data[MAX_PDU_SIZE - HEADER_SIZE];
    int bytesRead = fread(data, 1, sizeof(data), file);

    if (bytesRead == 0) {
        // EOF reached
        child->eof = true;
        pdu_header eofHeader;
        eofHeader.seq = htonl(get_lowest_unack_seq());  // Use last acknowledged sequence
        eofHeader.flag = 10;  // EOF flag
        eofHeader.checksum = 0;
        sendPdu(child->socketNum, &child->client, eofHeader, NULL, 0);
        printf("Reached EOF, sending EOF packet for seq %u\n", get_lowest_unack_seq());
        return;
    }

    // Retrieve the current sequence number.
    uint32_t seqToSend = get_next_seq_to_send();
    printf("Preparing to send packet with seq %u\n", seqToSend);

    pdu_header header;
    header.seq = htonl(seqToSend);
    header.flag = 16;  // Data packet flag
    header.checksum = 0;  // (Compute checksum if needed)

    int sent = sendPdu(child->socketNum, &child->client, header, data, bytesRead);
    if (sent < 0) {
        fprintf(stderr, "Failed to send data packet for seq %u\n", seqToSend);
    } else {
        printf("Sent data packet with seq %u (%d bytes)\n", seqToSend, bytesRead);
    }

    // Buffer the packet for potential retransmission.
    if (!buffer_packet(seqToSend, (uint8_t *)data, bytesRead, header)) {
        fprintf(stderr, "Buffering packet for seq %u failed\n", seqToSend);
    } else {
        printf("Buffered packet with seq %u\n", seqToSend);
    }

    // Debug: Print current window state.
    print_window_buffer();
}


int processClient(ServerContext *server, int dataLen, char *buffer)
{
    printf("Received packet (len=%d):\n", dataLen);
    printHexDump("", buffer, dataLen);

    // Validate checksum before processing packet
    if (in_cksum((uint16_t *)buffer, dataLen) != 0) {
        fprintf(stderr, "Corrupt packet detected. Discarding.\n");
        return -1;  // Ignore corrupted packets
    }
    
    pdu_header header;
    memcpy(&header, buffer, sizeof(pdu_header));
    uint8_t flag = header.flag;

    // Process filename packet
    if (flag == 8) {
        if (dataLen < (int)(sizeof(pdu_header) + 9)) {
            fprintf(stderr, "Payload too short for filename packet\n");
            return -1;
        }

        // Extract window and buffer sizes
        memcpy(&server->winSize, buffer + sizeof(pdu_header), sizeof(uint32_t));
        memcpy(&server->bufSize, buffer + sizeof(pdu_header) + sizeof(uint32_t), sizeof(uint32_t));
        server->winSize = ntohl(server->winSize);
        server->bufSize = ntohl(server->bufSize);

        // Extract filename
        uint8_t name_len = buffer[sizeof(pdu_header) + 8];
        if (dataLen < (int)(sizeof(pdu_header) + 9 + name_len)) {
            fprintf(stderr, "Payload too short for filename and sizes\n");
            return -1;
        }
        memcpy(server->filename, buffer + sizeof(pdu_header) + 9, name_len);
        server->filename[name_len] = '\0';

        printf("Parsed Filename: %s\n  Window: %u, Buffer: %u\n", server->filename, server->winSize, server->bufSize);

        // Prepare ACK header
        pdu_header ack;
        ack.seq = htonl(0);
        ack.flag = 9;
        ack.checksum = 0;

        // Ensure client address is passed correctly
        if (lookupFilename(server->filename)) {
            if (sendPdu(server->socketNum, &server->client, ack, "Ok", strlen("Ok")) < 0) {
                fprintf(stderr, "ERROR: Failed to send ACK\n");
                return -1;
            }
            return 0;  // File exists, proceed with file transfer
        } else {
            if (sendPdu(server->socketNum, &server->client, ack, "Not Ok", strlen("Not Ok")) < 0) {
                fprintf(stderr, "ERROR: Failed to send negative ACK\n");
            }
            return -1;
        }
    }

    return -1;
}

bool lookupFilename(const char *filename) {
    char adjusted[MAX_FILENAME + 1];
    // Check if there's a '.' in the filename.
    if (strchr(filename, '.') == NULL) {
        // Append ".txt" if not present.
        snprintf(adjusted, sizeof(adjusted), "%s.txt", filename);
    } else {
        strncpy(adjusted, filename, sizeof(adjusted) - 1);
        adjusted[sizeof(adjusted) - 1] = '\0';
    }
    
    DIR *d = opendir("test_files");
    if (!d) {
        perror("opendir");
        return false;  
    }
    
    bool found = false;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, adjusted) == 0) {
            found = true;
            break;
        }
    }
    closedir(d);
    
    if (found) {
        printf("File \"%s\" found in directory \"test_files\".\n", adjusted);
    } else {
        printf("File \"%s\" not found in directory \"test_files\".\n", adjusted);
    }
    return found;
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