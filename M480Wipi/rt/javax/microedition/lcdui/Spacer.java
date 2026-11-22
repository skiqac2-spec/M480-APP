package javax.microedition.lcdui;
public class Spacer extends Item {
    int w, h;
    public Spacer(int w, int h) { super(null); this.w = w; this.h = h; }
    public void setMinimumSize(int w, int h) { this.w = w; this.h = h; }
    int bodyHeight(Font f, int ww) { return h; }
}
