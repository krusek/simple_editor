// Next chapter: https://viewsourcecode.org/snaptoken/kilo/03.rawInputAndOutput.html
/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#pragma region defines

#define CTRL_KEY(k) ((k)&0x1f)
#define OCLEAR_SCREEN "\x1b[2J"
#define OCURSOR_HOME "\x1b[H"
#define KILO_VERSION "0.0.1"

enum editorKey
{
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

#pragma endregion

#pragma region data

struct editorConfig
{
    int cx, cy;
    int screenRows;
    int screenCols;
    struct termios orig_termios;
};

struct editorConfig E;

#pragma endregion

#pragma region terminal

void clearScreen()
{
    write(STDOUT_FILENO, OCLEAR_SCREEN, 4);
    write(STDOUT_FILENO, OCURSOR_HOME, 3);
}

void die(const char *s)
{
    clearScreen();
    perror(s);
    exit(1);
}

void disableRowMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");
    atexit(disableRowMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcgetattr");
}

int editorReadKey()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1)
        {
            die("read");
        }
    }

    if (c == '\x1b')
    {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';

        if (seq[0] == '[')
        {
            // [0-9]~
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                    case '3':
                        return DEL_KEY;
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    case '1':
                    case '7':
                        return HOME_KEY;
                    case '4':
                    case '8':
                        return END_KEY;
                    }
                }
                return '\x1b';
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
                case 'F':
                    return END_KEY;
                case 'H':
                    return HOME_KEY;
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

    return c;
}

int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }

    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return getCursorPosition(rows, cols);
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}
#pragma endregion

#pragma region append buffer

struct abuf
{
    char *b;
    int len;
};

#define ABUF_INIT \
    {             \
        NULL, 0   \
    }

void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab)
{
    free(ab->b);
}

#pragma endregion

#pragma region output
void editorDrawRows(struct abuf *ab)
{
    for (int y = 0; y < E.screenRows; ++y)
    {
        if (y == E.screenRows / 3)
        {
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
            if (welcomelen > E.screenCols)
                welcomelen = E.screenCols;
            int padding = (E.screenCols - welcomelen) / 2;
            if (padding)
            {
                abAppend(ab, "~", 1);
                padding--;
            }
            while (padding--)
            {
                abAppend(ab, " ", 1);
            }
            abAppend(ab, welcome, welcomelen);
        }
        else
            abAppend(ab, "~", 1);
        abAppend(ab, "\x1b[K", 3); // clear rest of line
        if (y < E.screenRows - 1)
            abAppend(ab, "\r\n", 2);
    }
}

void editorRefreshScreen()
{
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // hide cursor
    abAppend(&ab, "\x1b[H", 3);    // move cursor to position default (1;1)

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1); // move the cursor to E position
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // show cursor
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}
#pragma endregion

#pragma region input
void editorMoveCursor(int c)
{
    switch (c)
    {
    case ARROW_UP:
        if (E.cy > 0)
            E.cy--;
        break;
    case ARROW_LEFT:
        if (E.cx > 0)
            E.cx--;
        break;
    case ARROW_DOWN:
        if (E.cy < E.screenRows - 1)
            E.cy++;
        break;
    case ARROW_RIGHT:
        if (E.cx < E.screenCols - 1)
            E.cx++;
        break;
    }
}

void editorProcessKeypress()
{
    int c = editorReadKey();

    switch (c)
    {
    case CTRL_KEY('q'):
        clearScreen();
        exit(0);
        break;
    case ARROW_UP:
    case ARROW_LEFT:
    case ARROW_DOWN:
    case ARROW_RIGHT:
        editorMoveCursor(c);
        break;
    case PAGE_DOWN:
    case PAGE_UP:
    {
        int times = E.screenRows;
        while (times--)
        {
            editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
    }
    break;
    case HOME_KEY:
        E.cx = 0;
        break;
    case END_KEY:
        E.cx = E.screenCols - 1;
        break;
    }
}
#pragma endregion

#pragma region init
void initEditor()
{
    E.cx = 0;
    E.cy = 0;
    if (getWindowSize(&E.screenRows, &E.screenCols) == -1)
    {
        die("getWindowSize");
    }
}
#pragma endregion

int main()
{
    enableRawMode();
    initEditor();

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
