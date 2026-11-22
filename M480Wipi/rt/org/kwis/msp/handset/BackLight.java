package org.kwis.msp.handset;
public class BackLight {
    public static void on(int ms) { m480.Sys.backlight(1); }
    public static void alwaysOn() { m480.Sys.backlight(1); }
    public static void off() { m480.Sys.backlight(0); }
    public static void on() { m480.Sys.backlight(1); }
}
