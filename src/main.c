/*
 * main.c
 * it's just 1 file, no more info needed (check README.md and LICENSE)
 */

/* headers */
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>

/* macros */
#define CTRL_KEY(x) ((x) & 0x1f)

/* structs */
typedef struct {
    struct termios default_termios;
} EditorConfig;

/* function declarations */
static void die(const char *s);
static void disable_raw_mode(void); /* needs E.default_termis to be set */
static void enable_raw_mode(void);

/* global variables */
EditorConfig E;

/* main */
int main(void)
{
    enable_raw_mode();
    while (1) {
        char c;
        read(STDIN_FILENO, &c, 1);
        if (c == CTRL_KEY('q'))
            break;
    }
    return 0;
}

/* function definitions */
static void die(const char *s)
{
    fprintf(stderr, "Error: %s", s);
    exit(1);
}

void disable_raw_mode(void)
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.default_termios) < 0)
        die("tcsetattr");
}

void enable_raw_mode(void)
{
    if (tcgetattr(STDIN_FILENO, &E.default_termios) < 0)
        die("tcgetattr");
    atexit(disable_raw_mode);

    struct termios raw_termios = E.default_termios;
    raw_termios.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw_termios.c_iflag &= ~(IXON);
    raw_termios.c_oflag &= ~(OPOST);
    raw_termios.c_cflag |= (CS8);
    raw_termios.c_cc[VMIN] = 0;
    raw_termios.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_termios) < 0)
        die("tcsetattr");
}