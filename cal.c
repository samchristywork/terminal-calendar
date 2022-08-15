#include <curses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ONEDAY 60 * 60 * 24

time_t startup_time;

char *months[] = {"January", "February", "March", "April",
                  "May", "June", "July", "August",
                  "September", "October", "November", "December"};

char *months_short[] = {"Jan", "Feb", "Mar", "Apr",
                        "May", "Jun", "Jul", "Aug",
                        "Sep", "Oct", "Nov", "Dec"};

void signal_handler(int sig) {
  endwin();
  clear();
  refresh();
}

void print_multiline(char *str, int rootx, int rooty) {

  char *data = malloc(strlen(str) + 1);
  strcpy(data, str);
  char *ptr = data;
  int line = 0;

  for (int i = 0;; i++) {
    if (ptr[i] == 0) {
      move(rooty + line, rootx);
      printw("%s", ptr);
      break;
    }

    if (ptr[i] == '\n') {
      ptr[i] = 0;
      move(rooty + line, rootx);
      printw("%s", ptr);
      ptr += i + 1;
      i = 0;
      line++;
    }
  }

  free(data);
}

void print_day_pane(WINDOW *w, int rootx, int rooty, int date_offset) {

  time_t selected_day = startup_time + date_offset * ONEDAY;
  struct tm *selected = localtime(&selected_day);
  char buf[256];
  strftime(buf, 256, "%a %d %b %Y (%Y-%m-%d)", selected);

  move(rooty, rootx);
  printw("%s", buf);

  int width;
  int height;
  getmaxyx(w, height, width);
  move(rooty + 1, rootx);
  hline('-', width - rootx);
}

void print_cal_pane(WINDOW *w, int rootx, int rooty, int calendar_scroll, int date_offset) {
  int width;
  int height;
  getmaxyx(w, height, width);

  time_t initial_time = startup_time - (calendar_scroll * 7) * ONEDAY;

  clear();
  move(rooty, rootx + 4);
  printw("Su Mo Tu We Th Fr Sa");
  move(rooty + 1, rootx + 4);
  hline('-', 21);

  for (int i = 0;; i++) {

    time_t t = initial_time + i * ONEDAY;
    struct tm *tm = localtime(&t);

    if (i == 0) {
      move(rooty + 1, rootx);
      printw("'%d", tm->tm_year - 100);
    }

    int line = rooty + 2 + i / 7;
    move(line, rootx + (i % 7) * 3 + 4);
    if (line > height - 1) {
      break;
    }

    if (i == date_offset + calendar_scroll * 7) {
      color_set(1, NULL);
    }

    printw("%d", tm->tm_mday);
    color_set(0, NULL);

    if (tm->tm_mday == 1) {
      move(line, rootx);
      printw("%s", months_short[tm->tm_mon]);
    }
  }

  move(rooty, rootx + 25);
  vline('|', height);
}

int main() {
  WINDOW *w;
  if ((w = initscr()) == NULL) {
    fprintf(stderr, "Error initializing ncurses.\n");
    exit(EXIT_FAILURE);
  }

  noecho();
  curs_set(0);
  keypad(w, TRUE);

  start_color();
  init_pair(1, COLOR_BLACK, COLOR_WHITE);

  struct sigaction act;
  bzero(&act, sizeof(struct sigaction));
  act.sa_handler = signal_handler;
  sigaction(SIGWINCH, &act, NULL);

  int calendar_scroll = 4;
  int date_offset = 0;
  startup_time = time(0);

  int c = 0;
  while (c != 'q') {
    switch (c) {
    case ('j'):
      date_offset += 7;
      break;
    case ('k'):
      date_offset -= 7;
      break;
    case ('l'):
      date_offset++;
      break;
    case ('h'):
      date_offset--;
      break;
    case (KEY_DOWN):
      calendar_scroll++;
      break;
    case (KEY_UP):
      calendar_scroll--;
      break;
    default:
      break;
    }

    print_cal_pane(w, 0, 0, calendar_scroll, date_offset);
    print_day_pane(w, 27, 0, date_offset);

    refresh();
    c = getch();
  }

  delwin(w);
  endwin();
  refresh();
}
