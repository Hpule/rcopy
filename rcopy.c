#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <math.h>

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "cpe464.h"
#include "pollLib.h"
#include "windowBuffer.h"
#include "helperFunctions.h"

int  readFromStdin(char * buffer);
int  checkArgs(int argc, char * argv[]);
void talkToServer(int socketNum, struct sockaddr_in6 * server);
void sendFilename(int socketNum, struct sockaddr_in6 * server, char* filename, int buffersize, int windowsize); 
void runRcopy(int socketNum, struct sockaddr_in6 * server); 
void processACK(); 
void processPacket(); 

int main (int argc, char *argv[])
 {
	int socketNum = 0;				
	struct sockaddr_in6 server;		// Supports 4 and 6 but requires IPv6 struct
	int portNumber = 0;
	double dropped_packet_rate = 0.0; 
	
	portNumber = checkArgs(argc, argv);
	dropped_packet_rate = atof(argv[5]); 
	// sendErr_init(dropped_packet_rate, DROP_ON, FLIP_OFF, DEBUG_ON, RSEED_ON);

	socketNum = setupUdpClientToServer(&server, argv[6], portNumber);
	// addToPollSet(socketNum); 
	// talkToServer(socketNum, &server);
	sendFilename(socketNum, &server, argv[1], atoi(argv[4]), atoi(argv[3])); 
	runRcopy(socketNum, &server); 
	close(socketNum);
	return 0;
}

void runRcopy(int socketNum, struct sockaddr_in6 * server){
	struct sockaddr_in6 src;
    socklen_t srcLen = sizeof(src);
    char buffer[MAX_PACKET_SIZE];

    while (1) {
        int dataLen = safeRecvfrom(socketNum, buffer, MAX_PACKET_SIZE, 0,
                                   (struct sockaddr *)&src, (int *)&srcLen);
        if (dataLen <= 0) {
            fprintf(stderr, "Error or no data received\n");
            continue;
        }

        // ------ Recieved Data ------
        printf("Received packet (len=%d):\n", dataLen);
        printHexDump("", buffer, dataLen);

        // Extract the PDU header.
        pdu_header header;
        memcpy(&header, buffer, HEADER_SIZE);
        header.seq = ntohl(header.seq);
        header.checksum = ntohs(header.checksum);
        uint8_t flag = header.flag;

        printf("PDU Header:\n");
        printf("  Sequence: %u\n", header.seq);
        printf("  Checksum: 0x%04x\n", header.checksum);
        printf("  Flag: %u\n", flag);

        switch(flag){
            case 9:
                {
                    // Process ACK payload.
                    int payload_len = dataLen - HEADER_SIZE;
                    char payload[MAX_PACKET_SIZE - HEADER_SIZE + 1];
                    if (payload_len > 0) {
                        memcpy(payload, buffer + HEADER_SIZE, payload_len);
                        payload[payload_len] = '\0';
                        printf("ACK Payload: \"%s\"\n", payload);
                        if (strcmp(payload, "Ok") == 0) {
                            printf("Server indicates 'Ok'. Proceed with file transfer.\n");
                            // Here, call the function that starts receiving file data.
                        } else if (strcmp(payload, "Not Ok") == 0) {
                            printf("Server indicates 'Not Ok'. Terminating client.\n");
                            exit(EXIT_FAILURE);
                        } else {
                            printf("Unexpected ACK payload. Terminating client.\n");
                            exit(EXIT_FAILURE);
                        }
                    }
                    // Exit loop after processing ACK.
                    return;
                }
                break; 
            case 10:
                printf("EOF / Last data Packet");
                break; 
            case 16:
                printf("Regular data Packet");
                break; 
            default:
                printf("Received packet with unknown flag (%d).\n", flag);
                break; 


        }

        printf("------------------------------------------------\n");

    // ------ Send SREJ or RR ------

    // ------ Save Data ------

    }
}

void sendFilename(int socketNum, struct sockaddr_in6 * server, char* filename, int buffersize, int windowsize){
    char payload[MAX_PACKET_SIZE - HEADER_SIZE];
    int payload_len = 0;

    uint8_t filename_len = strlen(filename);
    if (filename_len > MAX_FILENAME) {
        fprintf(stderr, "Filename too long. Maximum length allowed is %d characters.\n", MAX_FILENAME);
        exit(EXIT_FAILURE);
    }

    // Packing the payload in binary format
    payload[payload_len++] = filename_len;
    memcpy(payload + payload_len, filename, filename_len);
    payload_len += filename_len;

    uint32_t net_bufsize = htonl(buffersize);
    uint32_t net_windowsize = htonl(windowsize);

    memcpy(payload + payload_len, &net_bufsize, sizeof(uint32_t));
    payload_len += sizeof(uint32_t);

    memcpy(payload + payload_len, &net_windowsize, sizeof(uint32_t));
    payload_len += sizeof(uint32_t);

    pdu_header header;
    header.seq = htonl(0);
    header.flag = 8; 
    header.checksum = 0;

    int bytesSent = sendPdu(socketNum, server, header, payload, payload_len);
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





