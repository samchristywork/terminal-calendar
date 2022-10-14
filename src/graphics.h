#ifndef GRAPHICS_H
#define GRAPHICS_H

int print_multiline(char *str, int rootx, int rooty, int width, int height);
void draw_day_pane(WINDOW *w, int rootx, int rooty, int date_offset, time_t startup_time, cJSON *dates, cJSON *weekdays, cJSON *cjson);
void draw_cal_pane(WINDOW *w, int rootx, int rooty, int calendar_scroll, int date_offset, char *search_string, int reg_flags, time_t startup_time, cJSON *dates, int calendar_view_mode);
void draw_help();

#endif
