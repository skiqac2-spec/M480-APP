package javax.microedition.lcdui.game;
import javax.microedition.lcdui.Graphics;
public abstract class Layer {
    int x, y, w, h; boolean visible = true;
    Layer(int w, int h) { this.w = w; this.h = h; }
    public void setPosition(int x, int y) { this.x = x; this.y = y; }
    public void move(int dx, int dy) { x += dx; y += dy; }
    public final int getX() { return x; }
    public final int getY() { return y; }
    public final int getWidth() { return w; }
    public final int getHeight() { return h; }
    public void setVisible(boolean v) { visible = v; }
    public final boolean isVisible() { return visible; }
    public abstract void paint(Graphics g);
}
