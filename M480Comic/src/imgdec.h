/* M480Comic - image decoding (JPEG/PNG/GIF/BMP via stb_image, WebP via simplewebp)
 * Output is RGB565, scaled to a requested width. */
#ifndef M480C_IMGDEC_H
#define M480C_IMGDEC_H

#include <stddef.h>

/* large blocks go outside the 32MB process slot on Windows Mobile */
void *big_malloc(size_t n);
void *big_realloc(void *p, size_t n);
void  big_free(void *p);

/* 1 if the data is a supported image; fills original size */
int imgdec_info(const unsigned char *d, int len, int *w, int *h);

/* decode and scale to tw pixels wide (height keeps aspect).
 * returns RGB565 pixels (free with big_free) or NULL; *ow,*oh = source size */
unsigned short *imgdec_decode(const unsigned char *d, int len, int tw, int *th_out, char *err, int errlen);

#define IMG_MAX_SRC_PIXELS 4200000L  /* source image limit (e.g. 700 x 6000) */

#endif
