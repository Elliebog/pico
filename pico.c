/*** Includes ***/
#define _GNU_SOURCE

#include <stdio.h>
#include <termios.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>
#include "pico_lib/writebuffer.h"

/*** Defines ***/
#define CTRL_KEY(k) ((k)&0x1f)
#define WBUFF_INIT \
    {              \
        NULL, 0    \
    }
#define PICO_VERSION "0.0.1"
#define EDITOR_NAME "pico"

void close_logger();
void restore_term_state();

/*** Data ***/
typedef struct ed_row
{
    char *data;
    int length;
} ed_row;

struct pico_ctrl
{
    struct termios old_termios;
    int screenrows, screencols;

    // View vars
    int v_offset;

    // Cursor control vars
    int cx, cy;
    int c_currcol;
    int c_row;

    // content vars
    struct ed_row *row;
    int numrows;

    // if a row is longer than the screen it is placed on the rtabindex = 1
    int rtab_index;
};

struct pico_ctrl ed_ctrl;
FILE *log_fp;

enum movekeys
{
    ARROW_LEFT = 'a',
    ARROW_UP = 'w',
    ARROW_RIGHT = 'd',
    ARROW_DOWN = 's'
};

/*** term Settings ***/
int get_window_size(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        return -1;
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

void die(const char *s)
{
    close_logger();
    restore_term_state();
    perror(s);
    exit(1);
}

void disable_raw_mode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &ed_ctrl.old_termios) == -1)
        die("tcsetattr");
}

void enable_raw_mode()
{
    // Revert to normal mode on exit
    struct termios raw = ed_ctrl.old_termios;

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

void restore_term_state()
{
    disable_raw_mode();
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
}

void save_term_state()
{
    // get original termios struct
    if (tcgetattr(STDIN_FILENO, &ed_ctrl.old_termios) == -1)
        die("tcgetattr");
}

void defaultexit()
{
    restore_term_state();
    exit(0);
}

void setup_term()
{
    save_term_state();
    enable_raw_mode();
    // set cursor to top left
    write(STDOUT_FILENO, "\x1b[H", 3);

    if (get_window_size(&ed_ctrl.screenrows, &ed_ctrl.screencols) == -1)
        die("getWindowSize");
}

/*** Input Handling ***/
char ed_read_key()
{
    int nread;
    char c;
    // Wait until a key is pressed.
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }
    // handle escape characters
    if (c == '\x1b')
    {
        // because only the escape character has been read, the actual content
        char seq[8];
        for (int i = 0; i < 8; i++)
        {
            // read the control sequence until it is over
            if (read(STDIN_FILENO, &seq[i], 1) == 0)
                break;
        }

        // Check if it is a valid control sequence (at least 2 bytes in sequence)
        if (strlen(seq) <= 2)
            return '\x1b';
        // Check character sequence and return the according character
        if (seq[0] == '[')
        {
            switch (seq[1])
            {
            // move keys
            case 'A':
                return 'w';
            case 'B':
                return 's';
            case 'C':
                return 'd';
            case 'D':
                return 'a';
            }
        }
        return '\x1b';
    }
    else
    {
        return c;
    }
}

void ed_move_cursor(char c)
{
    switch (c)
    {
    case ARROW_DOWN:
        // If there is no row to move down to prevent it
        if (ed_ctrl.c_row < ed_ctrl.numrows)
        {
            if (ed_ctrl.cy < ed_ctrl.screenrows - 2)
            {
                // Move cursor one row down
                ed_ctrl.cy++;
            }
            else
            {
                // Scroll Down by 1 row
                ed_ctrl.v_offset++;
            }
            ed_ctrl.c_row++;

            if (ed_ctrl.c_currcol > ed_ctrl.row[ed_ctrl.c_row].length)
                ed_ctrl.cx = ed_ctrl.row[ed_ctrl.c_row].length - 2;
            else
                ed_ctrl.cx = ed_ctrl.c_currcol;
        }
        break;
    case ARROW_UP:
        if (ed_ctrl.c_row > 0)
        {
            if (ed_ctrl.cy > 0)
            {
                // Move cursor 1 row up
                ed_ctrl.cy--;
                ed_ctrl.c_row--;
            }
            else
            {
                if (ed_ctrl.v_offset > 0)
                {
                    // Scroll 1 row up with offset
                    ed_ctrl.v_offset--;
                    ed_ctrl.c_row--;
                }
            }
            if (ed_ctrl.c_currcol > ed_ctrl.row[ed_ctrl.c_row].length)
                ed_ctrl.cx = ed_ctrl.row[ed_ctrl.c_row].length - 2;
            else
                ed_ctrl.cx = ed_ctrl.c_currcol;
        }
        break;
    case ARROW_LEFT:
        if (ed_ctrl.cx > 0)
        {
            // Move cursor to left
            ed_ctrl.cx--;
            ed_ctrl.c_currcol = (ed_ctrl.rtab_index * ed_ctrl.screencols) + ed_ctrl.cx;
        }
        else
        {
            // Move cursor to the previous lind (end)

            // check if there is a previous line to move to
            if (ed_ctrl.c_row > 0)
            {
                ed_ctrl.c_row--;
                ed_ctrl.cy--;
                ed_ctrl.cx = ed_ctrl.row[ed_ctrl.c_row].length - 2;
            }
        }
        break;
    case ARROW_RIGHT:
        if (ed_ctrl.cx < ed_ctrl.row[ed_ctrl.c_row].length - 2)
        {
            ed_ctrl.cx++;
            ed_ctrl.c_currcol = (ed_ctrl.rtab_index * ed_ctrl.screencols) + ed_ctrl.cx;
        }
        else
        {
            // Move cursor to the next line start

            // check if there is a line to move to
            if (ed_ctrl.c_row + 1 < ed_ctrl.numrows)
            {
                ed_ctrl.c_row++;
                ed_ctrl.cy++;
                ed_ctrl.cx = 0;
            }
        }
        break;
    }
}

void ed_handle_keypress()
{
    char c = ed_read_key();

    switch (c)
    {
    case CTRL_KEY('q'):
        defaultexit(0);
        break;
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_RIGHT:
    case ARROW_LEFT:
        ed_move_cursor(c);
        break;
    default:
        break;
    }
}

/*** Output ***/

void draw_welcomemsg()
{

    char *lines[3] = {"Welcome to pico", "Press any key to continue", "Press CTRL+q to quit and CTRL+H for help"};
    int linecount = sizeof(lines) / sizeof(lines[0]);

    // to center a text: half of the maxspace - half of the text = (maxspace - text) / 2
    int row = (ed_ctrl.screenrows - linecount) / 2;

    for (int i = 0; i < linecount; i++)
    {
        int col = (ed_ctrl.screencols - strlen(lines[i])) / 2;

        // position cursor in the correct row
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "\x1b[%d;%dH", row, col);
        write(STDOUT_FILENO, buffer, strlen(buffer));

        write(STDOUT_FILENO, lines[i], strlen(lines[i]));
        row++;
    }

    // hide the cursor
    write(STDOUT_FILENO, "\x1b[?25l", 6);
}

void draw_rows(struct writebuffer *wbuf)
{
    for (int y = ed_ctrl.v_offset; y < ed_ctrl.v_offset + ed_ctrl.screenrows; y++)
    {
        if (y < ed_ctrl.numrows)
        {
            ed_row *currow = &ed_ctrl.row[y];
            // Calculate start and end indexes based on rtab_index
            int startidx = ed_ctrl.rtab_index * ed_ctrl.screencols;
            int endidx = (ed_ctrl.rtab_index + 1) * ed_ctrl.screencols;

            if (startidx <= currow->length)
            {
                if (ed_ctrl.rtab_index >= 1)
                {
                    // print character to indicate the row started in the previous rtab index
                    // set foreground color black and background to white
                    append_to_buffer(wbuf, "\x1b[30m", 5);
                    append_to_buffer(wbuf, "\x1b[47m", 5);
                    append_to_buffer(wbuf, "<", 1);

                    // reset colors
                    append_to_buffer(wbuf, "\x1b[39m", 5);
                    append_to_buffer(wbuf, "\x1b[49m", 5);
                }

                // contentdiff = amount of characters left in the row
                int contentdiff = currow->length - startidx;
                append_to_buffer(wbuf, &ed_ctrl.row[y].data[startidx], contentdiff);

                if (currow->length > endidx)
                {
                    // print character to indicate the row isn't finished
                    // set foreground color black and background to white
                    append_to_buffer(wbuf, "\x1b[30m", 5);
                    append_to_buffer(wbuf, "\x1b[47m", 5);
                    append_to_buffer(wbuf, ">", 1);

                    // reset colors
                    append_to_buffer(wbuf, "\x1b[39m", 5);
                    append_to_buffer(wbuf, "\x1b[49m", 5);
                }

                // carriage return for next line
                append_to_buffer(wbuf, "\r", 1);
            }
        }
    }
}

void ed_refresh_screen()
{
    struct writebuffer wbuffer = WBUFF_INIT;

    // VT100 escape sequence
    // clear screen
    append_to_buffer(&wbuffer, "\x1b[?25l", 6);
    append_to_buffer(&wbuffer, "\x1b[2J", 4);
    append_to_buffer(&wbuffer, "\x1b[H", 3);

    // Draw rows
    draw_rows(&wbuffer);

    // position the cursor
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "\x1b[%d;%dH", ed_ctrl.cy + 1, ed_ctrl.cx + 1);
    append_to_buffer(&wbuffer, buffer, strlen(buffer));

    // make Cursor visible
    append_to_buffer(&wbuffer, "\x1b[?25h", 6);

    // write the Buffer
    write(STDOUT_FILENO, wbuffer.data, wbuffer.length);

    free_buffer(&wbuffer);
}

/*** Line Data ***/
void append_row(char *str, int len)
{
    ed_ctrl.row = realloc(ed_ctrl.row, sizeof(ed_row) * (ed_ctrl.numrows + 1));

    int idx = ed_ctrl.numrows;
    ed_ctrl.row[idx].length = len;
    ed_ctrl.row[idx].data = malloc(len + 1);
    memcpy(ed_ctrl.row[idx].data, str, len);
    ed_ctrl.row[idx].data[len] = '\0';

    ed_ctrl.numrows++;
}

/*** File Loading ***/

void open_file(char *filepath)
{
    FILE *fp;
    fp = fopen(filepath, "r");

    char *buffer = NULL;
    size_t buff_cap = 0;
    int linelen;
    while ((linelen = getline(&buffer, &buff_cap, fp)) != -1)
    {
        while (linelen > 0 && (buffer[linelen] == '\r' || buffer[linelen] == '\n'))
        {
            linelen--;
        }
        append_row(buffer, linelen);
        // write the content to the editor
    }

    fclose(fp);
    free(buffer);
}

/*** Logging ***/

void close_logger()
{
    fclose(log_fp);
}

void setup_logger()
{
    log_fp = fopen("logs/testlog.txt", "w+");

    time_t t;
    struct tm *timeinfo;
    char buffer[80];

    time(&t);
    timeinfo = localtime(&t);

    strftime(buffer, 30, "%d-%m-%Y %X", timeinfo);

    fprintf(log_fp, "# Log of %s from %s\n", EDITOR_NAME, buffer);

    atexit(close_logger);
}

void log_msg(const char *msg, ...)
{
    va_list args;
    va_start(args, msg);

    vfprintf(log_fp, msg, args);

    va_end(args);
}

/*** Init ***/
int main(int argc, char *argv[])
{
    setup_term();
    setup_logger();

    // Read file else display welcome message
    if (argc == 2)
    {
        log_msg("Opening %s");
        open_file(argv[1]);
    }
    else
    {
        draw_welcomemsg();
        // wait until a key is pressed to remove the
        char c = ed_read_key();
        if (c == CTRL_KEY('q'))
        {
            write(STDOUT_FILENO, "\x1b[?25h", 6);
            defaultexit();
        }
    }
    // todo: Help screen

    // Editor loop
    while (1)
    {
        ed_refresh_screen();
        ed_handle_keypress();
    }
    return 0;
}