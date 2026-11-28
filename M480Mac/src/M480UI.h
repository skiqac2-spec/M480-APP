/*
	M480UI.h

	SCH-M480 (Windows Mobile 6.1, 320x320 touch screen) front end
	for Mini vMac (Mac II, 640x480, 256 colors).

	- the whole Mac screen is shown at half size (320x240) in color,
	  every 2x2 block of Mac pixels is blended (slightly toward the
	  darkest pixel so thin lines and text stay visible)
	- touching the picture moves the Mac mouse there and presses it
	- a button bar fills the rest of the screen (320x80):
	  disk / keyboard / command / option / shift / control / speed / quit
	- a touch friendly disk image chooser (app folder and
	  \Storage Card\M480Mac)

	Included from OSGLUWIN.c when M480UI is 1.
*/

#define M480_W 320
#define M480_H 320
#define M480_MacW (vMacScreenWidth / 2)
#define M480_MacH (vMacScreenHeight / 2)
#define M480_BarY M480_MacH
#define M480_BarH (M480_H - M480_BarY)

#ifndef SHFS_HIDETASKBAR
#define SHFS_SHOWTASKBAR   0x0001
#define SHFS_HIDETASKBAR   0x0002
#define SHFS_SHOWSIPBUTTON 0x0004
#define SHFS_HIDESIPBUTTON 0x0008
#define SHFS_SHOWSTARTICON 0x0010
#define SHFS_HIDESTARTICON 0x0020
#endif
#ifndef ANTIALIASED_QUALITY
#define ANTIALIASED_QUALITY 4
#endif
#ifndef DT_END_ELLIPSIS
#define DT_END_ELLIPSIS 0
#endif
#ifndef VK_PROCESSKEY
#define VK_PROCESSKEY 0xE5
#endif

#ifdef __MINGW32CE__
void WINAPI SystemIdleTimerReset(void);
#else
#define SystemIdleTimerReset() ((void)0)
#endif

LOCALVAR HDC m480_memdc = NULL;
LOCALVAR HBITMAP m480_bmp = NULL;
LOCALVAR HBITMAP m480_oldbmp = NULL;
LOCALVAR ui4b *m480_fb = NULL;
LOCALVAR int m480_xoff = 0;
LOCALVAR int m480_yoff = 0;
LOCALVAR HFONT m480_font = NULL;
LOCALVAR HFONT m480_fontS = NULL;

/* palette split into 5/6/5 bit parts */
LOCALVAR ui3b m480_pr[CLUT_size];
LOCALVAR ui3b m480_pg[CLUT_size];
LOCALVAR ui3b m480_pb[CLUT_size];
LOCALVAR ui4b m480_p565[CLUT_size];

/* gray for 0..4 black pixels in a 2x2 block (mono mode) */
LOCALVAR ui4b m480_gray[5];

/* touch state */
LOCALVAR blnr m480_macPress = falseblnr;
LOCALVAR blnr m480_barPress = falseblnr;
LOCALVAR int m480_barHot = -1;

/* sticky modifier keys */
enum {
	m480_kDisk,
	m480_kKeyboard,
	m480_kCommand,
	m480_kOption,
	m480_kShift,
	m480_kControl,
	m480_kSpeed,
	m480_kQuit,
	m480_kNumButtons
};
LOCALVAR blnr m480_modOn[m480_kNumButtons];

/* disk chooser */
#define M480_MaxFiles 96
#define M480_ListRowH 28
#define M480_ListTitleH 28
#define M480_ListRows 9
typedef struct {
	TCHAR path[_MAX_PATH];
	TCHAR name[48];
	ui5b kb;
} m480_file;
LOCALVAR m480_file *m480_files = NULL;
LOCALVAR int m480_nFiles = 0;
LOCALVAR int m480_listTop = 0;
LOCALVAR int m480_listHot = -1;
LOCALVAR blnr m480_listMode = falseblnr;
LOCALVAR int m480_listResult = -1;

/* data folders: app folder, then <card>\M480Mac */
#define M480_MaxCards 4
LOCALVAR TCHAR m480_cards[M480_MaxCards][_MAX_PATH];
LOCALVAR int m480_nCards = 0;
LOCALVAR blnr m480_cardsFound = falseblnr;

LOCALPROC M480_FindCards(void)
{
	WIN32_FIND_DATA fd;
	HANDLE h = FindFirstFile(TEXT("\\*"), &fd);

	m480_nCards = 0;
	m480_cardsFound = trueblnr;
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				&& (fd.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY)
				&& (m480_nCards < M480_MaxCards))
			{
				_tcscpy(m480_cards[m480_nCards], TEXT("\\"));
				_tcscat(m480_cards[m480_nCards], fd.cFileName);
				_tcscat(m480_cards[m480_nCards], TEXT("\\M480Mac"));
				++m480_nCards;
			}
		} while (FindNextFile(h, &fd));
		FindClose(h);
	}
}

/* i = 0: app folder, 1..: card folders. false when no more */
LOCALFUNC blnr M480_DataDir(int i, LPTSTR path)
{
	if (0 == i) {
		return GetAppDir(path);
	}
	if (! m480_cardsFound) {
		M480_FindCards();
	}
	if (i - 1 < m480_nCards) {
		_tcscpy(path, m480_cards[i - 1]);
		return trueblnr;
	}
	return falseblnr;
}

LOCALFUNC HFONT M480_MakeFont(int h, blnr bold)
{
	LOGFONT lf;

	memset(&lf, 0, sizeof(lf));
	lf.lfHeight = -h;
	lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
	lf.lfCharSet = DEFAULT_CHARSET;
	lf.lfQuality = ANTIALIASED_QUALITY;
	return CreateFontIndirect(&lf);
}

LOCALPROC M480_FullScreen(void)
{
	if (MainWnd != NULL) {
		SHFullScreen(MainWnd,
			SHFS_HIDETASKBAR | SHFS_HIDESIPBUTTON | SHFS_HIDESTARTICON);
		(void) MoveWindow(MainWnd, 0, 0,
			GetSystemMetrics(SM_CXSCREEN),
			GetSystemMetrics(SM_CYSCREEN), FALSE);
	}
}

LOCALFUNC blnr M480_Init(void)
{
	struct {
		BITMAPINFOHEADER h;
		DWORD mask[3];
	} bi;
	void *bits = NULL;
	int i;
	int sw = GetSystemMetrics(SM_CXSCREEN);
	int sh = GetSystemMetrics(SM_CYSCREEN);

	if (NULL != m480_memdc) {
		return trueblnr;
	}

	m480_xoff = (sw > M480_W) ? (sw - M480_W) / 2 : 0;
	m480_yoff = (sh > M480_H) ? (sh - M480_H) / 2 : 0;

	memset(&bi, 0, sizeof(bi));
	bi.h.biSize = sizeof(BITMAPINFOHEADER);
	bi.h.biWidth = M480_W;
	bi.h.biHeight = - M480_H;
	bi.h.biPlanes = 1;
	bi.h.biBitCount = 16;
	bi.h.biCompression = BI_BITFIELDS;
	bi.mask[0] = 0xF800;
	bi.mask[1] = 0x07E0;
	bi.mask[2] = 0x001F;

	m480_memdc = CreateCompatibleDC(MainWndDC);
	m480_bmp = CreateDIBSection(MainWndDC, (BITMAPINFO *)&bi,
		DIB_RGB_COLORS, &bits, NULL, 0);
	if ((NULL == m480_memdc) || (NULL == m480_bmp) || (NULL == bits)) {
		return falseblnr;
	}
	m480_oldbmp = (HBITMAP)SelectObject(m480_memdc, m480_bmp);
	m480_fb = (ui4b *)bits;
	memset(m480_fb, 0, M480_W * M480_H * 2);
	SetBkMode(m480_memdc, TRANSPARENT);

	m480_font = M480_MakeFont(16, trueblnr);
	m480_fontS = M480_MakeFont(13, falseblnr);

	/* 1 black pixel of 4 -> visibly gray, uniform areas unchanged */
	for (i = 0; i <= 4; ++i) {
		int avg = 255 * (4 - i) / 4;
		int mn = (i > 0) ? 0 : 255;
		int v = (3 * avg + mn) / 4;
		m480_gray[i] = (ui4b)(((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3));
	}

	for (i = 0; i < m480_kNumButtons; ++i) {
		m480_modOn[i] = falseblnr;
	}

	/* raw hardware keys, no Hangul composition */
	(void) ImmAssociateContext(MainWnd, (HIMC)0);
	(void) SetThreadPriority(GetCurrentThread(),
		THREAD_PRIORITY_ABOVE_NORMAL);

	return trueblnr;
}

LOCALPROC M480_UnInit(void)
{
	if (NULL != m480_memdc) {
		SelectObject(m480_memdc, m480_oldbmp);
		DeleteDC(m480_memdc);
		m480_memdc = NULL;
	}
	if (NULL != m480_bmp) {
		DeleteObject(m480_bmp);
		m480_bmp = NULL;
	}
	if (NULL != m480_font) {
		DeleteObject(m480_font);
	}
	if (NULL != m480_fontS) {
		DeleteObject(m480_fontS);
	}
	if (NULL != m480_files) {
		free(m480_files);
		m480_files = NULL;
	}
}

LOCALPROC M480_Blit(int x, int y, int w, int h)
{
	if ((NULL != MainWndDC) && (w > 0) && (h > 0)) {
		BitBlt(MainWndDC, m480_xoff + x, m480_yoff + y, w, h,
			m480_memdc, x, y, SRCCOPY);
	}
}

/* --- Mac screen, half size --- */

LOCALPROC M480_UpdatePalette(void)
{
#if 0 != vMacScreenDepth
	int i;

	for (i = 0; i < CLUT_size; ++i) {
		ui3r r = CLUT_reds[i] >> 11;
		ui3r g = CLUT_greens[i] >> 10;
		ui3r b = CLUT_blues[i] >> 11;

		m480_pr[i] = r;
		m480_pg[i] = g;
		m480_pb[i] = b;
		m480_p565[i] = (ui4b)((r << 11) | (g << 5) | b);
	}
#endif
}

#define M480_Mix(s0, s1, s2, s3, sum, mn) \
	sum = (s0) + (s1) + (s2) + (s3); \
	mn = (s0); \
	if ((s1) < mn) { mn = (s1); } \
	if ((s2) < mn) { mn = (s2); } \
	if ((s3) < mn) { mn = (s3); }

LOCALPROC M480_DrawMac(si4b top, si4b left, si4b bottom, si4b right)
{
	ui3b *cdb;
	int t;
	int b;
	int l;
	int r;
	int x;
	int y;

	if ((NULL == m480_fb) || m480_listMode) {
		return;
	}

	if (top < 0) {
		top = 0;
	}
	if (left < 0) {
		left = 0;
	}
	if (bottom > vMacScreenHeight) {
		bottom = vMacScreenHeight;
	}
	if (right > vMacScreenWidth) {
		right = vMacScreenWidth;
	}
	t = top >> 1;
	l = left >> 1;
	b = (bottom + 1) >> 1;
	r = (right + 1) >> 1;
	if ((t >= b) || (l >= r)) {
		return;
	}

	cdb = GetCurDrawBuff();

#if 0 != vMacScreenDepth
	if (UseColorMode) {
		M480_UpdatePalette();
		for (y = t; y < b; ++y) {
			ui3b *s0 = cdb + (ui5r)vMacScreenByteWidth * (2 * y) + 2 * l;
			ui3b *s1 = s0 + vMacScreenByteWidth;
			ui4b *d = m480_fb + y * M480_W + l;

			for (x = r - l; --x >= 0; ) {
				ui3r a0 = s0[0];
				ui3r a1 = s0[1];
				ui3r a2 = s1[0];
				ui3r a3 = s1[1];

				if ((a0 == a1) && (a0 == a2) && (a0 == a3)) {
					*d = m480_p565[a0];
				} else {
					int sum;
					int mn;
					int cr;
					int cg;
					int cb;

					M480_Mix(m480_pr[a0], m480_pr[a1],
						m480_pr[a2], m480_pr[a3], sum, mn);
					cr = (3 * sum + 4 * mn) >> 4;
					M480_Mix(m480_pg[a0], m480_pg[a1],
						m480_pg[a2], m480_pg[a3], sum, mn);
					cg = (3 * sum + 4 * mn) >> 4;
					M480_Mix(m480_pb[a0], m480_pb[a1],
						m480_pb[a2], m480_pb[a3], sum, mn);
					cb = (3 * sum + 4 * mn) >> 4;
					*d = (ui4b)((cr << 11) | (cg << 5) | cb);
				}
				++d;
				s0 += 2;
				s1 += 2;
			}
		}
	} else
#endif
	{
		/* 1 bit per pixel, 1 = black */
		for (y = t; y < b; ++y) {
			ui3b *s0 = cdb + (ui5r)vMacScreenMonoByteWidth * (2 * y);
			ui3b *s1 = s0 + vMacScreenMonoByteWidth;
			ui4b *d = m480_fb + y * M480_W + l;

			for (x = l; x < r; ++x) {
				int sh = 6 - ((2 * x) & 7);
				int v0 = (s0[(2 * x) >> 3] >> sh) & 3;
				int v1 = (s1[(2 * x) >> 3] >> sh) & 3;
				int n = (v0 & 1) + (v0 >> 1) + (v1 & 1) + (v1 >> 1);

				*d++ = m480_gray[n];
			}
		}
	}

	M480_Blit(l, t, r - l, b - t);
}

/* --- button bar --- */

LOCALVAR const TCHAR *m480_labels[m480_kNumButtons] = {
	TEXT("디스크"),
	TEXT("키보드"),
	TEXT("커맨드"),
	TEXT("옵션"),
	TEXT("시프트"),
	TEXT("컨트롤"),
	TEXT("속도"),
	TEXT("종료")
};

LOCALPROC M480_ButtonRect(int i, RECT *rc)
{
	int col = i & 3;
	int row = i >> 2;

	rc->left = col * (M480_W / 4);
	rc->right = rc->left + (M480_W / 4);
	rc->top = M480_BarY + row * (M480_BarH / 2);
	rc->bottom = rc->top + (M480_BarH / 2);
}

LOCALFUNC int M480_ButtonAt(int x, int y)
{
	int col;
	int row;

	if ((y < M480_BarY) || (y >= M480_H) || (x < 0) || (x >= M480_W)) {
		return -1;
	}
	col = x / (M480_W / 4);
	row = (y - M480_BarY) / (M480_BarH / 2);
	return row * 4 + col;
}

LOCALPROC M480_FillRect(RECT *rc, COLORREF c)
{
	HBRUSH br = CreateSolidBrush(c);
	FillRect(m480_memdc, rc, br);
	DeleteObject(br);
}

LOCALPROC M480_Text(LPCTSTR s, RECT *rc, COLORREF c, HFONT f, UINT fmt)
{
	HFONT old = (HFONT)SelectObject(m480_memdc, f);
	SetTextColor(m480_memdc, c);
	DrawText(m480_memdc, s, -1, rc, fmt);
	SelectObject(m480_memdc, old);
}

LOCALPROC M480_DrawBar(void)
{
	int i;
	RECT rc;

	if ((NULL == m480_memdc) || m480_listMode) {
		return;
	}

	rc.left = 0;
	rc.top = M480_BarY;
	rc.right = M480_W;
	rc.bottom = M480_H;
	M480_FillRect(&rc, RGB(24, 24, 28));

	for (i = 0; i < m480_kNumButtons; ++i) {
		RECT r2;
		RECT r3;
		COLORREF bg;
		COLORREF fg = RGB(235, 235, 235);
		TCHAR sub[16];

		M480_ButtonRect(i, &r2);
		r2.left += 2;
		r2.top += 2;
		r2.right -= 2;
		r2.bottom -= 2;

		if (i == m480_barHot) {
			bg = RGB(120, 120, 140);
		} else if (m480_modOn[i]) {
			bg = RGB(40, 110, 200);
		} else if (m480_kQuit == i) {
			bg = RGB(90, 40, 40);
		} else if (m480_kDisk == i) {
			bg = RGB(40, 80, 60);
		} else {
			bg = RGB(58, 58, 66);
		}
		M480_FillRect(&r2, bg);

		sub[0] = 0;
		if ((m480_kCommand <= i) && (i <= m480_kControl)) {
			_tcscpy(sub, m480_modOn[i] ? TEXT("켜짐") : TEXT(""));
		} else if (m480_kSpeed == i) {
			switch (SpeedValue) {
				case 0:
					_tcscpy(sub, TEXT("1배"));
					break;
				case 1:
					_tcscpy(sub, TEXT("2배"));
					break;
				case 2:
					_tcscpy(sub, TEXT("4배"));
					break;
				default:
					_tcscpy(sub, TEXT("최대"));
					break;
			}
		}

		if (sub[0]) {
			r3 = r2;
			r3.bottom = r2.top + (r2.bottom - r2.top) * 3 / 5;
			M480_Text(m480_labels[i], &r3, fg, m480_font,
				DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
			r3.top = r3.bottom;
			r3.bottom = r2.bottom;
			M480_Text(sub, &r3, RGB(255, 230, 120), m480_fontS,
				DT_CENTER | DT_TOP | DT_SINGLELINE);
		} else {
			M480_Text(m480_labels[i], &r2, fg, m480_font,
				DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
	}

	M480_Blit(0, M480_BarY, M480_W, M480_BarH);
}

LOCALPROC M480_SetMod(int i, blnr on)
{
	ui3r key;

	switch (i) {
		case m480_kCommand:
			key = MKC_formac_Command;
			break;
		case m480_kOption:
			key = MKC_formac_Option;
			break;
		case m480_kShift:
			key = MKC_formac_Shift;
			break;
		default:
			key = MKC_Control;
			break;
	}
	m480_modOn[i] = on;
	Keyboard_UpdateKeyMap2(key, on);
}

LOCALPROC M480_ReleaseMods(void)
{
	int i;

	for (i = m480_kCommand; i <= m480_kControl; ++i) {
		if (m480_modOn[i]) {
			M480_SetMod(i, falseblnr);
		}
	}
}

LOCALPROC M480_ToggleSip(void)
{
	SIPINFO si;
	blnr on = falseblnr;

	memset(&si, 0, sizeof(si));
	si.cbSize = sizeof(si);
	if (SipGetInfo(&si)) {
		on = 0 != (si.fdwFlags & SIPF_ON);
	}
	SipShowIM(on ? SIPF_OFF : SIPF_ON);
	m480_modOn[m480_kKeyboard] = ! on;
}

LOCALPROC M480_AskQuit(void);

LOCALPROC M480_BarAction(int i)
{
	switch (i) {
		case m480_kDisk:
			RequestInsertDisk = trueblnr;
			break;
		case m480_kKeyboard:
			M480_ToggleSip();
			break;
		case m480_kCommand:
		case m480_kOption:
		case m480_kShift:
		case m480_kControl:
			M480_SetMod(i, ! m480_modOn[i]);
			break;
		case m480_kSpeed:
			switch (SpeedValue) {
				case 0:
					SpeedValue = 1;
					break;
				case 1:
					SpeedValue = 2;
					break;
				case 2:
					SpeedValue = (ui3b) -1;
					break;
				default:
					SpeedValue = 0;
					break;
			}
			break;
		case m480_kQuit:
			M480_AskQuit();
			break;
	}
}

/* --- touch on the Mac picture --- */

LOCALPROC M480_MacMouse(int x, int y)
{
	int mx = (x - m480_xoff) * 2;
	int my = (y - m480_yoff) * 2;

	if (mx < 0) {
		mx = 0;
	} else if (mx >= vMacScreenWidth) {
		mx = vMacScreenWidth - 1;
	}
	if (my < 0) {
		my = 0;
	} else if (my >= vMacScreenHeight) {
		my = vMacScreenHeight - 1;
	}
	MyMousePositionSet(mx, my);
}

/* --- disk image chooser --- */

LOCALFUNC blnr M480_IsDiskName(LPCTSTR n)
{
	static const TCHAR *exts[] = {
		TEXT(".dsk"), TEXT(".img"), TEXT(".image"), TEXT(".hfv"),
		TEXT(".hda"), TEXT(".dc42"), TEXT(".ima"), TEXT(".dmg"),
		TEXT(".rom"), NULL
	};
	LPCTSTR dot = _tcsrchr(n, (TCHAR)('.'));
	int i;

	if (NULL == dot) {
		return falseblnr;
	}
	for (i = 0; exts[i]; ++i) {
		if (0 == _tcsicmp(dot, exts[i])) {
			return trueblnr;
		}
	}
	return falseblnr;
}

LOCALPROC M480_ScanDir(LPCTSTR dir)
{
	TCHAR pat[_MAX_PATH];
	WIN32_FIND_DATA fd;
	HANDLE h;

	if (_tcslen(dir) + 4 >= _MAX_PATH) {
		return;
	}
	_tcscpy(pat, dir);
	_tcscat(pat, TEXT("\\*"));
	h = FindFirstFile(pat, &fd);
	if (h == INVALID_HANDLE_VALUE) {
		return;
	}
	do {
		if (0 == (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			&& M480_IsDiskName(fd.cFileName)
			&& (m480_nFiles < M480_MaxFiles)
			&& (_tcslen(dir) + 2 + _tcslen(fd.cFileName) < _MAX_PATH))
		{
			m480_file *f = &m480_files[m480_nFiles++];

			_tcscpy(f->path, dir);
			_tcscat(f->path, TEXT("\\"));
			_tcscat(f->path, fd.cFileName);
			_tcsncpy(f->name, fd.cFileName, 47);
			f->name[47] = 0;
			f->kb = (ui5b)(fd.nFileSizeLow >> 10)
				| (ui5b)(fd.nFileSizeHigh << 22);
		}
	} while (FindNextFile(h, &fd));
	FindClose(h);
}

LOCALPROC M480_BuildList(void)
{
	TCHAR dir[_MAX_PATH];
	TCHAR sub[_MAX_PATH];
	int i;

	if (NULL == m480_files) {
		m480_files = (m480_file *)malloc(sizeof(m480_file) * M480_MaxFiles);
	}
	m480_nFiles = 0;
	if (NULL == m480_files) {
		return;
	}
	M480_FindCards();
	for (i = 0; M480_DataDir(i, dir); ++i) {
		M480_ScanDir(dir);
		if (_tcslen(dir) + 7 < _MAX_PATH) {
			_tcscpy(sub, dir);
			_tcscat(sub, TEXT("\\disks"));
			M480_ScanDir(sub);
		}
	}
}

LOCALPROC M480_DrawList(void)
{
	RECT rc;
	int i;
	TCHAR s[80];

	if (NULL == m480_memdc) {
		return;
	}

	rc.left = 0;
	rc.top = 0;
	rc.right = M480_W;
	rc.bottom = M480_H;
	M480_FillRect(&rc, RGB(20, 22, 30));

	rc.bottom = M480_ListTitleH;
	M480_FillRect(&rc, RGB(40, 80, 60));
	wsprintf(s, TEXT("디스크 고르기 (%d개)"), m480_nFiles);
	M480_Text(s, &rc, RGB(255, 255, 255), m480_font,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	if (0 == m480_nFiles) {
		rc.top = M480_ListTitleH + 20;
		rc.bottom = M480_ListTitleH + M480_ListRows * M480_ListRowH;
		rc.left = 10;
		rc.right = M480_W - 10;
		M480_Text(TEXT("디스크 이미지가 없습니다.\n\n")
			TEXT("저장 카드의 M480Mac 폴더에\n")
			TEXT(".dsk / .img / .image 파일을\n넣어 주세요."),
			&rc, RGB(230, 230, 230), m480_font, DT_CENTER | DT_WORDBREAK);
	}

	for (i = 0; i < M480_ListRows; ++i) {
		int k = m480_listTop + i;
		RECT r2;
		RECT r3;

		if (k >= m480_nFiles) {
			break;
		}
		r2.left = 2;
		r2.right = M480_W - 2;
		r2.top = M480_ListTitleH + i * M480_ListRowH + 1;
		r2.bottom = r2.top + M480_ListRowH - 2;
		M480_FillRect(&r2, (k == m480_listHot)
			? RGB(40, 110, 200) : RGB(48, 50, 60));

		r3 = r2;
		r3.left += 6;
		r3.right -= 70;
		M480_Text(m480_files[k].name, &r3, RGB(240, 240, 240), m480_font,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

		if (m480_files[k].kb >= 1024) {
			wsprintf(s, TEXT("%u.%uMB"),
				(unsigned)(m480_files[k].kb / 1024),
				(unsigned)((m480_files[k].kb % 1024) * 10 / 1024));
		} else {
			wsprintf(s, TEXT("%uKB"), (unsigned)m480_files[k].kb);
		}
		r3.left = r3.right;
		r3.right = r2.right - 6;
		M480_Text(s, &r3, RGB(170, 200, 255), m480_fontS,
			DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	}

	/* bottom buttons: up / down / cancel */
	for (i = 0; i < 3; ++i) {
		static const TCHAR *lb[3] = {
			TEXT("▲ 위로"), TEXT("▼ 아래로"), TEXT("취소")
		};
		RECT r2;

		r2.left = i * (M480_W / 3) + 2;
		r2.right = (i + 1) * (M480_W / 3) - 2;
		r2.top = M480_ListTitleH + M480_ListRows * M480_ListRowH + 4;
		r2.bottom = M480_H - 2;
		M480_FillRect(&r2, (2 == i) ? RGB(90, 40, 40) : RGB(58, 58, 66));
		M480_Text(lb[i], &r2, RGB(240, 240, 240), m480_font,
			DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}

	M480_Blit(0, 0, M480_W, M480_H);
}

LOCALPROC M480_ListScroll(int d)
{
	int maxTop = m480_nFiles - M480_ListRows;

	if (maxTop < 0) {
		maxTop = 0;
	}
	m480_listTop += d;
	if (m480_listTop > maxTop) {
		m480_listTop = maxTop;
	}
	if (m480_listTop < 0) {
		m480_listTop = 0;
	}
	M480_DrawList();
}

LOCALFUNC int M480_ListItemAt(int y)
{
	int row;

	if ((y < M480_ListTitleH)
		|| (y >= M480_ListTitleH + M480_ListRows * M480_ListRowH))
	{
		return -1;
	}
	row = (y - M480_ListTitleH) / M480_ListRowH;
	if (m480_listTop + row >= m480_nFiles) {
		return -1;
	}
	return m480_listTop + row;
}

LOCALPROC M480_ListDown(int x, int y)
{
	x -= m480_xoff;
	y -= m480_yoff;
	m480_listHot = M480_ListItemAt(y);
	if (m480_listHot >= 0) {
		M480_DrawList();
	}
}

LOCALPROC M480_ListUp(int x, int y)
{
	int k;

	x -= m480_xoff;
	y -= m480_yoff;
	k = M480_ListItemAt(y);
	if ((k >= 0) && (k == m480_listHot)) {
		m480_listResult = k;
		m480_listMode = falseblnr;
		return;
	}
	m480_listHot = -1;
	if (y >= M480_ListTitleH + M480_ListRows * M480_ListRowH) {
		int b = x / (M480_W / 3);

		if (0 == b) {
			M480_ListScroll(- (M480_ListRows - 1));
		} else if (1 == b) {
			M480_ListScroll(M480_ListRows - 1);
		} else {
			m480_listResult = -1;
			m480_listMode = falseblnr;
			return;
		}
	} else {
		M480_DrawList();
	}
}

LOCALPROC M480_ListKey(WPARAM vk)
{
	switch (vk) {
		case VK_UP:
			M480_ListScroll(-1);
			break;
		case VK_DOWN:
			M480_ListScroll(1);
			break;
		case VK_ESCAPE:
		case VK_BACK:
			m480_listResult = -1;
			m480_listMode = falseblnr;
			break;
	}
}

/* shows the chooser, returns true and the path when a file was picked */
LOCALFUNC blnr M480_ChooseDisk(LPTSTR path)
{
	MSG msg;
	blnr IsOk = falseblnr;

	M480_ReleaseMods();
	SipShowIM(SIPF_OFF);
	m480_modOn[m480_kKeyboard] = falseblnr;
	if (m480_macPress) {
		m480_macPress = falseblnr;
		SetCurMouseButton(falseblnr);
	}

	M480_BuildList();
	m480_listTop = 0;
	m480_listHot = -1;
	m480_listResult = -1;
	m480_listMode = trueblnr;
	M480_DrawList();

	while (m480_listMode && GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	m480_listMode = falseblnr;

	if ((m480_listResult >= 0) && (m480_listResult < m480_nFiles)) {
		_tcscpy(path, m480_files[m480_listResult].path);
		IsOk = trueblnr;
	}

	M480_DrawMac(0, 0, vMacScreenHeight, vMacScreenWidth);
	M480_DrawBar();

	return IsOk;
}

LOCALPROC M480_AskQuit(void)
{
	int r = IDYES;

	if (AnyDiskInserted()) {
		M480_ReleaseMods();
		MyBeginDialog();
		r = MessageBox(MainWnd,
			TEXT("맥에서 먼저 [Special] 메뉴의 [Shut Down]으로")
			TEXT(" 끄는 것이 안전합니다.\n\n그래도 지금 끌까요?"),
			TEXT("M480Mac 종료"),
			MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
		MyEndDialog();
		M480_FullScreen();
		(void) InvalidateRect(MainWnd, NULL, FALSE);
	}
	if (IDYES == r) {
		ForceMacOff = trueblnr;
	}
}

LOCALPROC M480_Paint(void)
{
	if (m480_listMode) {
		M480_DrawList();
	} else {
		M480_DrawMac(0, 0, vMacScreenHeight, vMacScreenWidth);
		M480_DrawBar();
	}
}

/* returns true when the message was handled here */
LOCALFUNC blnr M480_Touch(UINT msg, LPARAM lparam)
{
	int x = (short)LOWORD(lparam);
	int y = (short)HIWORD(lparam);

	if (m480_listMode) {
		if (WM_LBUTTONDOWN == msg) {
			M480_ListDown(x, y);
		} else if (WM_LBUTTONUP == msg) {
			M480_ListUp(x, y);
		}
		return trueblnr;
	}

	switch (msg) {
		case WM_LBUTTONDOWN:
			SetCapture(MainWnd);
			if (y - m480_yoff >= M480_BarY) {
				m480_barPress = trueblnr;
				m480_barHot = M480_ButtonAt(x - m480_xoff, y - m480_yoff);
				M480_DrawBar();
			} else {
				m480_macPress = trueblnr;
				M480_MacMouse(x, y);
				SetCurMouseButton(trueblnr);
			}
			break;
		case WM_MOUSEMOVE:
			if (m480_macPress) {
				M480_MacMouse(x, y);
			}
			break;
		case WM_LBUTTONUP:
			ReleaseCapture();
			if (m480_macPress) {
				m480_macPress = falseblnr;
				M480_MacMouse(x, y);
				SetCurMouseButton(falseblnr);
			} else if (m480_barPress) {
				int hot = m480_barHot;

				m480_barPress = falseblnr;
				m480_barHot = -1;
				if ((hot >= 0)
					&& (hot == M480_ButtonAt(x - m480_xoff, y - m480_yoff)))
				{
					M480_BarAction(hot);
				}
				M480_DrawBar();
			}
			break;
	}
	return trueblnr;
}

LOCALFUNC WPARAM M480_RealVK(WPARAM vk)
{
	if (VK_PROCESSKEY == vk) {
		vk = ImmGetVirtualKey(MainWnd);
	}
	if ((vk >= 'a') && (vk <= 'z')) {
		vk -= 32;
	}
	return vk & 0xFF;
}
