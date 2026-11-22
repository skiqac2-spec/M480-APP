package com.skt.m;
import javax.microedition.lcdui.*;
public class Graphics2D {
    public static final int DRAW_COPY = 0, DRAW_OR = 1, DRAW_AND = 2, DRAW_XOR = 3;
    Graphics g;
    public Graphics2D(Graphics g) { this.g = g; }
    public static Graphics2D getGraphics2D(Graphics g) { return new Graphics2D(g); }
    public int getPixel(int x, int y) { int[] p = new int[1]; g.getRGBPixels(x, y, 1, 1, p, 0, 1); return p[0] & 0xFFFFFF; }
    public void setPixel(int x, int y, int c) { int o = g.getColor(); g.setColor(c); g.fillRect(x, y, 1, 1); g.setColor(o); }
    public boolean getPixelMask(int x, int y) { return true; }
    public void setPixelMask(int x, int y, boolean b) {}
    public void invertRect(int x, int y, int w, int h) {
        int[] p = new int[w * h];
        g.getRGBPixels(x, y, w, h, p, 0, w);
        for (int i = 0; i < p.length; i++) p[i] = ~p[i] | 0xFF000000;
        g.drawRGB(p, 0, w, x, y, w, h, false);
    }
    public void drawImage(int x, int y, Image img, int sx, int sy, int w, int h, int mode) { g.drawRegion(img, sx, sy, w, h, 0, x, y, Graphics.TOP | Graphics.LEFT); }
    public void drawImage(int x, int y, Image img, int mode) { g.drawImage(img, x, y, Graphics.TOP | Graphics.LEFT); }
}
