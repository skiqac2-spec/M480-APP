package javax.microedition.lcdui;
import java.util.Vector;
/* minimal high-level UI renderer shared by Form / List / TextBox / Alert */
public abstract class Screen extends Displayable {
    int focus;
    int scroll;
    abstract int count();
    abstract int itemHeight(int i, Font f, int w);
    abstract void paintItem(Graphics g, int i, int x, int y, int w, boolean focused, Font f);
    boolean selectItem(int i) { return false; }
    boolean keyItem(int i, int id) { return false; }
    public void paintScreen() {
        Graphics g = screenGraphics();
        int W = getWidth(), H = getHeight();
        Font f = Font.getDefaultFont();
        g.setColor(0xFFFFFF); g.fillRect(0, 0, W, H);
        int y = 0;
        if (title != null) {
            int th = f.getHeight() + 4;
            g.setColor(0x284878); g.fillRect(0, 0, W, th);
            g.setColor(0xFFFFFF); g.setFont(f); g.drawString(title, 3, 2, Graphics.LEFT | Graphics.TOP);
            y = th;
        }
        int bottom = H - (commands.size() > 0 ? Font.getFont(0, 0, Font.SIZE_SMALL).getHeight() + 4 : 0);
        int n = count();
        if (focus >= n) focus = n - 1;
        if (focus < 0) focus = 0;
        /* keep focus visible */
        int total = 0, fy = 0, fh = 0;
        for (int i = 0; i < n; i++) { int h = itemHeight(i, f, W - 4); if (i == focus) { fy = total; fh = h; } total += h; }
        int view = bottom - y;
        if (fy - scroll + fh > view) scroll = fy + fh - view;
        if (fy < scroll) scroll = fy;
        g.setClip(0, y, W, view);
        int yy = y - scroll;
        for (int i = 0; i < n; i++) {
            int h = itemHeight(i, f, W - 4);
            if (yy + h > y && yy < bottom) { g.setFont(f); paintItem(g, i, 2, yy, W - 4, i == focus, f); }
            yy += h;
        }
        g.setClip(0, 0, W, H);
        drawSoftBar(g);
    }
    public void keyEvent(int type, int id) {
        if (type == m480.Sys.EV_KEY_RELEASE) return;
        if (keyItem(focus, id)) { m480.Sys.repaint(); return; }
        int n = count();
        if (id == m480.Keys.UP || id == 2) { if (focus > 0) focus--; else focus = n - 1; }
        else if (id == m480.Keys.DOWN || id == 8) { if (focus < n - 1) focus++; else focus = 0; }
        else if (id == m480.Keys.FIRE || id == 5) { if (!selectItem(focus)) fireSoft(m480.Keys.SOFT1); }
        else if (id == m480.Keys.SOFT1 || id == m480.Keys.SOFT2) fireSoft(id);
        else if (id == m480.Keys.CLEAR) fireSoft(m480.Keys.SOFT2);
        m480.Sys.repaint();
    }
    static Vector wrap(String s, Font f, int w) {
        Vector v = new Vector();
        if (s == null) return v;
        int start = 0, n = s.length();
        while (start < n) {
            int end = start, lastSpace = -1;
            while (end < n && s.charAt(end) != '\n') {
                if (f.substringWidth(s, start, end - start + 1) > w && end > start) break;
                if (s.charAt(end) == ' ') lastSpace = end;
                end++;
            }
            if (end < n && s.charAt(end) != '\n' && lastSpace > start) end = lastSpace + 1;
            v.addElement(s.substring(start, end));
            start = end;
            if (start < n && s.charAt(start) == '\n') start++;
        }
        return v;
    }
}
