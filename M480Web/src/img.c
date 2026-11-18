/* M480Web - image decoding (stb_image) and scaling to RGB565 */
#include "util.h"
#include "html.h"
#include "layout.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(x) ((void)0)
#include "stb_image.h"

/* returns 1 ok */
int img_decode(const unsigned char *data, int len, int maxw, Img *im)
{
	int w, h, comp, tw, th, x, y;
	unsigned char *px;
	unsigned short *out;
	if (!stbi_info_from_memory(data, len, &w, &h, &comp)) return 0;
	if (w <= 0 || h <= 0 || w > 6000 || h > 6000 || (long)w * h > 1400000L) return 0;
	px = stbi_load_from_memory(data, len, &w, &h, &comp, 4);
	if (!px) return 0;
	if (im->w && im->h) { tw = SC(im->w); th = SC(im->h); }
	else if (im->w) { tw = SC(im->w); th = (int)((long)h * tw / w); }
	else if (im->h) { th = SC(im->h); tw = (int)((long)w * th / h); }
	else { tw = SC(w); th = SC(h); }
	if (tw > maxw) { th = (int)((long)th * maxw / tw); tw = maxw; }
	if (tw < 1) tw = 1;
	if (th < 1) th = 1;
	if ((long)tw * th > 900000L) { stbi_image_free(px); return 0; }
	out = (unsigned short *)malloc((size_t)tw * th * 2);
	if (!out) { stbi_image_free(px); return 0; }
	for (y = 0; y < th; y++) {
		int sy0 = (int)((long)y * h / th), sy1 = (int)((long)(y + 1) * h / th);
		if (sy1 <= sy0) sy1 = sy0 + 1;
		if (sy1 > h) sy1 = h;
		for (x = 0; x < tw; x++) {
			int sx0 = (int)((long)x * w / tw), sx1 = (int)((long)(x + 1) * w / tw);
			unsigned long r = 0, g = 0, b = 0, n = 0;
			int sx, sy, stepx, stepy;
			if (sx1 <= sx0) sx1 = sx0 + 1;
			if (sx1 > w) sx1 = w;
			/* limit work for huge downscales: sample at most 4x4 */
			stepx = (sx1 - sx0 + 3) / 4; if (stepx < 1) stepx = 1;
			stepy = (sy1 - sy0 + 3) / 4; if (stepy < 1) stepy = 1;
			for (sy = sy0; sy < sy1; sy += stepy) {
				const unsigned char *p = px + ((size_t)sy * w + sx0) * 4;
				for (sx = sx0; sx < sx1; sx += stepx, p += 4 * stepx) {
					unsigned a = p[3];
					r += (p[0] * a + 255 * (255 - a)) / 255;
					g += (p[1] * a + 255 * (255 - a)) / 255;
					b += (p[2] * a + 255 * (255 - a)) / 255;
					n++;
				}
			}
			r /= n; g /= n; b /= n;
			out[y * tw + x] = (unsigned short)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
		}
	}
	stbi_image_free(px);
	im->pix = out;
	im->iw = tw;
	im->ih = th;
	return 1;
}
