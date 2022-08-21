#include <cjson/cJSON.h>
#include <curses.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cal.h"
#include "version.h"

FILE *log_file;
cJSON *cjson;
char *calendar_filename;
char *text_editor = 0;
char status_line[256];
int modified = 0;
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
    buffer = malloc(3);
    strcpy(buffer, "{}");
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
 * Print text, respecting newlines, and coloring the text based on the
 * 1-character signifier at the beginning of the line.
 */
void print_multiline(char *str, int rootx, int rooty, int width) {

  char *data = malloc(strlen(str) + 1);
  strcpy(data, str);
  char *ptr = data;
  int line = 0;

  for (int i = 0;; i++) {
    if (ptr[i] == '\n' || ptr[i] == 0) {
      ptr[i] = 0;
      move(rooty + line, rootx);
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
      printw("%s", ptr);
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
}

/*
 * Print the right-hand pane, with the data for that day
 */
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

  /*
   * Print the top pane, with the data specific to the day
   */
  {
    strftime(buf, 256, "%Y-%m-%d", selected);
    cJSON *root = find(cjson, buf);
    if (root) {
      cJSON *day_data = find(root, "data");
      if (day_data) {
        print_multiline(day_data->valuestring, rootx, rooty + 2, width - rootx);
      }
    } else {
      move(rooty + 2, rootx);
      printw("No entry.");
    }
  }

  /*
   * Print the bottom pane, with the recurring tasks
   */
  {
    cJSON *root = find(cjson, days_short[selected->tm_wday]);
    if (root) {
      cJSON *day_data = find(root, "data");
      if (day_data) {
        move(height / 2 + 0, rootx);
        printw("Recurring");
        move(height / 2 + 1, rootx);
        hline('-', width);
        print_multiline(day_data->valuestring, rootx, height / 2 + 2, width - rootx);
      }
    }
  }
}

/*
 * Print the left-hand pane
 */
void print_cal_pane(WINDOW *w, int rootx, int rooty, int calendar_scroll,
                    int date_offset) {
  int width;
  int height;
  getmaxyx(w, height, width);
  height = height + width - width;

  time_t initial_time = startup_time - (calendar_scroll * 7) * ONEDAY;

  clear();
  move(rooty, rootx + 4);
  printw("Su Mo Tu We Th Fr Sa");
  move(rooty + 1, rootx + 4);
  hline('-', 21);

  struct tm *tm = localtime(&initial_time);
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
  vline('|', height - 1);
}

/*
 * Edit a tag in the cJSON structure with the chosen text editor
 */
void edit_date(char *tag) {
  if (verbose) {
    fprintf(log_file, "Editing tag \"%s\".\n", tag);
  }

  cJSON *root = find(cjson, tag);
  if (!root) {
    root = cJSON_CreateObject();
    cJSON_AddItemToObject(cjson, tag, root);
  }

  if (root) {
    cJSON *day_data = find(root, "data");
    if (!day_data) {
      day_data = cJSON_CreateString("");
      cJSON_AddItemToObject(root, "data", day_data);
    }

    char filename[] = "/tmp/cal.XXXXXX";
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

void usage(char *argv[]) {
  fprintf(stderr,
          "Usage: %s [options]\n"
          " -e,--editor    The command representing the text editor to use (default vim).\n"
          " -f,--file      Calendar file to use. Default \"calendar.json\".\n"
          " -h,--help      Print this usage message.\n"
          " -n,--no-clear  Do not clear the screen on shutdown.\n"
          " -v,--verbose   Display additional logging information.\n"
          "",
          argv[0]);
  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {

  int no_clear = 0;
  calendar_filename = NULL;

  text_editor = getenv("EDITOR");

  /*
   * Handle command-line arguments
   */
  int opt;
  int option_index = 0;
  char *optstring = "e:f:hnv";
  static struct option long_options[] = {
      {"editor", required_argument, 0, 'e'},
      {"file", required_argument, 0, 'f'},
      {"help", no_argument, 0, 'h'},
      {"no-clear", no_argument, 0, 'n'},
      {"verbose", no_argument, 0, 'v'},
      {0, 0, 0, 0},
  };

  while ((opt = getopt_long(argc, argv, optstring, long_options, &option_index)) != -1) {
    if (opt == 'e') {
      text_editor = malloc(strlen(optarg) + 1);
      strcpy(text_editor, optarg);
    } else if (opt == 'f') {
      calendar_filename = malloc(strlen(optarg) + 1);
      strcpy(calendar_filename, optarg);
    } else if (opt == 'h') {
      usage(argv);
    } else if (opt == 'n') {
      no_clear = 1;
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

  /*
   * Use the default editor if none is selected
   */
  if (!text_editor) {
    text_editor = malloc(strlen("vim") + 1);
    strcpy(text_editor, "vim");
  }

  log_file = fopen("log", "wb");

  /*
   * Use the default filename if none is selected
   */
  if (!calendar_filename) {
    char *f = "data.json";
    calendar_filename = malloc(strlen(f) + 1);
    strcpy(calendar_filename, f);
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

    switch (c) {

    case ('0'):
      date_offset = 0;
      break;

    case ('d'):
      if (verbose) {
        fprintf(log_file, "Deleting calendar entry.\n");
      }
      cJSON_DeleteItemFromObject(cjson, tag);
      set_statusline("Deleted entry \"%s\".", tag);
      modified = 1;
      break;

    case ('r'):
      edit_date(days_short[selected->tm_wday]);
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

    case ('\n'):
      edit_date(tag);
      break;

    case (' '):
      edit_date(tag);
      break;

    case ('i'):
      edit_date(tag);
      break;

    case ('q'):
      if (!modified) {
        running = 0;
      } else {
        set_statusline("Refusing to quit (you have unsaved data). Save with \"s\", or quit with \"ctrl-c\".");
      }
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

    if (running == 0) {
      break;
    }

    /*
     * Display the left and right panes
     */
    print_cal_pane(w, 0, 0, calendar_scroll, date_offset);
    print_day_pane(w, 27, 0, date_offset);

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

    refresh();
    c = getch();
  }

  if (verbose) {
    fprintf(log_file, "Cleaning up.\n");
  }

  /*
   * Clean up
   */
  delwin(w);
  endwin();
  refresh();
  cJSON_Delete(cjson);
  fclose(log_file);
  free(calendar_filename);

  if (!no_clear) {
    printf("\33[H\33[2J");
  }
}
