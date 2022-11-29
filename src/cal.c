#include <cjson/cJSON.h>
#include <curses.h>
#include <dirent.h>
#include <getopt.h>
#include <regex.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <locale.h>

#include "graphics.h"
#include "util.h"
#include "version.h"

#define ONEDAY 60 * 60 * 24

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
  int next_empty;
  int next_n;
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

#define redraw()                                                                                                           \
  draw_cal_pane(w, 0, 0, calendar_scroll, date_offset, search_string, reg_flags, startup_time, dates, calendar_view_mode); \
  draw_day_pane(w, 27, 0, date_offset, startup_time, dates, weekdays, cjson);

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
  if(w){
    delwin(w);
    endwin();
    refresh();
  }
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
    version = cJSON_CreateString(VERSION_STRING_SHORT);
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
          " -V,--version     Display the software version and exit.\n"
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

void print_version() {
  printf("%s\n\n%s\n", VERSION_STRING, LICENSE_STRING);
}

int main(int argc, char *argv[]) {
  setlocale(LC_ALL, "");

  keys.calendar_scroll_down = KEY_DOWN;
  keys.calendar_scroll_up = KEY_UP;
  keys.cycle_mode = 'e';
  keys.delete_entry = 'D';
  keys.edit_backlog = 'b';
  keys.edit_date = '\n';
  keys.edit_recurring = 'r';
  keys.help = '?';
  keys.move_down = 'j';
  keys.move_fast_down = 'J';
  keys.move_fast_left = 'H';
  keys.move_fast_right = 'L';
  keys.move_fast_up = 'K';
  keys.move_left = 'h';
  keys.move_right = 'l';
  keys.move_up = 'k';
  keys.next_empty = 'n';
  keys.next_n = 'N';
  keys.print = 'p';
  keys.quit = 'q';
  keys.reset_date_offset = '0';
  keys.reverse_search = 92; //Backslash
  keys.save = 's';
  keys.search = '/';

  int no_clear = 0;
  int cli_mode = 0;
  char *cli_arg = NULL;

  text_editor = getenv("EDITOR");
  home = getenv("HOME");

  /*
   * Handle command-line arguments
   */
  int opt;
  int option_index = 0;
  char *optstring = "b:d:c:e:f:hl:no:vz:V";
  static struct option long_options[] = {
      {"cli", required_argument, 0, 'z'},
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
      {"version", no_argument, 0, 'V'},
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
    } else if (opt == 'V') {
      print_version();
      exit(EXIT_SUCCESS);
    } else if (opt == 'z') {
      cli_mode = 1;
      cli_arg=malloc(strlen(optarg)+1);
      strcpy(cli_arg, optarg);
    } else if (opt == '?') {
      usage(argv);
    } else {
      puts(optarg);
    }
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

  if (cli_mode) {
    if (strcmp(cli_arg, "print") == 0) {
      if (optind < argc) {
        int i = optind;
        while (i < argc) {
          cJSON *tag = find(dates, argv[i]);
          if (tag) {
            cJSON *data = find(tag, "data");
            fprintf(stdout, "%s\n", data->valuestring);
          } else {
            fprintf(stdout, "Not found.\n");
          }
          i++;
        }
      }
    }

    cJSON_Delete(cjson);
    fclose(log_file);
    free(calendar_filename);
    return EXIT_SUCCESS;
  }

  /*
   * Create a lock to prevent multiple use
   */
  if (access(lock_location, F_OK) == 0) {
    printf("Found a lock file (%s). Another instance of this program may be running.\n", lock_location);
    exit(EXIT_FAILURE);
  } else {
    FILE *tclock = fopen(lock_location, "wb");
    fclose(tclock);
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
  struct tm *now = localtime(&startup_time);
  now->tm_hour=12;
  startup_time = mktime(now);

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
      char *days_short[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
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
      date_offset -= 3;
    } else if (c == keys.move_fast_down) {
      date_offset += 7 * 3;
    } else if (c == keys.move_fast_up) {
      date_offset -= 7 * 3;
    } else if (c == keys.move_fast_right) {
      date_offset += 3;
    } else if (c == keys.next_empty) {
      while (1) {
        time_t s = startup_time + date_offset * ONEDAY;
        struct tm *sel = localtime(&s);
        char t[256];
        strftime(t, 256, "%Y-%m-%d", sel);
        cJSON *root = find(dates, t);
        if (!root) {
          break;
        }
        date_offset++;
      }
    } else if (c == keys.next_n) {
      while (1) {
        time_t s = startup_time + date_offset * ONEDAY;
        struct tm *sel = localtime(&s);
        char t[256];
        strftime(t, 256, "%Y-%m-%d", sel);
        cJSON *root = find(dates, t);
        if (!root) {
          break;
        }
        if (root) {
          cJSON *day_data = find(root, "data");
          if (day_data) {
            int count = 0;
            for (int i = 0; i < strlen(day_data->valuestring); i++) {
              if (day_data->valuestring[i] == '\n') {
                count++;
              }
            }
            if (count < 4) {
              break;
            }
          }
        }
        date_offset++;
      }
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

    while (date_offset + calendar_scroll * 7 < 0) {
      calendar_scroll++;
    }

    while (date_offset + calendar_scroll * 7 > height * 7 * .9) {
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

    draw_statusline(w, status_line);

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
