package m480;
public final class Snd {
    public static native int load(byte[] d, int off, int len);
    public static native void play(int h, int loops);
    public static native void stop(int h);
    public static native void pause(int h);
    public static native void resume(int h);
    public static native void volume(int h, int v);
    public static native boolean playing(int h);
    public static native int position(int h);
    public static native int duration(int h);
    public static native void free(int h);
    public static native void tone(int note, int ms, int vol);
}
