/*** Includes ***/
#include <termios.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <string.h>

/*** Defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define WBUFF_INIT {NULL, 0}

void restore_term_state();

/*** Data ***/
struct pico_config {
    struct termios old_termios;
    int screenrows;
    int screencols;
};

struct pico_config config;

struct writebuffer {
    char *data;
    int length;
};

/*** term Settings ***/
int get_window_size(int *rows, int *cols)
{
    struct winsize ws;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return -1;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

void die(const char *s)
{
    restore_term_state();
    perror(s);
    exit(1);
}

void disable_raw_mode() {
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &config.old_termios) == -1)
        die("tcsetattr");
}


void enable_raw_mode() {
    //Revert to normal mode on exit
    struct termios raw = config.old_termios;

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP| IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

void restore_term_state() {
    disable_raw_mode();
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
}

void save_term_state() { 
    //get original termios struct
    if(tcgetattr(STDIN_FILENO, &config.old_termios) == -1)
        die("tcgetattr");
}

void defaultexit() {
    restore_term_state();
    exit(0);
}

void setup_term() {
    save_term_state();
    enable_raw_mode();
    //set cursor to top left
    write(STDOUT_FILENO, "\x1b[H", 3);

    if(get_window_size(&config.screenrows, &config.screencols) == -1)
        die("getWindowSize");
}

/*** Input Handling ***/
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

char ed_read_key() {
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if(nread == -1 && errno != EAGAIN)
            die("read");
    }
    return c;
}

void ed_handle_keypress() {
    char c = ed_read_key();

    switch (c) {
        case CTRL_KEY('q'):
            defaultexit(0);
            break;
        default:
            break;
    }
}

/*** Output ***/

void draw_rows(struct writebuffer *wbuf) {
    for (int y = 0; y < config.screencols - 1; y++) {
        append_to_buffer(wbuf, "-\r\n", 3);
    }
    append_to_buffer(wbuf, "\r\n", 2);
}

void ed_refresh_screen() {
    struct writebuffer wbuffer = WBUFF_INIT;

    //VT100 escape sequence
    //clear screen
    append_to_buffer(&wbuffer, "\x1b[2J", 4);

    //Draw rows
    draw_rows(&wbuffer);
    //reset Cursor
    append_to_buffer(&wbuffer,"\x1b[H", 3);
    
    //write the Buffer
    write(STDOUT_FILENO, wbuffer.data, wbuffer.length);

    free_buffer(&wbuffer);
}

/*** Init ***/
int main() {
    setup_term();
    while (1)
    {
        ed_refresh_screen();
        ed_handle_keypress();
    }
    return 0;
}