//-----------------------------------------------//
//                  Headers                      //
//-----------------------------------------------//
#include <ctype.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
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
//-----------------------------------------------//
//                    Data                       //
//-----------------------------------------------//

struct editorConfig {
  int screenrows;
  int screencols;
  struct termios orig_termios;
};
struct editorConfig E;
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
  // A collection of flages to enable "Raw Mode"(Terrible name btw 😒)
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

char editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
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
//                    Append Buffer              //
//-----------------------------------------------//

struct abuf {
  char *b;
  int len;
};
#define ABUF_INIT {NULL, 0}

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
    if (y == E.screenrows / 3) {
      char welcome[80];
      int welcomelen = snprintf(welcome, sizeof(welcome),
                                "JI Editor -- version %s", JI_VERSION);
      if (welcomelen > E.screencols)
        welcomelen = E.screencols;
      appendBuffer(ab, welcome, welcomelen);
    } else {
      appendBuffer(ab, "~", 1);
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

  appendBuffer(&ab, "\x1b[H", 3);
  appendBuffer(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  appendBuffFree(&ab);
}
//-----------------------------------------------//
//                  Input                        //
//-----------------------------------------------//

void editorProcessorKeypress() {
  char c = editorReadKey();

  switch (c) {
  case CTRL_KEY('q'):
    // escape sequences to clear screen on exit
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  }
}

//-----------------------------------------------//
//                    Init                       //
//-----------------------------------------------//
void initEditor() {
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
}

int main() {
  enableRawMode();
  initEditor();
  while (1) {
    editorRefreshScreen();
    editorProcessorKeypress();
  }
  return EXIT_SUCCESS;
}
