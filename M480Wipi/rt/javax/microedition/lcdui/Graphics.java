package javax.microedition.lcdui;
public class Graphics {
    /* field order is shared with javax.microedition.lcdui.Graphics (natives use slot indices) */
    int surf; int tx, ty; int cx, cy, cw, ch; int color; int alpha = 255; int xor; int stroke; Font font; Object img; int gw, gh;
    public static final int HCENTER = 1, VCENTER = 2, LEFT = 4, RIGHT = 8, TOP = 16, BOTTOM = 32, BASELINE = 64;
    public static final int SOLID = 0, DOTTED = 1;
    Graphics(int s, int w, int h, Object image) { surf = s; gw = w; gh = h; img = image; reset(); }
    
    public void reset() { tx = ty = 0; cx = cy = 0; cw = gw; ch = gh; color = 0; alpha = 255; xor = 0; stroke = 0; font = Font.getDefaultFont(); }
    public void translate(int x, int y) { tx += x; ty += y; }
    public int getTranslateX() { return tx; }
    public int getTranslateY() { return ty; }
    public void setClip(int x, int y, int w, int h) {
        int x0 = x + tx, y0 = y + ty, x1 = x0 + w, y1 = y0 + h;
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > gw) x1 = gw; if (y1 > gh) y1 = gh;
        cx = x0; cy = y0; cw = x1 > x0 ? x1 - x0 : 0; ch = y1 > y0 ? y1 - y0 : 0;
    }
    public void clipRect(int x, int y, int w, int h) {
        int x0 = x + tx, y0 = y + ty, x1 = x0 + w, y1 = y0 + h;
        if (x0 < cx) x0 = cx; if (y0 < cy) y0 = cy; if (x1 > cx + cw) x1 = cx + cw; if (y1 > cy + ch) y1 = cy + ch;
        cx = x0; cy = y0; cw = x1 > x0 ? x1 - x0 : 0; ch = y1 > y0 ? y1 - y0 : 0;
    }
    public int getClipX() { return cx - tx; }
    public int getClipY() { return cy - ty; }
    public int getClipWidth() { return cw; }
    public int getClipHeight() { return ch; }
    public void setColor(int rgb) { color = rgb & 0xFFFFFF; }
    public void setColor(int r, int g, int b) { color = ((r & 255) << 16) | ((g & 255) << 8) | (b & 255); }
    public int getColor() { return color; }
    public int getRedComponent() { return (color >> 16) & 255; }
    public int getGreenComponent() { return (color >> 8) & 255; }
    public int getBlueComponent() { return color & 255; }
    public int getGrayScale() { return (getRedComponent() + getGreenComponent() + getBlueComponent()) / 3; }
    public void setGrayScale(int v) { setColor(v, v, v); }
    public void setFont(Font f) { font = f == null ? Font.getDefaultFont() : f; }
    public Font getFont() { return font; }
    public void setStrokeStyle(int s) { stroke = s; }
    public int getStrokeStyle() { return stroke; }
    public native void fillRect(int x, int y, int w, int h);
    public native void drawRect(int x, int y, int w, int h);
    public native void drawLine(int x1, int y1, int x2, int y2);
    public native void fillRoundRect(int x, int y, int w, int h, int aw, int ah);
    public native void drawRoundRect(int x, int y, int w, int h, int aw, int ah);
    public native void fillArc(int x, int y, int w, int h, int s, int a);
    public native void drawArc(int x, int y, int w, int h, int s, int a);
    public native void fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3);
    public native void fillPolygon(int[] xs, int[] ys);
    public native void drawPolygon(int[] xs, int[] ys);
    public void fillPolygon(int[] xs, int[] ys, int n) { fillPolygon(trim(xs, n), trim(ys, n)); }
    public void drawPolygon(int[] xs, int[] ys, int n) { drawPolygon(trim(xs, n), trim(ys, n)); }
    private static int[] trim(int[] a, int n) { if (a.length == n) return a; int[] b = new int[n]; System.arraycopy(a, 0, b, 0, n); return b; }
    public native void drawString(String s, int x, int y, int anchor);
    public native void drawSubstring(String s, int off, int len, int x, int y, int anchor);
    public native void drawChars(char[] c, int off, int len, int x, int y, int anchor);
    public void drawChar(char c, int x, int y, int anchor) { char[] a = { c }; drawChars(a, 0, 1, x, y, anchor); }
    native void drawImage0(Object img, int x, int y, int anchor);
    native void drawRegion0(Object img, int sx, int sy, int w, int h, int tr, int x, int y, int anchor);
    public void drawImage(Image img, int x, int y, int anchor) { if (img == null) throw new NullPointerException(); drawImage0(img, x, y, anchor); }
    public void drawRegion(Image img, int sx, int sy, int w, int h, int tr, int x, int y, int anchor) {
        if (img == null) throw new NullPointerException();
        if (sx < 0 || sy < 0 || w < 0 || h < 0 || sx + w > img.w || sy + h > img.h) throw new IllegalArgumentException();
        drawRegion0(img, sx, sy, w, h, tr, x, y, anchor);
    }
    public int getDisplayColor(int c) { int r = (c >> 16) & 0xF8, g = (c >> 8) & 0xFC, b = c & 0xF8; return (r << 16) | (g << 8) | b; }
    native void copyArea(int sx, int sy, int w, int h, int dx, int dy);
    public native void drawRGB(int[] rgb, int off, int scan, int x, int y, int w, int h, boolean alpha);
    public native void getRGBPixels(int x, int y, int w, int h, int[] rgb, int off, int scan);
    public void copyArea(int sx, int sy, int w, int h, int dx, int dy, int anchor) {
        if ((anchor & HCENTER) != 0) dx -= w / 2; else if ((anchor & RIGHT) != 0) dx -= w;
        if ((anchor & VCENTER) != 0) dy -= h / 2; else if ((anchor & BOTTOM) != 0) dy -= h;
        copyArea(sx, sy, w, h, dx, dy);
    }
}
