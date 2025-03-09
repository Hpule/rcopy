#ifndef WINDOWBUFFER_H
#define WINDOWBUFFER_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BUFFER 1400
#define MAX_INT_32 0xFFFFFFFF

typedef struct {
    uint32_t seq_num;   //holds sequence number(likely a global variable to be incremented)
    uint8_t data[MAX_BUFFER];
} pane;  //heh. pane holds each data from file to be sent.

typedef struct {
    pane *panes;
    FILE *data;  //file pointer to data stored on server 
    uint32_t window_size;
    uint32_t buffer_size;
    uint32_t lower;  // lower edge
    uint32_t currIndex;  // current index
    uint32_t upper;     // upper edge, will 
} WindowBuffer;

void init_window(WindowBuffer *wb, uint32_t windowSize, uint32_t bufferSize, FILE* input);

// Moves the window forward by specified amount ((sizeof struct) * amount)
int slide_window(WindowBuffer *wb, int length);

// Frees allocated memory when done sliding around
void free_window(WindowBuffer *wb);

#endif
