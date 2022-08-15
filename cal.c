#include <cjson/cJSON.h>
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

cJSON *find(cJSON *tree, char *str) {
  cJSON *node = NULL;

  if (tree) {
    node = tree->child;
    while (1) {
      if (!node) {
        break;
      }
      if (strcmp(str, node->string) == 0) {
        break;
      }
      node = node->next;
    }
  }
  return node;
}

cJSON *readJSONFile(FILE *f) {

  fseek(f, 0, SEEK_END);
  int size = ftell(f);
  rewind(f);

  char buffer[size + 1];
  buffer[size] = 0;
  int ret = fread(buffer, 1, size, f);
  if (ret != size) {
    fprintf(stderr, "Could not read the expected number of bytes.\n");
    exit(EXIT_FAILURE);
  }

  cJSON *cjson = cJSON_Parse(buffer);
  if (!cjson) {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr) {
      fprintf(stderr, "Error before: %s\n", error_ptr);
    }
    cJSON_Delete(cjson);
    exit(EXIT_FAILURE);
  }
  return cjson;
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

void print_day_pane(WINDOW *w, cJSON *cjson, int rootx, int rooty, int date_offset) {

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

  strftime(buf, 256, "%Y-%m-%d", selected);
  cJSON *root = find(cjson, buf);
  if (root) {
    cJSON *day_data = find(root, "data");
    if (day_data) {
      print_multiline(day_data->valuestring, rootx, rooty + 2);
    }
  } else {
    move(rooty + 2, rootx);
    printw("No entry.");
  }
}

void print_cal_pane(WINDOW *w, cJSON *cjson, int rootx, int rooty, int calendar_scroll, int date_offset) {
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

    char buf[256];
    sprintf(buf, "%d-%2.2d-%2.2d", 1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday);
    cJSON *root = find(cjson, buf);
    if (root) {
      attron(A_BOLD);
    }

    if (i == date_offset + calendar_scroll * 7) {
      color_set(1, NULL);
    }

    printw("%d", tm->tm_mday);
    color_set(0, NULL);
    attroff(A_BOLD);

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

  if (has_colors() == FALSE) {
    fprintf(stderr, "Terminal does not support color.\n");
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

    print_cal_pane(w, cjson, 0, 0, calendar_scroll, date_offset);
    print_day_pane(w, cjson, 27, 0, date_offset);

    refresh();
    c = getch();
  }

  delwin(w);
  endwin();
  refresh();
  cJSON_Delete(cjson);
}
