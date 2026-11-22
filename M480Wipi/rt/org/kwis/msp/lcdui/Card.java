package org.kwis.msp.lcdui;
public class Card implements JletEventListener {
    Display display;
    int x, y, w, h;
    boolean transparent;
    boolean shown;
    public Card() { this(Display.getDefaultDisplay()); }
    public Card(int type) { this(Display.getDefaultDisplay()); }
    public Card(boolean transparent) { this(Display.getDefaultDisplay()); this.transparent = transparent; }
    public Card(int x, int y, int w, int h) { this(Display.getDefaultDisplay(), x, y, w, h); }
    public Card(Display d) { this(d, 0, 0, Display.screenW(), Display.screenH()); }
    public Card(Display d, int x, int y, int w, int h) { this(d, x, y, w, h, false); }
    public Card(Display d, int x, int y, int w, int h, boolean t) {
        display = d == null ? Display.getDefaultDisplay() : d;
        this.x = x; this.y = y; this.w = w; this.h = h; transparent = t;
    }
    public void paint(Graphics g) {}
    public boolean keyNotify(int type, int key) { return false; }
    public boolean pointerNotify(int type, int x, int y) { return false; }
    public void showNotify(boolean b) {}
    public void notifyEvent(int type, int p1, int p2) {}
    public int getWidth() { return w; }
    public int getHeight() { return h; }
    public int getX() { return x; }
    public int getY() { return y; }
    public void move(int nx, int ny) { x = nx; y = ny; repaint(); }
    public void resize(int nw, int nh) { w = nw; h = nh; repaint(); }
    public Display getDisplay() { return display; }
    public boolean isShown() { return shown; }
    public boolean isTransparent() { return transparent; }
    public void repaint() { if (display != null && display.contains(this)) m480.Sys.repaint(); }
    public void repaint(int x, int y, int w, int h) { repaint(); }
    public void serviceRepaints() { m480.Sys.serviceRepaints(); }
}
