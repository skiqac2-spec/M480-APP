#ifndef APP_H
#define APP_H
int app_start(const char *path, int carrier, int scr_w, int scr_h, const char *savebase, char *err, int errmax);
int app_step(int budget_ms);      /* 0 when the game ended */
int app_idle_ms(void);            /* how long the VM can sleep */
const char *app_name(void);
const char *app_id(void);
int app_running(void);
void app_request_quit(void);
#endif
