#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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


void runServer(int socket); 
void processClient(int socketNum, int dataLen, char * buffer); 
int checkArgs(int argc, char *argv[]);

int main ( int argc, char *argv[]  )
{ 
	int socketNum = 0;				
	int portNumber = 0;

	portNumber = checkArgs(argc, argv);
	socketNum = udpServerSetup(portNumber);
	// sendErr_init(.1, DROP_ON, FLIP_OFF, DEBUG_ON, RSEED_ON);

	runServer(socketNum); 

	close(socketNum);
	
	return 0;
}

void runServer(int socketNum){
    struct sockaddr_in6 client;
    socklen_t clientAddrLen = sizeof(client);
    char buffer[MAXBUF + 1];

	while(1){
		// Wait for an incoming packet (e.g., a filename or file data packet)
		int dataLen = recvfrom(socketNum, buffer, MAXBUF, 0,
			(struct sockaddr *)&client, &clientAddrLen);
		if (dataLen < (int)sizeof(pdu_header)) {
		fprintf(stderr, "Received packet too short (%d bytes).\n", dataLen);
		continue;
		}

		processClient(socketNum, dataLen, buffer);
	}
}

void processClient(int socketNum, int dataLen, char * buffer){
		// Print hex dump of the received packet
		printf("Received packet (len=%d):\n", dataLen);
		for (int i = 0; i < dataLen; i++) {
			printf("%02x ", (unsigned char)buffer[i]);
		}
		printf("\n");

		// Extract the PDU header using memcpy
		pdu_header header;
		memcpy(&header, buffer, sizeof(pdu_header));
		// Convert fields from network to host order
		uint32_t seq = ntohl(header.seq);
		uint16_t chksum = ntohs(header.checksum);
		uint8_t flag = header.flag;
		printf("PDU Header:\n");
		printf("  Sequence: %u\n", seq);
		printf("  Checksum: 0x%04x\n", chksum);
		printf("  Flag: %u\n", flag);

		if(flag == (uint8_t) 8){
			// If there is a payload, extract it as a printable string
			int payload_len = dataLen - sizeof(pdu_header);
			if (payload_len > 0) {
				char payload[MAXBUF - sizeof(pdu_header) + 1];
				memcpy(payload, buffer + sizeof(pdu_header), payload_len);
				payload[payload_len] = '\0'; // Null-terminate the string
				printf("Payload: \"%s\"\n", payload);
	
				// Unparse the payload. Expected format: "<filename> <buffersize> <windowsize>"
				char filename[MAX_FILENAME];
				int bufSize, winSize;
				if (sscanf(payload, "%s%d%d", filename, &bufSize, &winSize) == 3) {
					printf("Parsed Information:\n");
					printf("  Filename: %s\n", filename);
					printf("  Buffer size: %d\n", bufSize);
					printf("  Window size: %d\n", winSize);
				} else {
					fprintf(stderr, "Error parsing payload: %s\n", payload);
				}
			}
			printf("--------------------------------------------------\n");
		} else {
			printf("--------------------------------------------------\n");
		}
}


// void processClient(int socketNum)
// {
// 	int dataLen = 0; 
// 	char buffer[MAXBUF + 1];	  
// 	struct sockaddr_in6 client;		
// 	int clientAddrLen = sizeof(client);	
	
// 	buffer[0] = '\0';
// 	while (buffer[0] != '.')
// 	{
// 		dataLen = safeRecvfrom(socketNum, buffer, MAXBUF, 0, (struct sockaddr *) &client, &clientAddrLen);
	
// 		printf("Received message from client with ");
// 		printIPInfo(&client);
// 		printf(" Len: %d \'%s\'\n", dataLen, buffer);

// 		// just for fun send back to client number of bytes received
// 		sprintf(buffer, "bytes: %d", dataLen);
// 		sendtoErr(socketNum, buffer, strlen(buffer)+1, 0, (struct sockaddr *) & client, clientAddrLen);

// 	}
// }

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
		fprintf(stderr, "Error: %s <error-rate> muyst be between 0.0 and 1.0\n", argv[1]); 
		exit(EXIT_FAILURE); 
	}
	
	portNumber = atoi(argv[1]);
	return portNumber;
}