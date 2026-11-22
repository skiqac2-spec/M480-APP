package javax.microedition.lcdui;
public class Gauge extends Item {
    public static final int INDEFINITE = -1, CONTINUOUS_IDLE = 0, INCREMENTAL_IDLE = 1, CONTINUOUS_RUNNING = 2, INCREMENTAL_UPDATING = 3;
    boolean inter; int max, val;
    public Gauge(String l, boolean i, int m, int v) { super(l); inter = i; max = m; val = v; }
    public void setValue(int v) { val = v; changed(); }
    public int getValue() { return val; }
    public void setMaxValue(int m) { max = m; changed(); }
    public int getMaxValue() { return max; }
    public boolean isInteractive() { return inter; }
    int bodyHeight(Font f, int w) { return 10; }
    void paintBody(Graphics g, int x, int y, int w, boolean focused, Font f) {
        g.setColor(0x808080); g.drawRect(x, y, w - 1, 8);
        if (max > 0) { g.setColor(0x3070D0); g.fillRect(x + 1, y + 1, (w - 2) * Math.min(val, max) / max, 7); }
    }
    boolean key(int id) {
        if (!inter) return false;
        if (id == m480.Keys.LEFT || id == 4) { if (val > 0) val--; notifyStateChanged(); return true; }
        if (id == m480.Keys.RIGHT || id == 6) { if (val < max) val++; notifyStateChanged(); return true; }
        return false;
    }
}
