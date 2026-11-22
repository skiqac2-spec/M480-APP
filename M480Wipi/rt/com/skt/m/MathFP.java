package com.skt.m;
/* 16.16 fixed point */
public class MathFP {
    public static final long PI = 205887L, E = 178145L;
    public static long toLong(long a) { return a >> 16; }
    public static long parseFP(long a) { return a << 16; }
    public static long add(long a, long b) { return a + b; }
    public static long sub(long a, long b) { return a - b; }
    public static long multiply(long a, long b) { return (a * b) >> 16; }
    public static long divide(long a, long b) { return (a << 16) / b; }
    public static long abs(long a) { return a < 0 ? -a : a; }
    public static long max(long a, long b) { return a > b ? a : b; }
    public static long min(long a, long b) { return a < b ? a : b; }
    public static long round(long a) { return (a + 32768) & ~0xFFFFL; }
    private static double d(long a) { return a / 65536.0; }
    private static long f(double v) { return (long) (v * 65536.0); }
    public static long sqrt(long a) { return f(Math.sqrt(d(a))); }
    public static long sin(long a) { return f(Math.sin(d(a))); }
    public static long cos(long a) { return f(Math.cos(d(a))); }
    public static long tan(long a) { return f(Math.tan(d(a))); }
    public static long asin(long a) { return f(Math.asin(d(a))); }
    public static long acos(long a) { return f(Math.acos(d(a))); }
    public static long atan(long a) { return f(Math.atan(d(a))); }
    public static long exp(long a) { return f(Math.exp(d(a))); }
    public static long log(long a) { return f(Math.log(d(a))); }
    public static long pow(long a, long b) { return f(Math.pow(d(a), d(b))); }
}
