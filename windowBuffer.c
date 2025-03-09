#include "windowBuffer.h"
void init_window(WindowBuffer *wb, uint32_t windowSize, uint32_t bufferSize, FILE* input) {

    wb->window_size = windowSize;
    wb->data = input;
    wb->lower = 0;
    wb->currIndex = 0;
    wb->upper = windowSize - 1;
    wb->buffer_size = bufferSize;
    wb->panes = (pane *)malloc(windowSize * sizeof(pane)); //alllocate an entire window of memory
    if (wb->panes == NULL) {
        fprintf(stderr, "Error allocating memory for window buffer.\n");
        exit(EXIT_FAILURE);
    }
    for (uint32_t i = 0; i < windowSize; i++) {
        wb->panes[i].seq_num = MAX_INT_32; //prevent any issues
    }
}

int slide_window(WindowBuffer *wb, int length) {
    wb->lower += length;
    wb->upper += length;
    return 0; // Success
}

void free_window(WindowBuffer *wb) {
    free(wb->panes);
    wb->panes = NULL;
}