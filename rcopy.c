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

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "cpe464.h"

typedef struct __attribute__((packed)){
	uint32_t seq; 
	uint16_t checksum; 
	uint8_t flag; 
} pdu_header; 

#define MAXBUF 80
#define MAX_FILENAME 100
#define HEADER_SIZE sizeof(pdu_header)
#define MAX_PACKET_SIZE 256  // Adjust this size as needed

int readFromStdin(char * buffer);
int checkArgs(int argc, char * argv[]);
void talkToServer(int socketNum, struct sockaddr_in6 * server);
void sendFilename(int socketNum, struct sockaddr_in6 * server, char* filename, int buffersize, int windowsize); 

int main (int argc, char *argv[])
 {
	int socketNum = 0;				
	struct sockaddr_in6 server;		// Supports 4 and 6 but requires IPv6 struct
	int portNumber = 0;
	
	portNumber = checkArgs(argc, argv);
	// sendErr_init(.1, DROP_ON, FLIP_OFF, DEBUG_ON, RSEED_ON);

	socketNum = setupUdpClientToServer(&server, argv[6], portNumber);
	// addToPollSet(socketNum); 
	// talkToServer(socketNum, &server);
	sendFilename(socketNum, &server, argv[1], atoi(argv[4]), atoi(argv[3])); 
	close(socketNum);

	return 0;
}

void sendFilename(int socketNum, struct sockaddr_in6 * server, char* filename, int buffersize, int windowsize){
	// Build the payload: "filename buffersize windowsize"
    char payload[MAX_PACKET_SIZE - HEADER_SIZE];
    snprintf(payload, sizeof(payload), "%s%d%d", filename, buffersize, windowsize);
    int payload_len = strlen(payload);

    // Prepare the header.
    pdu_header header;
    header.seq = htonl(0);   // Sequence number 0 for the filename packet.
    header.flag = 8;         // Flag value 8 indicates filename exchange.
    header.checksum = 0;     // Initially zero for checksum calculation.

    // Create a fixed-size packet buffer.
    char packet[MAX_PACKET_SIZE];

    // Copy header into the packet (first HEADER_SIZE bytes).
    memcpy(packet, &header, HEADER_SIZE);
    memcpy(packet + HEADER_SIZE, payload, payload_len);
    int packet_len = HEADER_SIZE + payload_len;

    // Compute the checksum over the entire packet.
    uint16_t checksum = in_cksum((unsigned short *)packet, packet_len);
    checksum = htons(checksum);
    memcpy(packet + 4, &checksum, sizeof(uint16_t));

    // Print the packet in hex.
    printf("Packet hex dump: ");
    for (int i = 0; i < packet_len; i++) {
        printf("%02x ", (unsigned char)packet[i]);
    }
    printf("\n");

    // Send the packet using sendtoErr.
    int serverAddrLen = sizeof(struct sockaddr_in6);
    if (sendtoErr(socketNum, packet, packet_len, 0, (struct sockaddr *)server, serverAddrLen) < 0) {
        perror("sendtoErr");
        exit(EXIT_FAILURE);
    }
    printf("Sent filename packet: %s\n", payload);
}

void talkToServer(int socketNum, struct sockaddr_in6 * server)
{
	int serverAddrLen = sizeof(struct sockaddr_in6);
	char * ipString = NULL;
	int dataLen = 0; 
	char buffer[MAXBUF+1];
	
	buffer[0] = '\0';
	while (buffer[0] != '.')
	{
		dataLen = readFromStdin(buffer);

		printf("Sending: %s with len: %d\n", buffer,dataLen);
	
		sendtoErr(socketNum, buffer, dataLen, 0, (struct sockaddr *) server, serverAddrLen);
		
		safeRecvfrom(socketNum, buffer, MAXBUF, 0, (struct sockaddr *) server, &serverAddrLen);
		
		// print out bytes received
		ipString = ipAddressToString(server);
		printf("Server with ip: %s and port %d said it received %s\n", ipString, ntohs(server->sin6_port), buffer);
	      
	}
}

int readFromStdin(char * buffer)
{
	char aChar = 0;
	int inputLen = 0;        
	
	// Important you don't input more characters than you have space 
	buffer[0] = '\0';
	printf("Enter data: ");
	while (inputLen < (MAXBUF - 1) && aChar != '\n')
	{
		aChar = getchar();
		if (aChar != '\n')
		{
			buffer[inputLen] = aChar;
			inputLen++;
		}
	}
	
	// Null terminate the string
	buffer[inputLen] = '\0';
	inputLen++;
	
	return inputLen;
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
    
    portNumber = atoi(argv[7]);
    return portNumber;
}





