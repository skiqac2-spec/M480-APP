package org.kwis.msp.media;
public class Vibrator {
    public static void on(int level, int ms) { m480.Sys.vibrate(ms); }
    public static void on(int ms) { m480.Sys.vibrate(ms); }
    public static void off() { m480.Sys.vibrate(0); }
}
