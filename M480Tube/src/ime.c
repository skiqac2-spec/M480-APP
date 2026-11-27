/* romaji -> hiragana, the usual (Hepburn + wapuro) spellings */
#include <string.h>
#include "ime.h"

typedef struct { const char *r; const char *k; } Rk;   /* k is UTF-8 */
static const Rk g_rk[] = {
	{"a","あ"},{"i","い"},{"u","う"},{"e","え"},{"o","お"},
	{"ka","か"},{"ki","き"},{"ku","く"},{"ke","け"},{"ko","こ"},
	{"sa","さ"},{"si","し"},{"shi","し"},{"su","す"},{"se","せ"},{"so","そ"},
	{"ta","た"},{"ti","ち"},{"chi","ち"},{"tu","つ"},{"tsu","つ"},{"te","て"},{"to","と"},
	{"na","な"},{"ni","に"},{"nu","ぬ"},{"ne","ね"},{"no","の"},
	{"ha","は"},{"hi","ひ"},{"hu","ふ"},{"fu","ふ"},{"he","へ"},{"ho","ほ"},
	{"ma","ま"},{"mi","み"},{"mu","む"},{"me","め"},{"mo","も"},
	{"ya","や"},{"yu","ゆ"},{"yo","よ"},{"ye","いぇ"},
	{"ra","ら"},{"ri","り"},{"ru","る"},{"re","れ"},{"ro","ろ"},
	{"la","ら"},{"li","り"},{"lu","る"},{"le","れ"},{"lo","ろ"},
	{"wa","わ"},{"wi","うぃ"},{"we","うぇ"},{"wo","を"},{"nn","ん"},{"n'","ん"},{"xn","ん"},
	{"ga","が"},{"gi","ぎ"},{"gu","ぐ"},{"ge","げ"},{"go","ご"},
	{"za","ざ"},{"zi","じ"},{"ji","じ"},{"zu","ず"},{"ze","ぜ"},{"zo","ぞ"},
	{"da","だ"},{"di","ぢ"},{"du","づ"},{"de","で"},{"do","ど"},
	{"ba","ば"},{"bi","び"},{"bu","ぶ"},{"be","べ"},{"bo","ぼ"},
	{"pa","ぱ"},{"pi","ぴ"},{"pu","ぷ"},{"pe","ぺ"},{"po","ぽ"},
	{"va","ゔぁ"},{"vi","ゔぃ"},{"vu","ゔ"},{"ve","ゔぇ"},{"vo","ゔぉ"},
	{"kya","きゃ"},{"kyu","きゅ"},{"kyo","きょ"},{"kye","きぇ"},
	{"sha","しゃ"},{"shu","しゅ"},{"sho","しょ"},{"she","しぇ"},{"sya","しゃ"},{"syu","しゅ"},{"syo","しょ"},
	{"cha","ちゃ"},{"chu","ちゅ"},{"cho","ちょ"},{"che","ちぇ"},{"tya","ちゃ"},{"tyu","ちゅ"},{"tyo","ちょ"},
	{"cya","ちゃ"},{"cyu","ちゅ"},{"cyo","ちょ"},
	{"nya","にゃ"},{"nyu","にゅ"},{"nyo","にょ"},
	{"hya","ひゃ"},{"hyu","ひゅ"},{"hyo","ひょ"},
	{"mya","みゃ"},{"myu","みゅ"},{"myo","みょ"},
	{"rya","りゃ"},{"ryu","りゅ"},{"ryo","りょ"},
	{"gya","ぎゃ"},{"gyu","ぎゅ"},{"gyo","ぎょ"},
	{"ja","じゃ"},{"ju","じゅ"},{"jo","じょ"},{"je","じぇ"},{"jya","じゃ"},{"jyu","じゅ"},{"jyo","じょ"},
	{"zya","じゃ"},{"zyu","じゅ"},{"zyo","じょ"},
	{"bya","びゃ"},{"byu","びゅ"},{"byo","びょ"},
	{"pya","ぴゃ"},{"pyu","ぴゅ"},{"pyo","ぴょ"},
	{"dya","ぢゃ"},{"dyu","ぢゅ"},{"dyo","ぢょ"},
	{"fa","ふぁ"},{"fi","ふぃ"},{"fe","ふぇ"},{"fo","ふぉ"},
	{"thi","てぃ"},{"dhi","でぃ"},{"twu","とぅ"},{"dwu","どぅ"},{"tsa","つぁ"},
	{"xa","ぁ"},{"xi","ぃ"},{"xu","ぅ"},{"xe","ぇ"},{"xo","ぉ"},
	{"xya","ゃ"},{"xyu","ゅ"},{"xyo","ょ"},{"xtu","っ"},{"xtsu","っ"},{"ltu","っ"},
	{"-","ー"},{",","、"},{".","。"},{"[","「"},{"]","」"},
	{NULL,NULL}
};

static int utf8_dec(const char *s, WCHAR *out)
{
	int n = 0;
	const unsigned char *p = (const unsigned char *)s;
	while (*p) {
		unsigned c = *p;
		if (c < 0x80) { out[n++] = (WCHAR)c; p++; }
		else if ((c & 0xE0) == 0xC0) { out[n++] = (WCHAR)(((c & 0x1F) << 6) | (p[1] & 0x3F)); p += 2; }
		else { out[n++] = (WCHAR)(((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F)); p += 3; }
	}
	return n;
}

static int is_rlet(WCHAR c) { return (c >= 'a' && c <= 'z') || c == '\'' || c == '-' || c == ',' || c == '.' || c == '[' || c == ']'; }
static int is_vowel(WCHAR c) { return c == 'a' || c == 'i' || c == 'u' || c == 'e' || c == 'o'; }

/* does some table key start with s[0..n)? */
static int is_prefix(const WCHAR *s, int n)
{
	int i, k;
	for (i = 0; g_rk[i].r; i++) {
		const char *r = g_rk[i].r;
		if ((int)strlen(r) <= n) continue;
		for (k = 0; k < n && r[k] == (char)s[k]; k++) ;
		if (k == n) return 1;
	}
	return 0;
}

int ime_romaji(WCHAR *buf, int len, int from)
{
	WCHAR out[512];
	int i = from, o = 0, k;
	if (len > 500) return len;
	while (i < len) {
		WCHAR c = buf[i];
		int best = -1, blen = 0;
		if (c >= 'A' && c <= 'Z') c = (WCHAR)(c + 32);
		if (!is_rlet(c)) { out[o++] = buf[i++]; continue; }
		/* small tsu: doubled consonant ("kk" -> っk) */
		if (i + 1 < len && c != 'n' && !is_vowel(c) && c >= 'a' && c <= 'z' && (buf[i + 1] | 32) == c) {
			out[o++] = 0x3063;
			i++;
			continue;
		}
		/* "nn": ん, but "nni" = ん + に (konnichiha) */
		if (c == 'n' && i + 1 < len && (buf[i + 1] | 32) == 'n') {
			WCHAR d = i + 2 < len ? (WCHAR)(buf[i + 2] | 32) : 0;
			out[o++] = 0x3093;
			i += (is_vowel(d) || d == 'y') ? 1 : 2;
			continue;
		}
		/* n before a consonant -> ん */
		if (c == 'n' && i + 1 < len) {
			WCHAR d = (WCHAR)(buf[i + 1] | 32);
			if (d >= 'a' && d <= 'z' && !is_vowel(d) && d != 'y' && d != 'n') { out[o++] = 0x3093; i++; continue; }
		}
		for (k = 0; g_rk[k].r; k++) {
			const char *r = g_rk[k].r;
			int n = (int)strlen(r), j;
			if (n <= blen || i + n > len) continue;
			for (j = 0; j < n && r[j] == (char)(buf[i + j] >= 'A' && buf[i + j] <= 'Z' ? buf[i + j] + 32 : buf[i + j]); j++) ;
			if (j == n) { best = k; blen = n; }
		}
		if (best >= 0) {
			/* "n" alone could still become "na"/"nya": wait unless something follows */
			o += utf8_dec(g_rk[best].k, out + o);
			i += blen;
			continue;
		}
		/* incomplete: leave the rest as typed if it can still become something */
		{
			WCHAR tmp[8];
			int n = len - i, j;
			if (n < 8) {
				for (j = 0; j < n; j++) tmp[j] = (WCHAR)(buf[i + j] >= 'A' && buf[i + j] <= 'Z' ? buf[i + j] + 32 : buf[i + j]);
				if (is_prefix(tmp, n)) { while (i < len) out[o++] = buf[i++]; break; }
			}
		}
		out[o++] = buf[i++];
	}
	memcpy(buf + from, out, o * sizeof(WCHAR));
	buf[from + o] = 0;
	return from + o;
}

int ime_finish(WCHAR *buf, int len)
{
	if (len > 0 && (buf[len - 1] == 'n' || buf[len - 1] == 'N') && (len < 2 || !((buf[len - 2] | 32) >= 'a' && (buf[len - 2] | 32) <= 'z'))) {
		buf[len - 1] = 0x3093;
		buf[len] = 0;
	}
	return len;
}

void ime_katakana(WCHAR *s, int n)
{
	int i;
	for (i = 0; i < n; i++) if (s[i] >= 0x3041 && s[i] <= 0x3096) s[i] = (WCHAR)(s[i] + 0x60);
}

int ime_is_comp(WCHAR c)
{
	return (c >= 0x3041 && c <= 0x30FF) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\'';
}
