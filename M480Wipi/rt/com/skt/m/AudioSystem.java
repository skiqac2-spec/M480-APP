package com.skt.m;
public class AudioSystem {
    static int vol = 5;
    public static AudioClip getAudioClip(String type) { return new AudioClip(type); }
    public static int getMaxVolume(String type) { return 5; }
    public static int getVolume(String type) { return vol; }
    public static void setVolume(String type, int v) { vol = v < 0 ? 0 : v > 5 ? 5 : v; }
}
