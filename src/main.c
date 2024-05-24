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
#include <stdarg.h>
#include <time.h>

/* macros */
#define CTRL_KEY(x) ((x) & 0x1f)
#define VERSION "v0.0.1"
#define TAB_STOP 8

/* enums */
enum EditorKeys {
    BACKSPACE = 127,
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
    size_t rsize;
    char *chars;
    char *render;
} Row;

typedef struct {
    struct termios default_termios;
    int screenrows, screencols;
    int cy, cx;
    int rowoff, coloff;
    Row *row;
    int rx;
    int numrows;
    char *filename;
    char statusmsg[100];
    time_t statustime;
} EditorConfig;

typedef struct {
    char *b;
    size_t len;
} String;

/* function declarations */
static void die(const char *s, ...);
static void disable_raw_mode(void); /* needs E.default_termis to be set */
static void enable_raw_mode(void);
static void get_window_size(int *rows, int *cols);
static int row_rx_to_cx(Row *row, int cx);
static void update_row(Row *row);
static void append_row(const char *s, size_t len);
static void row_insert_char(Row *row, int at, int c);
static void insert_char(int c);
static char *rows_to_string(int *len);
static void editor_open(const char *filename);
static void save_file(void);
static void s_append(String *sb, const char *s, size_t len);
static void scroll(void);
static void draw_rows(String *sb, int amount);
static void draw_status_bar(String *sb);
static void draw_status_message(String *sb);
static void refresh_screen(void);
static void set_status_message(const char *fmt, ...);
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
    set_status_message("HELP: C-q = quit");
    while (1) {
        refresh_screen();
        process_keypress();
    }
    return 0;
}

/* function definitions */
void die(const char *s, ...)
{
    fprintf(stderr, "\x1b[2J\x1b[H");
    va_list args;
    va_start(args, s);
    vfprintf(stderr, s, args);
    va_end(args);
    fprintf(stderr, "\n");
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

int row_rx_to_cx(Row *row, int cx)
{
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx = (TAB_STOP - 1) - (rx % TAB_STOP);
        rx++;
    }
    return rx;
}

void update_row(Row *row)
{
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') tabs++;
    }

    free(row->render);
    row->render = (char *) malloc(row->size + tabs * (TAB_STOP - 1) + 1);

    int idx;
    for (j = 0, idx = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % TAB_STOP != 0)
                row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }

    row->render[idx] = '\0';
    row->rsize = idx;
}

void append_row(const char *s, size_t len)
{
    E.row = realloc(E.row, sizeof(Row) * (E.numrows + 1));

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = (char *) malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    update_row(&E.row[at]);

    E.numrows++;
}

void row_insert_char(Row *row, int at, int c)
{
    if (at < 0 || at > row->size)
        at = row->size;

    row->chars = (char *) realloc(row->chars, row->size + 2);

    memmove((row->chars + (at + 1)), (row->chars + at), row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    update_row(row);
}

void insert_char(int c)
{
    if (c != '\0') {
        if (E.cy == E.numrows) {
            append_row("", 0);
        }
        row_insert_char((E.row + E.cy), E.cx, c);
        E.cx++;
    }
}

char *rows_to_string(int *len)
{
    int totlen = 0;
    int j;
    for (j = 0; j < E.numrows; j++) {
        totlen += E.row[j].size + 1;
    }

    *len = totlen;

    char *buf = (char *) malloc(totlen);
    char *p = buf;
    for (j = 0; j < E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }

    return buf;
}

void editor_open(const char *filename)
{
    free(E.filename);
    E.filename = strdup(filename);

    FILE *fr = fopen(filename, "r");
    if (fr == NULL)
        die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while ((linelen = getline(&line, &linecap, fr)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                               line[linelen - 1] == '\r'))
            linelen--;
        append_row(line, linelen);
    }

    free(line);
    fclose(fr);
}

void save_file(void)
{
    if (E.filename == NULL)
        return;

    int len;
    char *buf = rows_to_string(&len);

    FILE* fw = fopen(E.filename, "w");
    if (fw == NULL) {
        set_status_message("saving the file was not successful because: %s", strerror(errno));
        return;
    }

    if (fprintf(fw, "%s", buf) < 0) {
        set_status_message("saving the file was not successful because: %s", strerror(errno));
        return;
    }

    set_status_message("%d bytes written to the disk", len);

    fclose(fw);
    free(buf);
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

void scroll(void)
{
    E.rx = 0;
    if (E.cy < E.numrows) {
        E.rx = row_rx_to_cx(&E.row[E.cy], E.cx);
    }

    if (E.cy < E.rowoff)
        E.rowoff = E.cy;
    else if (E.cy >= E.rowoff + E.screenrows)
        E.rowoff = E.cy - E.screenrows + 1;

    if (E.rx < E.coloff)
        E.coloff = E.rx;
    else if (E.rx >= E.coloff + E.screencols)
        E.coloff = E.rx - E.screencols + 1;
}

void draw_rows(String *sb, int amount)
{
    int y;
    for (y = 0; y < amount; y++) {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) {
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
            else {
                s_append(sb, "~", 1);
            }
        } else {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            s_append(sb, &E.row[filerow].render[E.coloff], len);
        }

        s_append(sb, "\x1b[K", 3);
        s_append(sb, "\r\n", 2);
    }
}

void draw_status_bar(String *sb)
{
    s_append(sb, "\x1b[7m", 4);
    
    char buf[100], rbuf[100];
    int len = snprintf(buf, sizeof(buf), "%.20s - %d lines",
                          E.filename ? E.filename : "[New File]", E.numrows);

    int rlen;

    if (E.cy + 1 <= E.numrows) {
        rlen = snprintf(rbuf, sizeof(rbuf), "%d/%d",
                            E.cy + 1, E.numrows);
    } else {
        rlen = snprintf(rbuf, sizeof(rbuf), "bottom");
    }

    if (len > E.screencols) len = E.screencols;

    s_append(sb, buf, len);
    
    for (; len < E.screencols; len++) {
        if (E.screencols - len == rlen) {
            s_append(sb, rbuf, rlen);
            break;
        }
        s_append(sb, " ", 1);
    }

    s_append(sb, "\x1b[m\r\n", 5);
}

void draw_status_message(String *sb)
{
    s_append(sb, "\x1b[K", 3);
    int len = strlen(E.statusmsg);
    if (len > E.screencols) len = E.screencols;
    if (len && time(NULL) - E.statustime < 5)
        s_append(sb, E.statusmsg, len);
}

void refresh_screen(void)
{
    scroll();
    String sb = { NULL, 0 };
    s_append(&sb, "\x1b[?25l\x1b[H", 9);

    draw_rows(&sb, E.screenrows);
    draw_status_bar(&sb);
    draw_status_message(&sb);

    char buf[100];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
    s_append(&sb, buf, strlen(buf));

    s_append(&sb, "\x1b[?25h", 6);
    write(STDIN_FILENO, sb.b, sb.len);
}

void set_status_message(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, args);
    va_end(args);
    E.statustime = time(NULL);
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
    Row *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    switch (key) {
    case ARROW_LEFT:
        if (E.cx != 0) {
            E.cx--;
        } else if (E.cy > 0) {
            E.cy--;
            E.cx = E.row[E.cy].size;
        }
        break;
    case ARROW_RIGHT:
        if (row && E.cx < row->size) {
            E.cx++;
        } else if (row && E.cx == row->size) {
            E.cy++;
            E.cx = 0;
        }
        break;
    case ARROW_UP:
        if (E.cy != 0) {
            E.cy--;
        }
        break;
    case ARROW_DOWN:
        if (E.cy < E.numrows) {
            E.cy++;
        }
        break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen)
        E.cx = rowlen;
}

void process_keypress(void)
{
    int c;
    c = read_key();
    switch (c) {
    case '\r':
        /* TODO */
        break;

    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
        exit(0);
        break;

    case CTRL_KEY('s'):
        save_file();
        break;

    case HOME_KEY:
        E.cx = 0;
        break;

    case END_KEY:
        if (E.cy < E.numrows)
            E.cx = E.row[E.cy].size;
        break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
        /* TODO */
        break;

    case PAGE_UP:
    case PAGE_DOWN:
        {
            if (c == PAGE_UP) {
                E.cy = E.rowoff;
            } else if (c == PAGE_DOWN) {
                E.cy = E.rowoff + E.screenrows - 1;
                if (E.cy > E.numrows)
                    E.cy = E.numrows;
            }

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
    
    case CTRL_KEY('l'):
    case '\x1b':
        break;
    
    default:
        insert_char(c);
        break;
    }
}

void init(void)
{
    get_window_size(&E.screenrows, &E.screencols);
    E.screenrows -= 2;
    E.cy = 0;
    E.cx = 0;
    E.numrows = 0;
    E.row = NULL;
    E.rowoff = 0;
    E.coloff = 0;
    E.rx = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statustime = 0;
}