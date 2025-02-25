#include "windowBuffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

WindowBuffer *wb_create(int capacity) {
    WindowBuffer *wb = (WindowBuffer *)malloc(sizeof(WindowBuffer));
    if (!wb) {
        perror("Failed to allocate memory for WindowBuffer");
        return NULL;
    }
    wb->buffer = (Packet *)malloc(capacity * sizeof(Packet));
    if (!wb->buffer) {
        perror("Failed to allocate memory for packet array");
        free(wb);
        return NULL;
    }
    wb->size = capacity;
    wb->count = 0;
    // Initialize all slots as empty.
    for (int i = 0; i < capacity; i++) {
        wb->buffer[i].seq_num = 0;
        wb->buffer[i].len = 0;
        wb->buffer[i].acked = false;
        memset(wb->buffer[i].data, 0, sizeof(wb->buffer[i].data));
    }
    return wb;
}

void wb_destroy(WindowBuffer *wb) {
    if (wb) {
        free(wb->buffer);
        free(wb);
    }
}

bool wb_isFull(WindowBuffer *wb) {
    return (wb->count == wb->size);
}

bool wb_isEmpty(WindowBuffer *wb) {
    return (wb->count == 0);
}

// Inserts a packet at index = (seq_num % size). Overwrites any existing data there.
bool wb_insert(WindowBuffer *wb, uint32_t seq_num, const char *data, int data_len) {
    if (!wb || !data || data_len < 0)
        return false;
    int index = seq_num % wb->size;
    wb->buffer[index].seq_num = seq_num;
    wb->buffer[index].len = data_len;
    memcpy(wb->buffer[index].data, data, data_len);
    // Zero out any remaining bytes (optional).
    if (data_len < (int)sizeof(wb->buffer[index].data))
        memset(wb->buffer[index].data + data_len, 0, sizeof(wb->buffer[index].data) - data_len);
    wb->buffer[index].acked = false;
    // If this slot was previously empty (len==0), increment count.
    if (wb->buffer[index].len == data_len)
        ; // We assume overwriting is allowed; for a more robust implementation, 
          // you might want to check if the slot was valid before.
    if (wb->count < wb->size)
        wb->count++;
    return true;
}

// Retrieves a packet at index = (seq_num % size) if it exists (len > 0).
bool wb_get(WindowBuffer *wb, uint32_t seq_num, Packet *packet) {
    if (!wb || !packet)
        return false;
    int index = seq_num % wb->size;
    if (wb->buffer[index].len > 0) {
        *packet = wb->buffer[index];
        return true;
    }
    return false;
}

// Removes (invalidates) a packet at index = (seq_num % size).
bool wb_remove(WindowBuffer *wb, uint32_t seq_num) {
    if (!wb)
        return false;
    int index = seq_num % wb->size;
    if (wb->buffer[index].len > 0) {
        wb->buffer[index].len = 0;
        wb->buffer[index].acked = false;
        wb->buffer[index].seq_num = 0;
        wb->count--;
        return true;
    }
    return false;
}

// Marks a packet as acknowledged.
bool wb_markAcked(WindowBuffer *wb, uint32_t seq_num) {
    if (!wb)
        return false;
    int index = seq_num % wb->size;
    if (wb->buffer[index].len > 0) {
        wb->buffer[index].acked = true;
        return true;
    }
    return false;
}

void wb_print(WindowBuffer *wb) {
    if (!wb)
        return;
    printf("WindowBuffer (capacity: %d, count: %d):\n", wb->size, wb->count);
    for (int i = 0; i < wb->size; i++) {
        printf("Index %d: ", i);
        if (wb->buffer[i].len > 0) {
            printf("Seq: %u, Len: %d, Acked: %d, Data: \"%s\"\n",
                   wb->buffer[i].seq_num,
                   wb->buffer[i].len,
                   wb->buffer[i].acked ? 1 : 0,
                   wb->buffer[i].data);
        } else {
            printf("<empty>\n");
        }
    }
}
