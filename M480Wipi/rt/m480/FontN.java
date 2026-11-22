package m480;
public final class FontN {
    public static native int height(int fid);
    public static native int ascent(int fid);
    public static native int strWidth(int fid, String s, int off, int len);
    public static native int charsWidth(int fid, char[] c, int off, int len);
    public static native int pixelSize(int size);   /* MIDP/WIPI size constant -> px */
}
