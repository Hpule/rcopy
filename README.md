# CSC 464 - Networks - Hector Pule - 9pm (Lab)

Straight up kms for this shit


## Project Overview
This project implements a reliable file transfer system over UDP using the Selective Repeat protocol. It consists of a server (`server.c`) that serves files to clients, and a client program (`rcopy.c`) that requests and receives files from the server.

## Key Features
- Reliable file transfer over unreliable UDP
- Selective Repeat protocol with configurable window size
- Error detection and handling with checksums
- Packet drop simulation for testing
- Support for transferring text and binary files

## Components

### server.c
Server implementation that handles file transfer requests from clients.

**Key Functions:**
- `main()`: Entry point that initializes server and starts FSM
- `runServerFSM()`: Main server state machine
- `transferData()`: Handles file data transmission using sliding window
- `processClient()`: Processes client requests
- `lookupFilename()`: Verifies if a requested file exists
- `send_data_packet()`: Sends data packets to clients
- `retransmit_packets()`: Handles packet retransmission on timeout

### rcopy.c
Client implementation that requests and receives files from the server.

**Key Functions:**
- `main()`: Entry point that parses arguments and starts client FSM
- `rcopyFSM()`: Main client state machine
- `stateHandshake()`: Establishes connection with server
- `stateFileReceive()`: Receives and writes file data
- `sendFilename()`: Constructs and sends filename request packet
- `sendRR()`, `sendSREJ()`: Send acknowledgment and selective reject packets

### windowBuffer.h / windowBuffer.c
Implements the sliding window buffer for the Selective Repeat protocol.

**Key Structures:**
- `pane`: Holds packet data and sequence number
- `WindowBuffer`: Manages the sliding window

**Key Functions:**
- `init_window()`: Initializes the window buffer
- `slide_window()`: Moves the window forward
- `free_window()`: Cleans up allocated memory

### helperFunctions.h / helperFunctions.c
Helper utilities for packet handling and network operations.

**Key Structures:**
- `pdu_header`: Packet header structure with sequence number, checksum, and flag

**Key Functions:**
- `sendPdu()`: Sends a packet with proper checksum
- `sendAck()`: Convenience function for sending acknowledgments
- `printHexDump()`: Debugging utility for packet inspection

## Usage
### Server
```
./server <error-rate> [port-number]
```
- `error-rate`: Probability of packet errors (0.0-1.0)
- `port-number`: Optional port number (defaults to OS-assigned)

### Client (rcopy)
```
./rcopy <from-filename> <to-filename> <window-size> <buffer-size> <error-rate> <remote-machine> <remote-port>
```
- `from-filename`: File to request from server
- `to-filename`: Local filename to save received data
- `window-size`: Number of packets in the sliding window
- `buffer-size`: Size of each packet buffer
- `error-rate`: Probability of packet errors (0.0-1.0)
- `remote-machine`: Server hostname or IP
- `remote-port`: Server port number

## Protocol Details
1. Client sends filename request to server
2. Server responds with "Ok" if file exists, "Not Ok" otherwise
3. Server sends file data using Selective Repeat protocol
4. Client acknowledges received packets with RR (Ready to Receive)
5. Client requests missing packets with SREJ (Selective Reject)
6. Server sends EOF packet when file transfer is complete
7. Client acknowledges EOF and closes connection

## Packet Types
- Flag 8: Filename Request
- Flag 9: Filename ACK
- Flag 5: RR (Ready to Receive)
- Flag 6: SREJ (Selective Reject)
- Flag 10: EOF/EOF ACK
- Flag 16: Data Packet