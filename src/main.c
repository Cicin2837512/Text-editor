/*
 * main.c
 * it's just 1 file, no more info needed (check README.md and LICENSE)
 */

/* headers */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#define _DEFAULT_SOURCE

#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>

/* macros */
#define CTRL_KEY(x) ((x) & 0x1f)
#define VERSION "v0.0.1"

/* enums */
enum EditorKeys {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/* structs */
typedef struct {
    size_t size;
    char *chars;
} Row;

typedef struct {
    struct termios default_termios;
    int screenrows, screencols;
    int cy, cx;
    Row *row;
    int numrows;
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
static void append_row(const char *s, size_t len);
static void editor_open(const char *filename);
static void s_append(String *sb, const char *s, size_t len);
static void draw_rows(String *sb, int amount);
static void refresh_screen(void);
static int read_key(void);
static void move_cursor(int key);
static void process_keypress(void);
static void init(void);

/* global variables */
EditorConfig E;

/* main */
int main(int argc, char *argv[])
{
    if (argc != 2)
        die("Wrong number of arguments!");
    enable_raw_mode();
    init();
    editor_open(argv[1]);
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

void append_row(const char *s, size_t len)
{
    E.row = realloc(E.row, sizeof(Row) * (E.numrows + 1));

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = (char *) malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows++;
}

void editor_open(const char *filename)
{
    FILE *fr = fopen(filename, "r");
    if (fr == NULL)
        die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    linelen = getline(&line, &linecap, fr);

    while ((linelen = getline(&line, &linecap, fr)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                               line[linelen - 1] == '\r'))
            linelen--;
        append_row(line, linelen);
    }

    free(line);
    fclose(fr);
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

        if (y >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3) {

                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                                          "Text editor %s", VERSION);
                if (welcomelen > E.screencols)
                    welcomelen = E.screencols;

                int padding = (E.screencols - welcomelen) / 2;

                if (padding > 0)
                {
                    s_append(sb, "~", 1);
                }

                while (padding--)
                {
                    s_append(sb, " ", 1);
                }

                s_append(sb, welcome, welcomelen);
            }
            else
            {
                s_append(sb, "~", 1);
            }
        } else {
            int len = E.row[y].size;
            if (len > E.screencols) len = E.screencols;
            s_append(sb, E.row[y].chars, len);
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

int read_key(void)
{
    char c = '\0';
    int nread;
    if ((nread = read(STDIN_FILENO, &c, 1)) < 0) {
        if (nread < 0 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) < 0) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) < 0) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {

                if (read(STDIN_FILENO, &seq[2], 1) < 0) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                    case '1': return HOME_KEY;
                    case '3': return DEL_KEY;
                    case '4': return END_KEY;
                    case '5': return PAGE_UP;
                    case '6': return PAGE_DOWN;
                    case '7': return HOME_KEY;
                    case '8': return END_KEY;
                    }
                }

            }
            else {
                switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

void move_cursor(int key)
{
    switch (key) {
    case ARROW_LEFT:
        if (E.cx > 0)
            E.cx--;
        break;
    case ARROW_DOWN:
        if (E.cy < E.screenrows - 1)
            E.cy++;
        break;
    case ARROW_UP:
        if (E.cy > 0)
            E.cy--;
        break;
    case ARROW_RIGHT:
        if (E.cx < E.screencols - 1)
            E.cx++;
        break;
    }
}

void process_keypress(void)
{
    int c;
    c = read_key();
    switch (c) {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
        exit(0);
        break;

    case HOME_KEY:
        E.cx = 0;
        break;

    case END_KEY:
        E.cx = E.screencols - 1;
        break;

    case PAGE_UP:
    case PAGE_DOWN:
        {
            int times = E.screenrows;
            while (times--)
                move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
        break;

    case ARROW_DOWN:
    case ARROW_UP:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        move_cursor(c);
        break;
    }
}

void init(void)
{
    get_window_size(&E.screenrows, &E.screencols);
    E.cy = 0;
    E.cx = 0;
    E.numrows = 0;
    E.row = NULL;
}