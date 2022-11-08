/*** Includes ***/
#include <termios.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <string.h>
#include "pico_lib/writebuffer.h"

/*** Defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define WBUFF_INIT {NULL, 0}
#define PICO_VERSION "0.0.1"

void restore_term_state();

/*** Data ***/
struct pico_vars {
    struct termios old_termios;
    int screenrows, screencols;
    int cx, cy;
};
 
struct pico_vars config;

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
char ed_read_key() {
    int nread;
    char c;
    //Wait until a key is pressed. 
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

void draw_welcomemsg() {
    
    char *lines[3] = {"Welcome to pico", "Press any key to continue" , "Press CTRL+q to quit and CTRL+H for help"};
    int linecount = sizeof(lines) / sizeof(lines[0]);

    //to center a text: half of the maxspace - half of the text = (maxspace - text) / 2
    int row = (config.screenrows - linecount) / 2;
    
    for (int i = 0; i < linecount; i++) {
        int col = (config.screencols - strlen(lines[i])) / 2;

        // position cursor in the correct row
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "\x1b[%d;%dH", row, col);
        write(STDOUT_FILENO, buffer, strlen(buffer));

        write(STDOUT_FILENO, lines[i], strlen(lines[i]));
        row++;
    }

    //hide the cursor
    write(STDOUT_FILENO, "\x1b[?25l", 6);
}

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
    append_to_buffer(&wbuffer, "\x1b[?25l", 6);
    append_to_buffer(&wbuffer, "\x1b[2J", 4);
    append_to_buffer(&wbuffer, "\x1b[H", 3);

    //Draw rows
    draw_rows(&wbuffer);

    //position the cursor 
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "\x1b[%d;%dH", config.cy + 1, config.cx + 1);
    append_to_buffer(&wbuffer, buffer, strlen(buffer));

    // make Cursor visible
    append_to_buffer(&wbuffer, "\x1b[?25h", 6);

    //write the Buffer
    write(STDOUT_FILENO, wbuffer.data, wbuffer.length);

    free_buffer(&wbuffer);
}

/*** Init ***/
int main() {
    setup_term();
    
    //display welcome message
    draw_welcomemsg();

    //wait until a key is pressed to remove the welcome message
    char c = ed_read_key();
    if(c == CTRL_KEY('q')) {
        write(STDOUT_FILENO, "\x1b[?25h", 6);
        defaultexit();
    }

    //todo: Help screen

    //Editor loop
    while (1)
    {
        ed_refresh_screen();
        ed_handle_keypress();
    }
    return 0;
}