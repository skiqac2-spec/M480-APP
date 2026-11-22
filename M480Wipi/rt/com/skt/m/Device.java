package com.skt.m;
public class Device {
    public static final int COLOR_BLACK = 0, COLOR_WHITE = 1;
    public static void beep(int freq, int ms) { m480.Snd.tone(69, ms, 80); }
    public static void setNAI(int n) {}
    public static void destroyApp(boolean b) { m480.Sys.quit(); }
    public static int getColorDepth() { return 16; }
    public static boolean isColor() { return true; }
    public static String getPhoneNumber() { return "01000000000"; }
    public static String getDeviceName() { return "SCH-M480"; }
    public static int getBatteryLevel() { return 3; }
    public static int getRadioLevel() { return 4; }
}
