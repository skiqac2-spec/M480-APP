package org.kwis.msp.media;
public class Volume {
    private static int v = 3;
    public static int get(int type) { return v; }
    public static void set(int type, int level) { v = level; }
    public static void setMute(int type, boolean m) {}
    public static boolean getMute(int type) { return false; }
}
