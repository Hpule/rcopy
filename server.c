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

void runServer(int socket); 
void processClient(int socketNum, int dataLen, char * buffer, struct sockaddr_in6 *client, socklen_t clientAddrLen); 
int  checkArgs(int argc, char *argv[]);
bool lookupFilename(const char *filename); 

int main ( int argc, char *argv[]  )
{ 
    int socketNum = 0;				
    int portNumber = 0;
	double dropped_packet_rate = 0.0;  

    portNumber = checkArgs(argc, argv);
    socketNum = udpServerSetup(portNumber);
	dropped_packet_rate = atof(argv[1]); 
    // sendErr_init(dropped_packet_rate, DROP_ON, FLIP_OFF, DEBUG_ON, RSEED_ON);

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

		pid_t pid = fork(); 
		if(pid < 0){
			perror("fork"); 
		}
		else if(pid == 0){
			processClient(socketNum, dataLen, buffer, &client, clientAddrLen);
			exit(0); 
		}
    }
}

void processClient(int socketNum, int dataLen, char * buffer, struct sockaddr_in6 *client, socklen_t clientAddrLen){
    // Print hex dump of the received packet.
    printf("Received packet (len=%d):\n", dataLen);
    printHexDump("", buffer, dataLen);

    // Extract the PDU header.
    pdu_header header;
    memcpy(&header, buffer, sizeof(pdu_header));
    uint32_t seq = ntohl(header.seq);
    uint16_t chksum = ntohs(header.checksum);
    uint8_t flag = header.flag;
    printf("PDU Header:\n");
    printf("  Sequence: %u\n", seq);
    printf("  Checksum: 0x%04x\n", chksum);
    printf("  Flag: %u\n", flag);

    // Calculate payload length.
    int payload_len = dataLen - sizeof(pdu_header);
    if (payload_len > 0) {
        // Treat payload as a byte array.
        uint8_t *p = (uint8_t *)(buffer + sizeof(pdu_header));

        if (flag == 8) {
            if (payload_len < 1 + 4 + 4) {
                fprintf(stderr, "Payload too short for a filename packet\n");
            } else {
                uint8_t name_len = p[0];
                if (payload_len < 1 + name_len + 4 + 4) {
                    fprintf(stderr, "Payload too short for filename and sizes\n");
                } else {
                    char filename[MAX_FILENAME + 1];
                    if (name_len > MAX_FILENAME)
                        name_len = MAX_FILENAME;
                    memcpy(filename, p + 1, name_len);
                    filename[name_len] = '\0';

                    uint32_t bufSize = 0, winSize = 0;
                    memcpy(&bufSize, p + 1 + name_len, 4);
                    memcpy(&winSize, p + 1 + name_len + 4, 4);
                    bufSize = ntohl(bufSize);
                    winSize = ntohl(winSize);

                    printf("Parsed Filename: %s\n", filename);
                    printf("  Buffer size: %u\n", bufSize);
                    printf("  Window size: %u\n", winSize);

                    // Lookup the file (searches in "test_files" directory).
                    bool fileFound = lookupFilename(filename);
                    
                    // Prepare ACK header.
                    pdu_header ack;
                    ack.seq = htonl(0);   // You might update this with the proper sequence.
                    ack.flag = 9;         // Flag 9 indicates ACK.
                    ack.checksum = 0;     // Checksum will be computed in sendPdu.
                    
                    if (fileFound) {
                        const char *okStr = "Ok";
                        sendPdu(socketNum, client, ack, okStr, strlen(okStr));
                    } else {
                        const char *notOkStr = "Not Ok";
                        sendPdu(socketNum, client, ack, notOkStr, strlen(notOkStr));
                    }
                }
            }
        } else {
            // For non-filename packets, treat payload as a text string.
            char text_payload[MAXBUF - sizeof(pdu_header) + 1];
            memcpy(text_payload, buffer + sizeof(pdu_header), payload_len);
            text_payload[payload_len] = '\0';
            printf("Payload: \"%s\"\n", text_payload);
        }
    }
    printf("--------------------------------------------------\n");
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