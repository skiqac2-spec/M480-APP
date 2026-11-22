package javax.microedition.lcdui;
public abstract class CustomItem extends Item {
    protected CustomItem(String l) { super(l); }
    protected abstract int getMinContentWidth();
    protected abstract int getMinContentHeight();
    protected abstract int getPrefContentWidth(int h);
    protected abstract int getPrefContentHeight(int w);
    protected abstract void paint(Graphics g, int w, int h);
    protected void keyPressed(int k) {}
    protected void repaint() { changed(); }
    protected void invalidate() { changed(); }
    int bodyHeight(Font f, int w) { return getPrefContentHeight(w); }
    void paintBody(Graphics g, int x, int y, int w, boolean focused, Font f) { g.translate(x, y); paint(g, w, getPrefContentHeight(w)); g.translate(-x, -y); }
}
