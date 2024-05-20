/*
 * main.c
 * it's just 1 file, no more info needed (check README.md and LICENSE)
 */

/* headers */
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

/* macros */
#define CTRL_KEY(x) ((x) & 0x1f)
#define VERSION "v0.0.1"

/* structs */
typedef struct {
    struct termios default_termios;
    int screenrows, screencols;
    int cy, cx;
} EditorConfig;

typedef struct {
    char *b;
    size_t len;
} String;

/* function declarations */
static void die(const char *s);
static void disable_raw_mode(void); /* needs E.default_termis to be set */
static void enable_raw_mode(void);
static void get_window_size(int *rows, int *cols);
static void s_append(String *sb, const char *s, size_t len);
static void draw_rows(String *sb, int amount);
static void refresh_screen(void);
static char read_key(void);
static void move_cursor(char key);
static void process_keypress(void);
static void init(void);

/* global variables */
EditorConfig E;

/* main */
int main(void)
{
    enable_raw_mode();
    init();
    while (1) {
        refresh_screen();
        process_keypress();
    }
    return 0;
}

/* function definitions */
void die(const char *s)
{
    fprintf(stderr, "\x1b[2J\x1b[HError: %s", s);
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

void get_window_size(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0) {
        die("ioctl");
    } else {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    }
}

void s_append(String *sb, const char *s, size_t len)
{
    size_t new_len = sb->len + len;
    char *b = (char *) malloc(new_len);
    if (b == NULL)
        die("malloc");
    memcpy(b, sb->b, sb->len);
    memcpy((b + sb->len), s, len);
    free(sb->b);

    sb->b = b;
    sb->len = new_len;
}

void draw_rows(String *sb, int amount)
{
    int y;
    for (y = 0; y < amount; y++) {

        if (y == amount / 3) {

            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome),
                                      "Text editor %s", VERSION);
            if (welcomelen > E.screencols)
                welcomelen = E.screencols;

            int padding = (E.screencols - welcomelen) / 2;

            if (padding > 0) {
                s_append(sb, "~", 1);
            }

            while (padding--) {
                s_append(sb, " ", 1);
            }

            s_append(sb, welcome, welcomelen);

        }
        else {
            s_append(sb, "~", 1);
        }

        s_append(sb, "\x1b[K", 3);
        if (y < amount - 1)
            s_append(sb, "\r\n", 2);
    }
}

void refresh_screen(void)
{
    String sb = { NULL, 0 };
    s_append(&sb, "\x1b[?25l\x1b[H", 9);
    draw_rows(&sb, E.screenrows);

    char buf[100];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    s_append(&sb, buf, strlen(buf));

    s_append(&sb, "\x1b[?25h", 6);
    write(STDIN_FILENO, sb.b, sb.len);
}

char read_key(void)
{
    char c = '\0';
    int nread;
    if ((nread = read(STDIN_FILENO, &c, 1)) < 0) {
        if (nread < 0 && errno != EAGAIN) die("read");
    }
    return c;
}

void move_cursor(char key)
{
    switch (key) {
    case 'h':
        if (E.cx > 0)
            E.cx--;
        break;
    case 'j':
        if (E.cy < E.screenrows)
            E.cy++;
        break;
    case 'k':
        if (E.cy > 0)
            E.cy--;
        break;
    case 'l':
        if (E.cx < E.screencols)
            E.cx++;
        break;
    }
}

void process_keypress(void)
{
    char c;
    c = read_key();
    switch (c) {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
        exit(0);
        break;

    case 'h':
    case 'j':
    case 'k':
    case 'l':
        move_cursor(c);
        break;
    }
}

void init(void)
{
    get_window_size(&E.screenrows, &E.screencols);
    E.cy = 0;
    E.cx = 0;
}