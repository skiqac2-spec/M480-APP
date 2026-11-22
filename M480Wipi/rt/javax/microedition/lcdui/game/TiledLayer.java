package javax.microedition.lcdui.game;
import javax.microedition.lcdui.*;
public class TiledLayer extends Layer {
    int cols, rows, cw, ch; Image img; int tcols, ntiles; int[] cells; int[] anim = new int[4]; int nanim;
    public TiledLayer(int c, int r, Image i, int tw, int th) { super(c * tw, r * th); cols = c; rows = r; cells = new int[c * r]; setStaticTileSet(i, tw, th); }
    public void setStaticTileSet(Image i, int tw, int th) {
        if (tw <= 0 || th <= 0 || i.getWidth() % tw != 0 || i.getHeight() % th != 0) throw new IllegalArgumentException();
        int n = (i.getWidth() / tw) * (i.getHeight() / th);
        boolean keep = n >= ntiles && tw == cw && th == ch;
        img = i; cw = tw; ch = th; tcols = i.getWidth() / tw; ntiles = n; w = cols * cw; h = rows * ch;
        if (!keep) { for (int k = 0; k < cells.length; k++) cells[k] = 0; nanim = 0; }
    }
    public int createAnimatedTile(int t) {
        if (t < 0 || t > ntiles) throw new IndexOutOfBoundsException();
        if (nanim == anim.length) { int[] a = new int[anim.length * 2]; System.arraycopy(anim, 0, a, 0, nanim); anim = a; }
        anim[nanim++] = t; return -nanim;
    }
    public void setAnimatedTile(int a, int t) { anim[-a - 1] = t; }
    public int getAnimatedTile(int a) { return anim[-a - 1]; }
    public void setCell(int c, int r, int t) { if (c < 0 || c >= cols || r < 0 || r >= rows) throw new IndexOutOfBoundsException(); cells[r * cols + c] = t; }
    public int getCell(int c, int r) { return cells[r * cols + c]; }
    public void fillCells(int c, int r, int nc, int nr, int t) { for (int y = r; y < r + nr; y++) for (int x = c; x < c + nc; x++) setCell(x, y, t); }
    public final int getCellWidth() { return cw; }
    public final int getCellHeight() { return ch; }
    public final int getColumns() { return cols; }
    public final int getRows() { return rows; }
    public final void paint(Graphics g) {
        if (!visible) return;
        int clx = g.getClipX(), cly = g.getClipY(), clw = g.getClipWidth(), clh = g.getClipHeight();
        int c0 = Math.max(0, (clx - x) / cw), c1 = Math.min(cols - 1, (clx + clw - x) / cw);
        int r0 = Math.max(0, (cly - y) / ch), r1 = Math.min(rows - 1, (cly + clh - y) / ch);
        for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++) {
            int t = cells[r * cols + c];
            if (t < 0) t = anim[-t - 1];
            if (t == 0) continue;
            t--;
            g.drawRegion(img, (t % tcols) * cw, (t / tcols) * ch, cw, ch, 0, x + c * cw, y + r * ch, Graphics.TOP | Graphics.LEFT);
        }
    }
}
