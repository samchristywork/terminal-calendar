#ifndef UTIL_H
#define UTIL_H

cJSON *find(cJSON *tree, char *str);
void count_status(int *green, int *yellow, int *red, int *blue, cJSON *dates);
void count_from_string(char *str, int *green, int *yellow, int *red, int *blue);
int has_incomplete_tasks(char *str);
int has_important_tasks(char *str);

#endif
