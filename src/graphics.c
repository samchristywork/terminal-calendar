#include <cjson/cJSON.h>
#include <curses.h>
#include <math.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "util.h"

#define ONEDAY 60 * 60 * 24

char *months_short[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

/*
 * Print text, respecting newlines, and coloring the text based on the
 * 1-character signifier at the beginning of the line
 */
int print_multiline(char *str, int rootx, int rooty, int width, int height) {

  char *data = malloc(strlen(str) + 1);
  strcpy(data, str);
  char *ptr = data;
  int line = 0;

  if (!ptr[0]) {
    return 0;
  }

  int lines = 0;

  for (int i = 0;; i++) {
    if (ptr[i] == '\n' || ptr[i] == 0) {
      ptr[i] = 0;
      if (ptr[0] == '+') {
        color_set(2, NULL);
        attron(A_BOLD);
      }
      if (ptr[0] == 'o') {
        color_set(3, NULL);
        attron(A_BOLD);
      }
      if (ptr[0] == '-') {
        color_set(4, NULL);
        attron(A_BOLD);
      }
      if (ptr[0] == 'x') {
        color_set(5, NULL);
        attron(A_BOLD);
      }
      if (strlen(ptr) > width) {
        ptr[width] = 0;
      }
      if (line < height || height == 0) {
        move(rooty + line, rootx);
        printw("%s", ptr);
      }
      lines++;
      color_set(0, NULL);
      attroff(A_BOLD);

      ptr += i + 1;
      i = 0;
      line++;
    }

    if (ptr[i] == 0) {
      break;
    }
  }

  free(data);
  return lines;
}

/*
 * Print the right pane, with the data for that day
 */
void draw_day_pane(WINDOW *w, int rootx, int rooty, int date_offset, time_t startup_time, cJSON *dates, cJSON *weekdays, cJSON *cjson) {

  time_t selected_day = startup_time + date_offset * ONEDAY;
  struct tm *selected = localtime(&selected_day);
  char buf[256];
  strftime(buf, 256, "%a %d %b %Y (%Y-%m-%d) Week %U", selected);

  move(rooty, rootx);
  printw("%s", buf);

  int width;
  int height;
  getmaxyx(w, height, width);
  move(rooty + 1, rootx);
  hline(ACS_HLINE, width - rootx);

  /*
   * Print the top pane, with the data specific to the day
   */
  strftime(buf, 256, "%Y-%m-%d", selected);
  cJSON *day_root = find(dates, buf);
  if (day_root) {
    cJSON *day_data = find(day_root, "data");
    if (day_data) {
      print_multiline(day_data->valuestring, rootx, rooty + 2, width - rootx, height / 2 - rooty - 3);
    }
  } else {
    move(rooty + 2, rootx);
    printw("No entry.");
  }

  /*
   * Print the bottom pane, with the recurring tasks
   */
  move(height / 2 + 0, rootx);
  printw("Recurring Weekly");
  move(height / 2 + 1, rootx);
  hline(ACS_HLINE, width);

  char *days_short[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  cJSON *wday_root = find(weekdays, days_short[selected->tm_wday]);
  if (wday_root) {
    cJSON *mask = find(day_root, "mask");
    cJSON *day_data = find(wday_root, "data");
    if (day_data) {
      int lines = print_multiline(day_data->valuestring, rootx + 2, height / 2 + 2, width - rootx - 2, 0);

      int val = 0;
      if (mask && cJSON_IsNumber(mask)) {
        val = mask->valueint;
      }
      for (int i = 1; i < lines + 1; i++) {
        move(height / 2 + 1 + i, rootx);
        if (val >> i & 1) {
          printw("+");
        } else {
          printw("o");
        }
      }
    }
  }

  /*
   * Print the item counts in the top right corner
   */
  int green = 0;
  int yellow = 0;
  int red = 0;
  int blue = 0;

  count_status(&green, &yellow, &red, &blue, dates);

  int len = 0;
  if (green) {
    len += log10(green);
  }
  if (yellow) {
    len += log10(yellow);
  }
  if (red) {
    len += log10(red);
  }
  if (blue) {
    len += log10(blue);
  }
  len += 7;

  cJSON *backlog = find(cjson, "backlog");
  if (backlog) {
    cJSON *data = find(backlog, "data");
    if (data) {
      int j = 0;
      for (int i = 0; i < strlen(data->valuestring); i++) {
        if (data->valuestring[i] == '\n') {
          j++;
        }
      }
      if (j) {
        move(rooty, width - len - 3 - log10(j + 1));
        color_set(8, NULL);
        printw("(%d)", j);
        color_set(0, NULL);
      }
    }
  }

  attron(A_BOLD);
  move(rooty, width - len);
  color_set(2, NULL);
  printw("%d", green);
  color_set(0, NULL);
  printw("/");
  color_set(3, NULL);
  printw("%d", yellow);
  color_set(0, NULL);
  printw("/");
  color_set(4, NULL);
  printw("%d", red);
  color_set(0, NULL);
  printw("/");
  color_set(5, NULL);
  printw("%d", blue);
  color_set(0, NULL);
  attroff(A_BOLD);
}

/*
 * Print the left pane
 */
void draw_cal_pane(WINDOW *w, int rootx, int rooty, int calendar_scroll, int date_offset, char *search_string, int reg_flags, time_t startup_time, cJSON *dates, int calendar_view_mode) {

  regex_t preg;
  if (regcomp(&preg, search_string, reg_flags) != 0) {
    exit(EXIT_FAILURE);
  }

  int width;
  int height;
  getmaxyx(w, height, width);
  height = height + width - width;

  struct tm *tm = localtime(&startup_time);
  int current_year = tm->tm_year;
  int current_mon = tm->tm_mon;
  int current_mday = tm->tm_mday;

  time_t initial_time = startup_time - (calendar_scroll * 7) * ONEDAY;

  clear();
  move(rooty, rootx + 4);
  printw("Su Mo Tu We Th Fr Sa");
  move(rooty + 1, rootx + 4);
  hline(ACS_HLINE, 21);

  move(rooty + 1, rootx);
  printw("'%d", tm->tm_year - 100);
  int off = tm->tm_wday;

  for (int i = -tm->tm_wday;; i++) {
    time_t t = initial_time + i * ONEDAY;
    struct tm *tm = localtime(&t);

    int line = rooty + 2 + (i + off) / 7;
    move(line, rootx + ((i + off) % 7) * 3 + 4);
    if (line > height - 2) {
      break;
    }

    char buf[256];
    sprintf(buf, "%d-%2.2d-%2.2d", 1900 + tm->tm_year, tm->tm_mon + 1,
            tm->tm_mday);
    cJSON *root = find(dates, buf);
    int num_tasks = 0;
    if (root) {
      attron(A_BOLD);
      cJSON *day_data = find(root, "data");
      if (day_data) {
        if (has_incomplete_tasks(day_data->valuestring) &&
            current_mday + current_mon * 100 + current_year * 10000 >
                tm->tm_mday + tm->tm_mon * 100 + tm->tm_year * 10000) {
          color_set(7, NULL);
        }
        for (int i = 0; i < strlen(day_data->valuestring); i++) {
          if (day_data->valuestring[i] == '\n') {
            num_tasks++;
          }
        }
        if (has_important_tasks(day_data->valuestring)) {
          color_set(7, NULL);
        }
      }
    }

    if (strlen(search_string) > 0 && root) {
      cJSON *day_data = find(root, "data");
      if (day_data) {
        if (regexec(&preg, day_data->valuestring, 0, NULL, 0) == 0) {
          attroff(A_BOLD);
          color_set(6, NULL);
        }
      }
    }

    if (i == date_offset + calendar_scroll * 7) {
      attron(A_REVERSE);
    }

    if (calendar_view_mode == 0) {
      printw("%d", tm->tm_mday);
    } else if (calendar_view_mode == 1) {
      if (num_tasks != 0) {
        printw("%d", num_tasks);
      }
    } else {
      printw("%d", tm->tm_mon);
    }
    color_set(0, NULL);
    attroff(A_BOLD);
    attroff(A_REVERSE);

    if (tm->tm_mday == 1) {
      move(line, rootx);
      printw("%s", months_short[tm->tm_mon]);
    }
  }

  move(rooty, rootx + 25);
  vline(ACS_VLINE, height - 1);
  mvaddch(rooty + 1, rootx + 25, ACS_RTEE);
}

void draw_help() {
  char *str = ""
              "| Key              | Action                                            |\n"
              "|------------------|---------------------------------------------------|\n"
              "| h, j, k, l       | Move the cursor left, down, up, or right.         |\n"
              "| i, Space, Return | Edit the day under the cursor.                    |\n"
              "| s                | Save the data to the `calendar.json` file.        |\n"
              "| 1-9              | Toggle the indicators next to recurring tasks.    |\n"
              "| q                | Quit.                                             |\n"
              "| p                | 'Print' the calendar using the print script.      |\n"
              "| 0                | Move the cursor to the current day.               |\n"
              "| n                | Move cursor to the next empty date.               |\n"
              "| N                | Move cursor to next date with `num_lines` < 4.    |\n"
              "| d                | Delete the data for the day under the cursor.     |\n"
              "| b                | Edit the backlog.                                 |\n"
              "| r                | Edit the recurring task for that day of the week. |\n"
              "| e                | Cycles views in the calendar pane.                |\n"
              "| /                | Search for a string in day data using regex.      |\n"
              "| \\                | Same as '/', but is case insensitive.             |\n"
              "| Cursor keys      | Scroll the calendar.                              |\n"
              "\n"
              "Press any key to continue...\n";

  print_multiline(str, 0, 0, 80, 0);
}

void draw_statusline(WINDOW *w, char *status_line) {
  int width;
  int height;
  getmaxyx(w, height, width);

  status_line[200] = 0;
  move(height - 1, 0);
  printw(status_line);

  move(height - 1, width - 19);
  printw("Type '?' for help.");

}
