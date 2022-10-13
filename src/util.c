#include <cjson/cJSON.h>
#include <string.h>

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
void count_status(int *green, int *yellow, int *red, int *blue, cJSON *dates) {
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
