package com.skt.m;
public class BackLight {
    static int color;
    public static void on(int ms) { m480.Sys.backlight(1); }
    public static void off() { m480.Sys.backlight(0); }
    public static int getColor() { return color; }
    public static void setColor(int c) { color = c; }
}
