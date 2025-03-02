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

// ----- Rcopy Context Structure ----- 
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

// ----- Other stuctures ----- 

// ----- FSM Declarations -----


// ----- Sever Functions -----
void runServerFSM(ServerContext *server);
void processFilenamePacket(int socketNum, int payload_len, uint8_t *p, struct sockaddr_in6 *client);
int  checkArgs(int argc, char *argv[]);
int processClient(ServerContext *server, int dataLen, char *buffer); 
bool lookupFilename(const char *filename); 

// ----- Child Functions ----- 
void runChild(); 


int main ( int argc, char *argv[]  )
{ 
    ServerContext server; 
    memset(&server, 0, sizeof(ServerContext)); // Fixed sizeof issue
    server.portNum = checkArgs(argc, argv);
    server.socketNum = udpServerSetup(server.portNum);  
    server.error_rate = atof(argv[1]); 
    setupPollSet(); 
    // sendErr_init(server.error_rate, DROP_ON, FLIP_OFF, DEBUG_ON, RSEED_ON);

    runServerFSM(&server); 
    close(server.socketNum); 

    return 0;
}


void runServerFSM(ServerContext *server){
    server_state_t state = STATE_WAIT_PACKET; 
    char buffer[MAXBUF + 1]; 
    int dataLen; 

    while(1){
        switch(state){
            case STATE_WAIT_PACKET:
                printf("SERVER: Waiting for incoming packets...\n");
                server->clientAddrLen = sizeof(server->client); 

                dataLen = safeRecvfrom(server->socketNum, buffer, MAXBUF, 0,
                                (struct sockaddr *)&server->client, (int *)(&server->clientAddrLen));

                if (dataLen >= (int)sizeof(pdu_header)) {
                    state = STATE_PROCESS_FILENAME;
                }
                break;
            case STATE_PROCESS_FILENAME:
                printf("SERVER: Processing filename packet...\n");

                if (processClient(server, dataLen, buffer) == 0) {
                    state = STATE_TRANSFER_TO_CHILD;
                } else {
                    printf("SERVER: File not found. Returning to waiting state.\n");
                    state = STATE_WAIT_PACKET;
                }
                break;
            case STATE_TRANSFER_TO_CHILD:
                printf("SERVER: Forking child process for file transfer...\n");
                pid_t child = fork(); 
                if(child < 0){
                    perror("fork"); 
                } else if (child == 0){
                    runChild(0); 
                    exit(0); 
                }

                state = STATE_WAIT_PACKET;  // Keep listening for new clients

                break; 
            default:
                printf("SERVER: Unexpected state reached. Returning to wait state.\n");
                state = STATE_WAIT_PACKET; 
                break;              

        }
    }
}

void runChild(int attempts){
    // sendErr_init(server.error_rate, DROP_ON, FLIP_OFF, DEBUG_ON, RSEED_ON);


    // Going to have a double while loop

    // bool windowOpen = true; 
    // bool eof = false; 
    // int child_attempts = attempts; 

    // Open file and initilze circular buffer

    // while(!eof){
        // if attempts > 10, close 
        // while(windowOpen){
            // if attempts > 10, close 
            // Read from disk
            // send data 
            // while(poll(0) == true)
            //      process RR / SREJ
        // }
        // while(!windowOpen){
            // if attempts > 10, close 
            // while(poll(1000))
            //   resend lowest pdu
            //      child_attempts++
            // else 
            //      process RR / SREJ
        // }
    // }
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