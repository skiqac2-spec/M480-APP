/* PicoDrive pieces left out of the Windows CE build: PNG overlays, Ogg CD tracks, gz ROMs */
#include <stddef.h>
#include <math.h>
void *rpng_alloc(void) { return NULL; }
void rpng_free(void *r) { (void)r; }
int rpng_is_valid(void *r) { (void)r; return 0; }
int rpng_set_buf_ptr(void *r, void *data, size_t size) { (void)r; (void)data; (void)size; return 0; }
int rpng_start(void *r) { (void)r; return 0; }
int rpng_iterate_image(void *r) { (void)r; return 0; }
int rpng_process_image(void *r, void **data, size_t size, unsigned *w, unsigned *h, int bgra)
{ (void)r; (void)data; (void)size; (void)w; (void)h; (void)bgra; return -1; }
int ogg_get_length(void *f) { (void)f; return 0; }
void ogg_start_play(void *f, int pos) { (void)f; (void)pos; }
void ogg_stop_play(void) {}
void ogg_update(int *buffer, int length, int stereo) { (void)buffer; (void)length; (void)stereo; }
size_t gzfread(void *buf, size_t size, size_t n, void *f) { (void)buf; (void)size; (void)n; (void)f; return 0; }
const char *gzerror(void *f, int *err) { (void)f; if (err) *err = -1; return "gz not supported"; }
int gzeof(void *f) { (void)f; return 1; }
double exp2(double x) { return exp(x * 0.69314718055994530942); }   /* not pow(2,x): the compiler would turn that back into exp2() */
