#ifndef M480_IME_H
#define M480_IME_H
/* tiny Japanese input: romaji -> hiragana as you type; kanji candidates come from
   Google's input tools service (see main.c) */
#ifdef _WIN32
#include <windows.h>
#else
typedef unsigned short WCHAR;
#endif

/* converts the romaji letters in buf[from..len) to hiragana (as far as they are
   complete: "sak" -> "さk"). returns the new length. buf must have room for len+1. */
int ime_romaji(WCHAR *buf, int len, int from);
/* typing finished: a trailing "n" becomes ん. returns the new length */
int ime_finish(WCHAR *buf, int len);
/* hiragana -> katakana in place */
void ime_katakana(WCHAR *s, int n);
/* 1 for hiragana, katakana, ー and pending romaji letters */
int ime_is_comp(WCHAR c);
#endif
