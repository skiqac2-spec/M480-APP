/* runtime pieces FFmpeg expects from a C library that Windows CE lacks */
#include <windows.h>
#include <math.h>

int ce_errno;

long ce_clock(void) { return (long)GetTickCount(); }

double copysign(double x, double y) { return _copysign(x, y); }
double exp2(double x) { return pow(2.0, x); }
float exp2f(float x) { return (float)pow(2.0, x); }

/* ARMv5 has no atomic instructions: gcc calls these helpers; use the CE kernel's interlocked ops */
unsigned int ce_atomic_fetch_add_4(volatile void *p, unsigned int v, int order) __asm__("__atomic_fetch_add_4");
unsigned int ce_atomic_fetch_sub_4(volatile void *p, unsigned int v, int order) __asm__("__atomic_fetch_sub_4");
unsigned int ce_atomic_fetch_add_4(volatile void *p, unsigned int v, int order)
{
	(void)order;
	return (unsigned int)InterlockedExchangeAdd((LONG *)p, (LONG)v);
}
unsigned int ce_atomic_fetch_sub_4(volatile void *p, unsigned int v, int order)
{
	(void)order;
	return (unsigned int)InterlockedExchangeAdd((LONG *)p, -(LONG)v);
}

/* not exported by every Windows Mobile coredll */
float floorf(float x) { return (float)floor(x); }
