#ifndef M480_LAYOUT_H
#define M480_LAYOUT_H

#include "html.h"

typedef struct {
	int x, y, w, h;
	int lb;        /* line bottom */
	int item;
	int start, len;
} Box;

typedef struct {
	Doc *doc;
	Box *boxes; int nboxes, cap;
	int width, height;
} Layout;

extern int g_scale100;     /* DPI scale *100 */
extern int g_images_on;

void fonts_set_base(int px);
int  fonts_base(void);
HFONT font_for(int style);
int  font_height(int style);

void layout_doc(Layout *L, HDC hdc, Doc *d, int width);
void layout_free(Layout *L);
void layout_paint(Layout *L, HDC hdc, int scrolly, int viewh, int pressed_link, int pressed_ctl);
/* returns 1 if hit; sets link / ctl (-1 if none) */
int  layout_hit(Layout *L, int x, int y, int *link, int *ctl);
int  layout_item_y(Layout *L, int item);
int  layout_ctl_box(Layout *L, int ctl, RECT *r);
void img_make_bitmap(Img *im);

#define SC(v) (((v) * g_scale100 + 50) / 100)

#endif
