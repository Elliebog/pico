struct writebuffer {
    char *data;
    int length;
};

void append_to_buffer(struct writebuffer *wbuffer, const char *str, int len);
void free_buffer(struct writebuffer *wbuffer);