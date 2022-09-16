#include <cjson/cJSON.h>
#include <curses.h>
#include <getopt.h>
#include <math.h>
#include <regex.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "cal.h"
#include "version.h"

FILE *log_file;
cJSON *cjson;
cJSON *dates;
cJSON *weekdays;
char *calendar_filename;
char *command = 0;
char *home = 0;
char *lock_location = "/tmp/termcal.lock";
char *log_filename = 0;
char *text_editor = 0;
char search_string[256] = {0};
char status_line[256];
int calendar_view_mode = 0;
int modified = 0;
int reg_flags = 0;
int running = 1;
int verbose = 0;
time_t startup_time;

#define flog(...) fprintf(log_file, ##__VA_ARGS__);
#define set_statusline(...)      \
  {                              \
    char buf[512];               \
    sprintf(buf, ##__VA_ARGS__); \
    _set_statusline(buf);        \
  }

#define redraw()                                         \
  print_cal_pane(w, 0, 0, calendar_scroll, date_offset); \
  print_day_pane(w, 27, 0, date_offset);

/*
 * Handle ctrl-c
 */
void sig_term_handler(int signum, siginfo_t *info, void *ptr) {
  running = 0;
}

/*
 * Handle window resize
 */
void resize_handler(int sig) {
  endwin();
  clear();
  refresh();
}

/*
 * Internal function called by the set_statusline macro
 */
void _set_statusline(char *str) { strcpy(status_line, str); }

/*
 * Find a particular child tag of a node
 */
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

/*
 * Read a file into a cJSON struct
 */
cJSON *readJSONFile(FILE *f) {

  char *buffer;

  if (f) {
    fseek(f, 0, SEEK_END);
    int size = ftell(f);
    rewind(f);

    buffer = malloc(size + 1);
    buffer[size] = 0;
    int ret = fread(buffer, 1, size, f);
    if (ret != size) {
      fprintf(stderr, "Could not read the expected number of bytes.\n");
      exit(EXIT_FAILURE);
    }
  } else {
    char *template = "{\"weekdays\":{},\"days\":{}}";
    buffer = malloc(strlen(template) + 1);
    strcpy(buffer, template);
  }

  cJSON *handle = cJSON_Parse(buffer);
  if (!handle) {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr) {
      fprintf(stderr, "Error before: %s\n", error_ptr);
    }
    cJSON_Delete(handle);
    exit(EXIT_FAILURE);
  }

  free(buffer);
  return handle;
}

/*
 * Save data to disk
 */
void save() {
  cJSON *version = find(cjson, "version");
  if (!version) {
    version = cJSON_CreateString(TERMINAL_CALENDAR_VERSION);
    cJSON_AddItemToObject(cjson, "version", version);
  }
  FILE *f = fopen(calendar_filename, "wb");
  char *str = cJSON_Print(cjson);
  fprintf(f, str);
  fclose(f);
  free(str);
  modified = 0;
  set_statusline("File saved.");
  if (verbose) {
    fprintf(log_file, "Saving file.\n");
  }
}

/*
 * Run the command specified by the user
 */
void print() {
  if (!command) {
    system("./print.sh");
  } else {
    system(command);
  }
}

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
 * This function takes a string, and counts the number of lines that are
 * prepended with a '+', 'o', '-', or 'x'. The results are placed in the
 * appropriate args
 */
void count_from_string(char *str, int *green, int *yellow, int *red, int *blue) {
  if (str[0] == '+' && str[1] == ' ') {
    (*green)++;
  }
  if (str[0] == 'o' && str[1] == ' ') {
    (*yellow)++;
  }
  if (str[0] == '-' && str[1] == ' ') {
    (*red)++;
  }
  if (str[0] == 'x' && str[1] == ' ') {
    (*blue)++;
  }
  for (int i = 2; i < strlen(str); i++) {
    if (str[i - 2] == '\n' && str[i - 1] == '+' && str[i] == ' ') {
      (*green)++;
    }
    if (str[i - 2] == '\n' && str[i - 1] == 'o' && str[i] == ' ') {
      (*yellow)++;
    }
    if (str[i - 2] == '\n' && str[i - 1] == '-' && str[i] == ' ') {
      (*red)++;
    }
    if (str[i - 2] == '\n' && str[i - 1] == 'x' && str[i] == ' ') {
      (*blue)++;
    }
  }
}

/*
 * This function sums up the counts returned from 'count_from_string' over all
 * dates with data
 */
void count_status(int *green, int *yellow, int *red, int *blue) {
  cJSON *node = dates->child;

  while (1) {
    if (!node) {
      break;
    }
    cJSON *data = find(node, "data");
    if (data) {
      count_from_string(data->valuestring, green, yellow, red, blue);
    }
    node = node->next;
  }
}

/*
 * Print the right pane, with the data for that day
 */
void print_day_pane(WINDOW *w, int rootx, int rooty, int date_offset) {

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

  count_status(&green, &yellow, &red, &blue);

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
 * This function tests whether the string contains lines that start with the
 * character 'o'. This assists with user feedback.
 */
int has_incomplete_tasks(char *str) {
  if (str[0] == 'o') {
    return 1;
  }

  for (int i = 1; i < strlen(str); i++) {
    if (str[i - 1] == '\n' && str[i] == 'o') {
      return 1;
    }
  }

  return 0;
}

/*
 * This function tests whether the string contains lines that start with the
 * character '!'. This assists with user feedback.
 */
int has_important_tasks(char *str) {
  if (str[0] == '!') {
    return 1;
  }

  for (int i = 1; i < strlen(str); i++) {
    if (str[i - 1] == '\n' && str[i] == '!') {
      return 1;
    }
  }

  return 0;
}

/*
 * Print the left pane
 */
void print_cal_pane(WINDOW *w, int rootx, int rooty, int calendar_scroll,
                    int date_offset) {

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
          color_set(6, NULL);
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

/*
 * Edit a tag in the cJSON structure with the chosen text editor
 */
void edit_date(cJSON *node, char *tag) {
  if (verbose) {
    fprintf(log_file, "Editing tag \"%s\".\n", tag);
  }

  cJSON *root = find(node, tag);
  if (!root) {
    root = cJSON_CreateObject();
    cJSON_AddItemToObject(node, tag, root);
  }

  if (root) {
    cJSON *day_data = find(root, "data");
    if (!day_data) {
      day_data = cJSON_CreateString("");
      cJSON_AddItemToObject(root, "data", day_data);
    }

    mkdir("/tmp/terminal-calendar/", 0777);
    char filename[] = "/tmp/terminal-calendar/cal.XXXXXX";
    int tmpfd = mkstemp(filename);
    FILE *tmpfile = fdopen(tmpfd, "wb");
    if (day_data) {
      fprintf(tmpfile, "%s", day_data->valuestring);
    }
    fclose(tmpfile);

    char command[256];
    sprintf(command, "%s %s", text_editor, filename);
    system(command);

    tmpfile = fopen(filename, "rb");
    fseek(tmpfile, 0, SEEK_END);
    int size = ftell(tmpfile);
    rewind(tmpfile);

    char buffer[size + 1];
    buffer[size] = 0;
    int ret = fread(buffer, 1, size, tmpfile);
    if (ret != size) {
      fprintf(stderr, "Could not read the expected number of bytes.\n");
      exit(EXIT_FAILURE);
    }

    cJSON_DeleteItemFromObject(root, "data");
    day_data = cJSON_CreateString(buffer);
    cJSON_AddItemToObject(root, "data", day_data);
    modified = 1;
  }
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
              "| d                | Delete the data for the day under the cursor.     |\n"
              "| r                | Edit the recurring task for that day of the week. |\n"
              "| e                | Cycles views in the calendar pane.                |\n"
              "| /                | Search for a string in day data using regex.      |\n"
              "| \\                | Same as '/', but is case insensitive.             |\n"
              "| Cursor keys      | Scroll the calendar.                              |\n"
              "\n"
              "Press any key to continue...\n";

  print_multiline(str, 0, 0, 80, 0);
}

void usage(char *argv[]) {
  fprintf(stderr,
          "Usage: %s [options]\n"
          " -c,--command   The command to be run when \"printing\" (default `./print.sh`).\n"
          " -e,--editor    The command representing the text editor to use (default vim).\n"
          " -f,--file      Calendar file to use. Default \"calendar.json\".\n"
          " -h,--help      Print this usage message.\n"
          " -l,--log-file  The name of the log file to be used.\n"
          " -n,--no-clear  Do not clear the screen on shutdown.\n"
          " -o,--lock-file The name of the lock file to be used (default /tmp/termcal.lock).\n"
          " -v,--verbose   Display additional logging information.\n"
          "",
          argv[0]);
  exit(EXIT_FAILURE);
}

/*
 * This function handles the mechanics of the user entering a string to search
 * for
 */
void search(WINDOW *w, int calendar_scroll, int date_offset, int flags, char symbol) {
  reg_flags = flags;
  int width;
  int height;
  getmaxyx(w, height, width);
  width = width;
  height = height;
  move(height - 1, 0);
  printw("%c", symbol);
  search_string[0] = 0;
  while (1) {
    int i = strlen(search_string);
    search_string[i + 1] = 0;

    char c = getch();
    if (c == '\n') {
      break;
    }
    if (c == 7) { // Backspace
      if (i == 0) {
        break;
      }
      search_string[i - 1] = 0;
    } else {
      search_string[i] = c;
    }

    redraw();
    move(height - 1, 0);
    printw("%c", symbol);
    move(height - 1, 1);
    printw(search_string);
  }
  refresh();
}

void die(WINDOW *w, int no_clear, int status, char *reason) {
  delwin(w);
  endwin();
  refresh();
  cJSON_Delete(cjson);
  fclose(log_file);
  free(calendar_filename);
  unlink(lock_location);

  if (!no_clear) {
    printf("\33[H\33[2J");
  }

  printf("%s\n", reason);

  exit(status);
}

/*
 * Populate the "major", "minor", and "build" int pointers with the data in the string.
 *
 * Given:
 *   str="1.2.3"
 *
 * Then:
 *   major=1
 *   minor=2
 *   build=3
 */
void parse_version_string(char *str, int *major, int *minor, int *build) {
  *major = atoi(str);

  while (str[0] != '.') {
    str++;
    if (str[0] == 0) {
      die(NULL, 1, EXIT_FAILURE, "Version parse error.");
    }
  }
  str++;
  *minor = atoi(str);

  while (str[0] != '.') {
    str++;
    if (str[0] == 0) {
      die(NULL, 1, EXIT_FAILURE, "Version parse error.");
    }
  }
  str++;
  *build = atoi(str);

  while (str[0] != '.') {
    str++;
    if (str[0] == 0) {
      return;
    }
  }
  die(NULL, 1, EXIT_FAILURE, "Version parse error.");
}

int main(int argc, char *argv[]) {

  int no_clear = 0;
  calendar_filename = NULL;

  text_editor = getenv("EDITOR");
  home = getenv("HOME");

  /*
   * Handle command-line arguments
   */
  int opt;
  int option_index = 0;
  char *optstring = "c:e:f:hl:no:v";
  static struct option long_options[] = {
      {"command", required_argument, 0, 'c'},
      {"editor", required_argument, 0, 'e'},
      {"file", required_argument, 0, 'f'},
      {"help", no_argument, 0, 'h'},
      {"lock-file", required_argument, 0, 'o'},
      {"log-file", required_argument, 0, 'l'},
      {"no-clear", no_argument, 0, 'n'},
      {"verbose", no_argument, 0, 'v'},
      {0, 0, 0, 0},
  };

  while ((opt = getopt_long(argc, argv, optstring, long_options, &option_index)) != -1) {
    if (opt == 'c') {
      command = malloc(strlen(optarg) + 1);
      strcpy(command, optarg);
    } else if (opt == 'e') {
      text_editor = malloc(strlen(optarg) + 1);
      strcpy(text_editor, optarg);
    } else if (opt == 'f') {
      calendar_filename = malloc(strlen(optarg) + 1);
      strcpy(calendar_filename, optarg);
    } else if (opt == 'h') {
      usage(argv);
    } else if (opt == 'l') {
      log_filename = malloc(strlen(optarg) + 1);
      strcpy(log_filename, optarg);
    } else if (opt == 'n') {
      no_clear = 1;
    } else if (opt == 'o') {
      lock_location = malloc(strlen(optarg) + 1);
      strcpy(lock_location, optarg);
    } else if (opt == 'v') {
      verbose = 1;
    } else if (opt == '?') {
      usage(argv);
    } else {
      puts(optarg);
    }
  }

  if (optind < argc) {
    int i = optind;
    while (i < argc) {
      fprintf(stdout, "Got additional argument: %s\n", argv[i]);
      i++;
    }
  }

  if (access(lock_location, F_OK) == 0) {
    printf("Found a lock file (%s). Another instance of this program may be running.\n", lock_location);
    exit(EXIT_FAILURE);
  } else {
    FILE *tclock = fopen(lock_location, "wb");
    fclose(tclock);
  }

  /*
   * Use the default editor if none is selected
   */
  if (!text_editor) {
    text_editor = malloc(strlen("vim") + 1);
    strcpy(text_editor, "vim");
  }

  if (log_filename) {
    log_file = fopen(log_filename, "wb");
  } else {
    log_file = fopen("/dev/stderr", "wb");
  }

  /*
   * Use the default filename if none is selected
   */
  if (!calendar_filename) {
    char *f = ".terminal_calendar.json";
    calendar_filename = malloc(strlen(f) + 1 + strlen(home) + 1);
    sprintf(calendar_filename, "%s/%s", home, f);
  }

  /*
   * Open the appropriate save file and read it into a cJSON struct
   */
  if (verbose) {
    fprintf(log_file, "Using \"%s\" as save file.\n", calendar_filename);
  }
  FILE *f = fopen(calendar_filename, "rb");
  cjson = readJSONFile(f);
  if (f) {
    fclose(f);
  }

  cJSON *version = find(cjson, "version");
  if (version) {
    char *p = version->valuestring;

    int m_major, m_minor, m_build;
    int major, minor, build;

    parse_version_string(MIN_SUPPORTED_VERSION, &m_major, &m_minor, &m_build);
    parse_version_string(p, &major, &minor, &build);

    if (major < m_major) {
      die(NULL, 1, EXIT_FAILURE, "Version not supported.");
    }
    if (major == m_major) {
      if (minor < m_minor) {
        die(NULL, 1, EXIT_FAILURE, "Version not supported.");
      }
      if (minor == m_minor) {
        if (build < m_build) {
          die(NULL, 1, EXIT_FAILURE, "Version not supported.");
        }
      }
    }
  }

  dates = find(cjson, "days");
  weekdays = find(cjson, "weekdays");

  if (!dates || !weekdays) {
    fprintf(log_file, "File format error.\n");
    exit(EXIT_FAILURE);
  }

  /*
   * Initialize ncurses
   */
  if (verbose) {
    fprintf(log_file, "Initializing ncurses.\n");
  }
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
  init_pair(2, COLOR_GREEN, COLOR_BLACK);
  init_pair(3, COLOR_YELLOW, COLOR_BLACK);
  init_pair(4, COLOR_RED, COLOR_BLACK);
  init_pair(5, COLOR_BLUE, COLOR_BLACK);
  init_pair(6, COLOR_BLACK, COLOR_YELLOW);
  init_pair(7, COLOR_BLACK, COLOR_RED);

  /*
   * Handle signals
   */
  struct sigaction act;
  bzero(&act, sizeof(struct sigaction));
  act.sa_handler = resize_handler;
  sigaction(SIGWINCH, &act, NULL);

  bzero(&act, sizeof(struct sigaction));
  act.sa_sigaction = sig_term_handler;
  act.sa_flags = SA_SIGINFO;
  sigaction(SIGINT, &act, NULL);

  int calendar_scroll = 4;
  int date_offset = 0;
  startup_time = time(0);

  /*
   * Main Loop
   */
  if (verbose) {
    fprintf(log_file, "Displaying calendar.\n");
  }
  int c = 0;
  while (1) {

    time_t selected_day = startup_time + date_offset * ONEDAY;
    struct tm *selected = localtime(&selected_day);

    char tag[256];
    strftime(tag, 256, "%Y-%m-%d", selected);

    set_statusline(" ");

    if (c >= '1' && c <= '9') {
      int num = c - '0';
      cJSON *root = find(dates, tag);
      if (root) {
        cJSON *mask = find(root, "mask");
        if (!mask) {
          cJSON *number = cJSON_CreateNumber(0);
          cJSON_AddItemToObject(root, "mask", number);
          mask = find(root, "mask");
        }
        int value = mask->valueint;
        int maskdiff = 1 << num;
        value ^= maskdiff;
        cJSON_SetNumberHelper(mask, value);
      }
      modified = 1;
    }

    switch (c) {

    case ('0'):
      date_offset = 0;
      break;

    case ('d'):
      if (verbose) {
        fprintf(log_file, "Deleting calendar entry.\n");
      }
      cJSON_DeleteItemFromObject(dates, tag);
      set_statusline("Deleted entry \"%s\".", tag);
      modified = 1;
      break;

    case ('r'):
      edit_date(weekdays, days_short[selected->tm_wday]);
      break;

    case ('h'):
      date_offset--;
      break;

    case ('j'):
      date_offset += 7;
      break;

    case ('k'):
      date_offset -= 7;
      break;

    case ('l'):
      date_offset++;
      break;

    case ('s'):
      save();
      break;

    case ('p'):
      save();
      print();
      break;

    case ('\n'):
      edit_date(dates, tag);
      break;

    case (' '):
      edit_date(dates, tag);
      break;

    case ('i'):
      edit_date(dates, tag);
      break;

    case ('e'):
      calendar_view_mode++;
      if (calendar_view_mode > 2) {
        calendar_view_mode = 0;
      }
      break;

    case ('q'):
      if (!modified) {
        running = 0;
      } else {
        set_statusline("Refusing to quit (you have unsaved data). Save with \"s\", or quit with \"ctrl-c\".");
      }
      break;

    case ('/'):
      search(w, calendar_scroll, date_offset, 0, '/');
      break;

    case (92): // Backslash
      search(w, calendar_scroll, date_offset, REG_ICASE, 92);
      break;

    case ('?'):
      clear();
      draw_help();
      getch();
      break;

    case (KEY_DOWN):
      calendar_scroll++;
      break;

    case (KEY_UP):
      calendar_scroll--;
      break;

    default:
      flog("Uncaught keypress: %d\n", c);
      break;
    }

    if (running == 0) {
      break;
    }

    /*
     * Display the left and right panes
     */
    redraw();

    if (modified) {
      move(0, 0);
      printw("(*)");
    }

    int width;
    int height;
    getmaxyx(w, height, width);
    height = height + width - width;

    status_line[200] = 0;
    move(height - 1, 0);
    printw(status_line);

    move(height - 1, width - 19);
    printw("Type '?' for help.");

    refresh();
    c = getch();
  }

  if (verbose) {
    fprintf(log_file, "Cleaning up.\n");
  }

  /*
   * Clean up
   */
  die(w, no_clear, EXIT_SUCCESS, ":^)");
}
