package com.nokia.mid.ui;
public class DeviceControl {
    public static void setLights(int n, int l) { m480.Sys.backlight(l > 0 ? 1 : 0); }
    public static void flashLights(long d) {}
    public static void startVibra(int f, long d) { m480.Sys.vibrate((int) d); }
    public static void stopVibra() { m480.Sys.vibrate(0); }
}
