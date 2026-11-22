package org.kwis.msp.media;
public class Player {
    public static final int PLAY_ON = 1, PLAY_OFF = 0;
    private static int vol = 100;
    public static boolean play(BaseClip c, boolean repeat) {
        if (c == null || c.snd == 0) return false;
        if (c instanceof Clip) m480.Snd.volume(c.snd, ((Clip) c).volume * vol / 100);
        m480.Snd.play(c.snd, repeat ? -1 : 1);
        return true;
    }
    public static boolean play(Clip c, boolean repeat) { return play((BaseClip) c, repeat); }
    public static boolean play(BaseClip c) { return play(c, false); }
    public static boolean stop(BaseClip c) { if (c != null && c.snd != 0) m480.Snd.stop(c.snd); return true; }
    public static boolean stop(Clip c) { return stop((BaseClip) c); }
    public static boolean pause(BaseClip c) { if (c != null && c.snd != 0) m480.Snd.pause(c.snd); return true; }
    public static boolean resume(BaseClip c) { if (c != null && c.snd != 0) m480.Snd.resume(c.snd); return true; }
    public static boolean resume(Clip c) { return resume((BaseClip) c); }
    public static boolean record(BaseClip c) { return false; }
    public static boolean isPlaying(BaseClip c) { return c != null && c.snd != 0 && m480.Snd.playing(c.snd); }
    public static void setVolume(int v) { vol = v; }
    public static int getVolume() { return vol; }
    public static int getState(BaseClip c) { return isPlaying(c) ? PLAY_ON : PLAY_OFF; }
}
