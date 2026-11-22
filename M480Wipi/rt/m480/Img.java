package m480;
/* native image helpers shared by WIPI / MIDP / SKVM image classes (field 0 = surface handle) */
public final class Img {
    public static native int decode(byte[] b, int off, int len);
    public static native int create(int w, int h);
    public static native int region(int surf, int x, int y, int w, int h, int transform);
    public static native int fromRGB(int[] rgb, int w, int h, boolean alpha);
    public static native int width(int surf);
    public static native int height(int surf);
    public static native void getRGB(int surf, int[] rgb, int off, int scan, int x, int y, int w, int h);
    public static native void setTransparent(int surf, int rgb);
    public static native boolean hasAlpha(int surf);
    public static byte[] readAll(java.io.InputStream in) throws java.io.IOException {
        java.io.ByteArrayOutputStream bo = new java.io.ByteArrayOutputStream(4096);
        byte[] b = new byte[2048];
        int n;
        while ((n = in.read(b, 0, b.length)) > 0) bo.write(b, 0, n);
        return bo.toByteArray();
    }
    public static byte[] resource(String name) {
        if (name == null) return null;
        java.io.InputStream in = Img.class.getResourceAsStream(name.startsWith("/") ? name : "/" + name);
        if (in == null) return null;
        try { return readAll(in); } catch (java.io.IOException e) { return null; }
    }
}
