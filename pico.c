#include <termios.h>
#include <stdlib.h>
#include <unistd.h>

struct termios old_termios;

void enableRawMode() {
    //Revert to normal mode on exit
    atexit(disableRawMode);

    struct termios raw = old_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios);
}

int main() {
    //get original termios struct
    tcgetattr(STDIN_FILENO, &old_termios);

   //get original termios struct
    enableRawMode();

    char c;
    while(read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
    return 0;

}
