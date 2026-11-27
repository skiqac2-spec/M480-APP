/* M480Nico - thumbnail decoding (stb_image) to RGB565 */
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(x) ((void)0)
#include "stb_image.h"

/* decode and scale/crop ("cover") to tw x th. returns malloc'd pixels or NULL */
unsigned short *thumb_decode(const unsigned char *data, int len, int tw, int th)
{
	int w, h, comp, x, y, cw, ch, ox, oy;
	unsigned char *px;
	unsigned short *out;
	px = stbi_load_from_memory(data, len, &w, &h, &comp, 3);
	if (!px) return NULL;
	/* crop to target aspect */
	if ((long)w * th > (long)h * tw) { ch = h; cw = (int)((long)h * tw / th); }
	else { cw = w; ch = (int)((long)w * th / tw); }
	ox = (w - cw) / 2;
	oy = (h - ch) / 2;
	out = (unsigned short *)malloc((size_t)tw * th * 2);
	if (!out) { stbi_image_free(px); return NULL; }
	for (y = 0; y < th; y++) {
		int sy0 = oy + (int)((long)y * ch / th), sy1 = oy + (int)((long)(y + 1) * ch / th);
		if (sy1 <= sy0) sy1 = sy0 + 1;
		for (x = 0; x < tw; x++) {
			int sx0 = ox + (int)((long)x * cw / tw), sx1 = ox + (int)((long)(x + 1) * cw / tw);
			unsigned r = 0, g = 0, b = 0, n = 0;
			int sx, sy;
			if (sx1 <= sx0) sx1 = sx0 + 1;
			for (sy = sy0; sy < sy1 && sy < h; sy++)
				for (sx = sx0; sx < sx1 && sx < w; sx++) {
					const unsigned char *p = px + ((size_t)sy * w + sx) * 3;
					r += p[0]; g += p[1]; b += p[2]; n++;
				}
			if (!n) n = 1;
			r /= n; g /= n; b /= n;
			out[y * tw + x] = (unsigned short)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
		}
	}
	stbi_image_free(px);
	return out;
}
