#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1F)

struct abuf
{
    char * b;
    int len;
};

#define ABUF_INIT { NULL, 0 }

enum key {
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

void die(const char * s);

void enable_raw_mode(void);
void disable_raw_mode(void);

int read_key(void);
void move_cursor(int key);
void process_keypress(void);

int get_cursor_position(int * rows, int * cols);
int get_window_size(int * rows, int * cols);

void ab_append(struct abuf *ab, const char * s, int len);
void ab_free(struct abuf *ab);

void draw_rows(struct abuf * ab);
void refresh_screen(void);

void init_editor(void);

struct config
{
    int cx,cy;
    int screen_rows;
    int screen_cols;
    struct termios orig_termios;
};

struct config E;

void
die(const char * s)
{
    write(STDIN_FILENO, "\x1b[2J", 4);
    write(STDIN_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void
disable_raw_mode(void)
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    {
        die("tcsetattr");
    }
}

void
enable_raw_mode(void)
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    {
        die("tcgetattr");
    }

    atexit(disable_raw_mode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag &= ~(CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    {
        die("tcsetattr");
    }
}

int
read_key(void)
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
        {
            die("read");
        }
    }

    if (c == '\x1b')
    {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
        {
            return '\x1b';
        }

        if (read(STDIN_FILENO, &seq[1], 1) != 1)
        {
            return '\x1b';
        }

        if (seq[0] == '[')
        {
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                {
                    return '\x1b';
                }

                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                        case '1':
                            return HOME_KEY;
                        case '3':
                            return DEL_KEY;
                        case '4':
                            return END_KEY;
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                        case '7':
                            return HOME_KEY;
                        case '8':
                            return END_KEY;
                    }
                }
            }
            else
            {
                switch (seq[1])
                {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                }
            }
        }
        else if (seq[0] == 'O')
        {
            switch (seq[1])
            {
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
            }
        }

        return '\x1b';
    }
    else
    {
        return c;
    }

    return c;
}

void
move_cursor(int key)
{
    switch (key)
    {
        case ARROW_LEFT:
            if (E.cx != 0)
            {
                E.cx--;
            }
            break;
        case ARROW_RIGHT:
            if (E.cx != E.screen_cols - 1)
            {
                E.cx++;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0)
            {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy != E.screen_rows - 1)
            {
                E.cy++;
            }
            break;
    }
}

void
process_keypress(void)
{
    int c = read_key();
    switch (c)
    {
        case CTRL_KEY('q'):
            write(STDIN_FILENO, "\x1b[2J", 4);
            write(STDIN_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            E.cx = E.screen_cols - 1;
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screen_rows;
                while (times--)
                {
                    move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }
      break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            move_cursor(c);
            break;
    }
}

int
get_cursor_position(int * rows, int * cols)
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDIN_FILENO, "\x1b[6n", 4) != 4)
    {
        return -1;
    }

    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
        {
            break;
        }

        if (buf[i] == 'R')
        {
            break;
        }

        i++;
    }

    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
    {
        return -1;
    }

    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    {
        return -1;
    }

    return 0;
}

int
get_window_size(int * rows, int * cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
        {
            return -1;
        }

        return get_cursor_position(rows, cols);
    }
    else
    {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
        return 0;
    }
}

void
ab_append(struct abuf * ab, const char * s, int len)
{
    char *new = realloc(ab->b, ab->len + len);
    if (new == NULL)
    {
        return;
    }

    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void
ab_free(struct abuf * ab)
{
  free(ab->b);
}

void
draw_rows(struct abuf * ab)
{
    int y;
    for (y = 0; y < E.screen_rows; y++)
    {
        if (y == E.screen_rows / 3)
        {
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
            if (welcomelen > E.screen_cols)
            {
                welcomelen = E.screen_cols;
            }

            int padding = (E.screen_cols - welcomelen) / 2;
            if (padding)
            {
                ab_append(ab, "~", 1);
                padding--;
            }

            while (padding--)
            {
                ab_append(ab, " ", 1);
            }

            ab_append(ab, welcome, welcomelen);
        }
        else
        {
            ab_append(ab, "~", 1);
        }

        ab_append(ab, "\x1b[K", 3);
        if (y < E.screen_rows - 1)
        {
            ab_append(ab, "\r\n", 2);
        }
    }
}

void
refresh_screen(void)
{
    struct abuf ab = ABUF_INIT;

    ab_append(&ab, "\x1b[?25l", 6);
    ab_append(&ab, "\x1b[H", 3);

    draw_rows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    ab_append(&ab, buf, strlen(buf));

    ab_append(&ab, "\x1b[H", 3);
    ab_append(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);

    ab_free(&ab);
}

void
init_editor(void)
{
    E.cx = 0;
    E.cy = 0;

    if (get_window_size(&E.screen_rows, &E.screen_cols) == -1)
    {
        die("get_window_size");
    }
}

int
main(void)
{
    enable_raw_mode();
    init_editor();

    for(;;)
    {
        refresh_screen();
        process_keypress();
    }

    return 0;
}
