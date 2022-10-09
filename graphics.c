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
