#include <cjson/cJSON.h>
#include <curses.h>
#include <dirent.h>
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
#include "graphics.h"
#include "version.h"

FILE *log_file;
cJSON *cjson;
cJSON *dates;
cJSON *weekdays;
char *backup_dir = 0;
char *calendar_filename = 0;
char *command = 0;
char *home = 0;
char *lock_location = "/tmp/termcal.lock";
char *log_filename = 0;
char *text_editor = 0;
char search_string[256] = {0};
char status_line[256];
int calendar_view_mode = 0;
int modified = 0;
int num_backups = 10;
int reg_flags = 0;
int running = 1;
int verbose = 0;
time_t startup_time;

struct key_mapping {
  int calendar_scroll_down;
  int calendar_scroll_up;
  int cycle_mode;
  int delete_entry;
  int edit_backlog;
  int edit_date;
  int edit_recurring;
  int help;
  int move_down;
  int move_left;
  int move_right;
  int move_up;
  int move_fast_down;
  int move_fast_left;
  int move_fast_right;
  int move_fast_up;
  int print;
  int quit;
  int reset_date_offset;
  int reverse_search;
  int save;
  int search;
} keys;

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

int cmpfunc(const void *a, const void *b) {
  const char *aa = *(const char **)a;
  const char *bb = *(const char **)b;
  return strcmp(aa, bb);
}

/*
 * Note that this method will fail briefly on Saturday November 20, 2286.
 */
void remove_old_backups() {

  DIR *dirp = opendir(backup_dir);
  if (!dirp) {
    perror("opendir");
    die(NULL, 1, EXIT_FAILURE, "Directory could not be opened.");
  }

  char **dirs = malloc(sizeof(char *) * 256);

  int count = 0;
  while (count < 255) {

    struct dirent *d = readdir(dirp);
    if (d == NULL) {
      break;
    }
    if (strcmp(d->d_name, ".") == 0) {
      continue;
    }
    if (strcmp(d->d_name, "..") == 0) {
      continue;
    }
    dirs[count] = malloc(strlen(d->d_name) + 1);
    strcpy(dirs[count], d->d_name);
    count++;
  }

  qsort(dirs, count, sizeof(char *), cmpfunc);

  for (int i = 0; i < count; i++) {
    flog("%s\n", dirs[i]);
  }

  if (num_backups > 0) {
    int toremove = count - num_backups;
    if (toremove > 0) {
      for (int i = 0; i < toremove; i++) {
        char filename[PATH_MAX];
        sprintf(filename, "%s/%s", backup_dir, dirs[i]);
        flog("Removing %s\n", filename);
        unlink(filename);
      }
    }
  }
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
  char *str = cJSON_Print(cjson);

  FILE *f = fopen(calendar_filename, "wb");
  fprintf(f, "%s", str);
  fclose(f);

  /*
   * Backups
   */
  char backup_filename[PATH_MAX];
  bzero(backup_filename, PATH_MAX);
  sprintf(backup_filename, "%s/%lu", backup_dir, time(0));

  f = fopen(backup_filename, "wb");
  fprintf(f, "%s", str);
  fclose(f);

  free(str);
  modified = 0;
  set_statusline("File saved.");
  if (verbose) {
    fprintf(log_file, "Saving file.\n");
  }

  remove_old_backups();
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

void usage(char *argv[]) {
  fprintf(stderr,
          "Usage: %s [options]\n"
          " -b,--num_backups The number of backup files to keep (default 10). Specify 0 for unlimited files.\n"
          " -c,--command     The command to be run when \"printing\" (default `./print.sh`).\n"
          " -d,--backup_dir  The directory to store backup files in (default ~/.terminal_calendar_backup/).\n"
          " -e,--editor      The command representing the text editor to use (default vim).\n"
          " -f,--file        Calendar file to use. Default \"calendar.json\".\n"
          " -h,--help        Print this usage message.\n"
          " -l,--log-file    The name of the log file to be used.\n"
          " -n,--no-clear    Do not clear the screen on shutdown.\n"
          " -o,--lock-file   The name of the lock file to be used (default /tmp/termcal.lock).\n"
          " -v,--verbose     Display additional logging information.\n"
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

  keys.reset_date_offset = '0';
  keys.edit_backlog = 'b';
  keys.delete_entry = 'd';
  keys.edit_recurring = 'r';
  keys.move_fast_left = 'H';
  keys.move_fast_down = 'J';
  keys.move_fast_up = 'K';
  keys.move_fast_right = 'L';
  keys.move_left = 'h';
  keys.move_down = 'j';
  keys.move_up = 'k';
  keys.move_right = 'l';
  keys.save = 's';
  keys.print = 'p';
  keys.edit_date = '\n';
  keys.cycle_mode = 'e';
  keys.quit = 'q';
  keys.search = '/';
  keys.reverse_search = 92; //Backslash
  keys.help = '?';
  keys.calendar_scroll_down = KEY_DOWN;
  keys.calendar_scroll_up = KEY_UP;

  int no_clear = 0;

  text_editor = getenv("EDITOR");
  home = getenv("HOME");

  /*
   * Handle command-line arguments
   */
  int opt;
  int option_index = 0;
  char *optstring = "b:d:c:e:f:hl:no:v";
  static struct option long_options[] = {
      {"backup_dir", required_argument, 0, 'd'},
      {"command", required_argument, 0, 'c'},
      {"editor", required_argument, 0, 'e'},
      {"file", required_argument, 0, 'f'},
      {"help", no_argument, 0, 'h'},
      {"lock-file", required_argument, 0, 'o'},
      {"log-file", required_argument, 0, 'l'},
      {"no-clear", no_argument, 0, 'n'},
      {"num_backups", required_argument, 0, 'b'},
      {"verbose", no_argument, 0, 'v'},
      {0, 0, 0, 0},
  };

  while ((opt = getopt_long(argc, argv, optstring, long_options, &option_index)) != -1) {
    if (opt == 'b') {
      num_backups = atoi(optarg);
    } else if (opt == 'c') {
      command = malloc(strlen(optarg) + 1);
      strcpy(command, optarg);
    } else if (opt == 'd') {
      backup_dir = malloc(strlen(optarg) + 1);
      strcpy(backup_dir, optarg);
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
   * Use default backup directory if none supplied. Create directory if it does
   * not exist.
   */
  if (!backup_dir) {
    char *f = ".terminal_calendar_backup";
    backup_dir = malloc(strlen(f) + 1 + strlen(home) + 1);
    sprintf(backup_dir, "%s/%s", home, f);
  }

  mkdir(backup_dir, 0777);

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
  init_pair(8, COLOR_MAGENTA, COLOR_BLACK);

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
    } else if (c == keys.reset_date_offset) {
      date_offset = 0;
    } else if (c == keys.edit_backlog) {
      edit_date(cjson, "backlog");
    } else if (c == keys.delete_entry) {
      if (verbose) {
        fprintf(log_file, "Deleting calendar entry.\n");
      }
      cJSON_DeleteItemFromObject(dates, tag);
      set_statusline("Deleted entry \"%s\".", tag);
      modified = 1;
    } else if (c == keys.edit_recurring) {
      edit_date(weekdays, days_short[selected->tm_wday]);
    } else if (c == keys.move_left) {
      date_offset--;
    } else if (c == keys.move_down) {
      date_offset += 7;
    } else if (c == keys.move_up) {
      date_offset -= 7;
    } else if (c == keys.move_right) {
      date_offset++;
    } else if (c == keys.move_fast_left) {
      date_offset-=3;
    } else if (c == keys.move_fast_down) {
      date_offset += 7 * 3;
    } else if (c == keys.move_fast_up) {
      date_offset -= 7 * 3;
    } else if (c == keys.move_fast_right) {
      date_offset+=3;
    } else if (c == keys.save) {
      save();
    } else if (c == keys.print) {
      save();
      print();
    } else if (c == keys.edit_date) {
      edit_date(dates, tag);
    } else if (c == keys.cycle_mode) {
      calendar_view_mode++;
      if (calendar_view_mode > 2) {
        calendar_view_mode = 0;
      }
    } else if (c == keys.quit) {
      if (!modified) {
        running = 0;
      } else {
        set_statusline("Refusing to quit (you have unsaved data). Save with \"s\", or quit with \"ctrl-c\".");
      }
    } else if (c == keys.search) {
      search(w, calendar_scroll, date_offset, 0, '/');
    } else if (c == keys.reverse_search) {
      search(w, calendar_scroll, date_offset, REG_ICASE, 92);
    } else if (c == keys.help) {
      clear();
      draw_help();
      getch();
    } else if (c == keys.calendar_scroll_down) {
      calendar_scroll++;
    } else if (c == keys.calendar_scroll_up) {
      calendar_scroll--;
    } else {
      flog("Uncaught keypress: %d\n", c);
    }

    int width;
    int height;
    getmaxyx(w, height, width);
    height = height + width - width;

    if (date_offset + calendar_scroll * 7 < 0) {
      calendar_scroll++;
    }

    if (date_offset + calendar_scroll * 7 > height * 7 * .9) {
      calendar_scroll--;
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
