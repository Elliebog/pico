#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "writebuffer.h"

void append_to_buffer(struct writebuffer *wbuffer, const char *str, int len) {
    char *newptr = realloc(wbuffer->data, wbuffer->length + len);

    if(newptr == NULL)
        return;
    memcpy(&newptr[wbuffer->length], str, len);
    wbuffer->data = newptr;
    wbuffer->length += len;
}

void free_buffer(struct writebuffer *wbuffer) {
    free(wbuffer->data);
}