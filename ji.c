//-----------------------------------------------//
//                  Headers                      //
//-----------------------------------------------//
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

//-----------------------------------------------//
//                    Defines                    //
//-----------------------------------------------//

// Macro for control keys: When control is held with a key. The bit sequence
// used in the logical AND is used to indicate control keys -> Ascii removes the
// 5 and 6 bit to indicate a control key or sum like that
#define CTRL_KEY(k) ((k) & 0x1f)
#define JI_VERSION "0.0.1dev"
#define ABUF_INIT {NULL, 0}
//-----------------------------------------------//
//                    Data                       //
//-----------------------------------------------//
typedef struct erow {
  int size;
  char *chars;
} erow;

struct editorConfig {
  int cx, cy;
  int screenrows;
  int screencols;
  int numRows;
  erow row;
  struct termios orig_termios;
};
struct editorConfig E;

enum editorKeys {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  HOME_KEY,
  DEL_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};
//-----------------------------------------------//
//                  Terminal                     //
//-----------------------------------------------//

void die(const char *s) {
  // exit programme with correct error handling
  //  escape sequences to clear screen on exit
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcgetattr");
}

void enableRawMode() {
  // A collection of flags to enable "Raw Mode"(Terrible name btw 😒)
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRawMode);
  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= 9) {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '1':
            return HOME_KEY;
          case '2':
            return END_KEY;
          case '3':
            return DEL_KEY;
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
      } else {
        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
    } else if (seq[0] == '0') {
      switch (seq[1]) {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }
    return '\x1b';
  } else {
    return c;
  }

  return c;
}

int getCursorPos(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;
  while (i < sizeof(buf) - 1) {
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

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;
  // If it succeeded, we pass the values back by setting the int references
  // that were passed to the function. (This is a common approach to having
  // functions return multiple values in C. It also allows you to use the
  // return value to indicate success or failure.)

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return getCursorPos(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

//-----------------------------------------------//
//                  File i/o                     //
//-----------------------------------------------//
void editorOpen(char *fileName) {
  FILE *fp = fopen(fileName, "r");
  if (!fp)
    die("fopen");

  char *line = NULL;
  size_t lineCap = 0;
  ssize_t lineLen;
  lineLen = getline(&line, &lineCap, fp);
  if (lineLen != -1) {
    while (lineLen > 0 &&
           (line[lineLen - 1] == '\n' || line[lineLen - 1] == '\r'))
      lineLen--;
    E.row.size = lineLen;
    E.row.chars = malloc(lineLen + 1);
    memcpy(E.row.chars, line, lineLen);
    E.row.chars[lineLen] = '\0';
    E.numRows = 1;
  }
  free(line);
  fclose(fp);
}

//-----------------------------------------------//
//                    Append Buffer              //
//-----------------------------------------------//

struct abuf {
  char *b;
  int len;
};

void appendBuffer(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void appendBuffFree(struct abuf *ab) { free(ab->b); }

//-----------------------------------------------//
//                  output                       //
//-----------------------------------------------//
// TODO: implement the ```:set number``` vim command
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y <= E.screenrows; y++) {
    if (y >= E.numRows) {
      if (E.numRows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
                                  "JI Editor -- version %s: col: %d, row: %d",
                                  JI_VERSION, E.cy, E.cx);
        if (welcomelen > E.screencols)
          welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen) / 2;

        if (padding) {
          appendBuffer(ab, "~", 1);
          padding--;
        }

        while (padding--)
          appendBuffer(ab, " ", 1);

        appendBuffer(ab, welcome, welcomelen);
      } else {
        appendBuffer(ab, "~", 1);
      }
    } else {
      int len = E.row.size;
      if (len > E.screencols)
        len = E.screencols;
      appendBuffer(ab, E.row.chars, len);
    }

    appendBuffer(ab, "\x1b[K", 3);
    if (y < E.screenrows - 1) {
      appendBuffer(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen() {
  struct abuf ab = ABUF_INIT;

  appendBuffer(&ab, "\x1b[?25l", 6);
  appendBuffer(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  appendBuffer(&ab, buf, strlen(buf));

  appendBuffer(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  appendBuffFree(&ab);
}
//-----------------------------------------------//
//                  Input                        //
//-----------------------------------------------//

void editorMoveCursor(int key) {
  switch (key) {
  case ARROW_LEFT:
    if (E.cx != 0) {
      E.cx--;
      break;
    }
  case ARROW_DOWN:
    if (E.cy != E.screenrows - 1) {
      E.cy++;
      break;
    }
  case ARROW_RIGHT:
    if (E.cx != E.screencols - 1) {
      E.cx++;
      break;
    }
  case ARROW_UP:
    if (E.cy != 0) {
      E.cy--;
      break;
    }
  }
}

void editorProcessorKeypress() {
  int c = editorReadKey();

  switch (c) {
  case CTRL_KEY('q'):
    // escape sequences to clear screen on exit
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  case HOME_KEY:
    E.cx = 0;
    break;
  case END_KEY:
    E.cx = E.screencols - 1;
    break;

  case PAGE_UP:
  case PAGE_DOWN: {
    int times = E.screenrows;
    while (times--)
      editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  } break;

  case ARROW_UP:
  case ARROW_LEFT:
  case ARROW_DOWN:
  case ARROW_RIGHT:
    editorMoveCursor(c);
    break;
  }
}

//-----------------------------------------------//
//                    Init                       //
//-----------------------------------------------//
void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.numRows = 0;
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }
  while (1) {
    editorRefreshScreen();
    editorProcessorKeypress();
  }
  system("figlet Goodbye");
  return EXIT_SUCCESS;
}
