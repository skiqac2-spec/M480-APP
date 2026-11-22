package javax.microedition.lcdui;
import java.io.IOException;
import java.io.InputStream;
public class Image {
    int surf;
    int w, h;
    boolean mutable;
    Image(int s, boolean m) {
        if (s == 0) throw new IllegalArgumentException("image decode failed");
        surf = s; w = m480.Img.width(s); h = m480.Img.height(s); mutable = m;
    }
    public static Image createImage(int w, int h) {
        if (w <= 0 || h <= 0) throw new IllegalArgumentException();
        return new Image(m480.Img.create(w, h), true);
    }
    public static Image createImage(String name) throws IOException {
        if (name == null) throw new NullPointerException();
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
    public static Image createImage(InputStream in) throws IOException {
        if (in == null) throw new NullPointerException();
        byte[] b = m480.Img.readAll(in);
        int s = m480.Img.decode(b, 0, b.length);
        if (s == 0) throw new IOException("bad image");
        return new Image(s, false);
    }
    public static Image createImage(Image src) {
        if (!src.mutable) return src;
        return new Image(m480.Img.region(src.surf, 0, 0, src.w, src.h, 0), false);
    }
    public static Image createImage(Image src, int x, int y, int w, int h, int tr) {
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > src.w || y + h > src.h) throw new IllegalArgumentException();
        return new Image(m480.Img.region(src.surf, x, y, w, h, tr), false);
    }
    public static Image createRGBImage(int[] rgb, int w, int h, boolean alpha) {
        if (rgb == null) throw new NullPointerException();
        if (w <= 0 || h <= 0 || rgb.length < w * h) throw new IllegalArgumentException();
        return new Image(m480.Img.fromRGB(rgb, w, h, alpha), false);
    }
    public Graphics getGraphics() {
        if (!mutable) throw new IllegalStateException("immutable");
        return new Graphics(surf, w, h, this);
    }
    public int getWidth() { return w; }
    public int getHeight() { return h; }
    public boolean isMutable() { return mutable; }
    public void getRGB(int[] rgb, int off, int scan, int x, int y, int w, int h) {
        if (x < 0 || y < 0 || x + w > this.w || y + h > this.h) throw new IllegalArgumentException();
        m480.Img.getRGB(surf, rgb, off, scan, x, y, w, h);
    }
}
