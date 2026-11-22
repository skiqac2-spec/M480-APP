package org.kwis.msp.lcdui;
import java.io.IOException;
public class Image {
    int surf;
    int w, h;
    boolean mutable;
    Image(int s, boolean m) {
        if (s == 0) throw new IllegalArgumentException("image decode failed");
        surf = s; w = m480.Img.width(s); h = m480.Img.height(s); mutable = m;
    }
    protected Image() {}
    public static Image createImage(int w, int h) {
        if (w <= 0 || h <= 0) throw new IllegalArgumentException();
        return new Image(m480.Img.create(w, h), true);
    }
    public static Image createImage(String name) throws IOException {
        byte[] b = m480.Img.resource(name);
        if (b == null) throw new IOException(name);
        int s = m480.Img.decode(b, 0, b.length);
        if (s == 0) throw new IOException("bad image " + name);
        return new Image(s, false);
    }
    public static Image createImage(byte[] b, int off, int len) {
        int s = m480.Img.decode(b, off, len);
        if (s == 0) throw new IllegalArgumentException("bad image data");
        return new Image(s, false);
    }
    public static Image createImage(Image src) { return new Image(m480.Img.region(src.surf, 0, 0, src.w, src.h, 0), false); }
    public static Image createImage(Image src, int x, int y, int w, int h) { return new Image(m480.Img.region(src.surf, x, y, w, h, 0), false); }
    public static Image loadImage(String name, ImageObserver o) throws IOException { return createImage(name); }
    public static void stopImage(ImageObserver o) {}
    public Image createSubImage(int x, int y, int w, int h, boolean mut) { return new Image(m480.Img.region(surf, x, y, w, h, 0), mut); }
    public Graphics getGraphics() {
        if (!mutable) throw new IllegalStateException("immutable");
        return new Graphics(surf, w, h, this);
    }
    public int getWidth() { return w; }
    public int getHeight() { return h; }
    public boolean isMutable() { return mutable; }
    public boolean isAnimated() { return false; }
    public void play(ImageObserver o) {}
    public void stop() {}
    public void setTransparentColor(int c) { m480.Img.setTransparent(surf, c); }
    public void getRGB(int[] rgb, int off, int scan, int x, int y, int w, int h) { m480.Img.getRGB(surf, rgb, off, scan, x, y, w, h); }
}
