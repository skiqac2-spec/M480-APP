#ifndef CE_SIGNAL_H
#define CE_SIGNAL_H
#define SIGSEGV 11
#define SIGINT 2
#define SIGTERM 15
#define SIGABRT 6
typedef void (*ce_sighandler_t)(int);
#define SIG_DFL ((ce_sighandler_t)0)
#define SIG_IGN ((ce_sighandler_t)1)
static inline ce_sighandler_t signal(int s, ce_sighandler_t h) { (void)s; (void)h; return SIG_DFL; }
#endif
