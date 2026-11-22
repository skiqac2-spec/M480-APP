package javax.microedition.lcdui.game;
import javax.microedition.lcdui.*;
public class Sprite extends Layer {
    public static final int TRANS_NONE = 0, TRANS_ROT90 = 5, TRANS_ROT180 = 3, TRANS_ROT270 = 6, TRANS_MIRROR = 2,
        TRANS_MIRROR_ROT90 = 7, TRANS_MIRROR_ROT180 = 1, TRANS_MIRROR_ROT270 = 4;
    Image img; int fw, fh, cols, nframes; int[] seq; int seqIdx; int refX, refY; int tr;
    int cx, cy, cw, ch;
    public Sprite(Image i) { this(i, i.getWidth(), i.getHeight()); }
    public Sprite(Image i, int fw, int fh) { super(fw, fh); setImage(i, fw, fh); }
    public Sprite(Sprite s) {
        super(s.w, s.h);
        img = s.img; fw = s.fw; fh = s.fh; cols = s.cols; nframes = s.nframes; seq = s.seq; seqIdx = s.seqIdx;
        refX = s.refX; refY = s.refY; tr = s.tr; x = s.x; y = s.y; visible = s.visible; cx = s.cx; cy = s.cy; cw = s.cw; ch = s.ch;
    }
    public void setImage(Image i, int fw, int fh) {
        if (fw <= 0 || fh <= 0 || i.getWidth() % fw != 0 || i.getHeight() % fh != 0) throw new IllegalArgumentException();
        img = i; this.fw = fw; this.fh = fh;
        cols = i.getWidth() / fw; int n = cols * (i.getHeight() / fh);
        if (n != nframes || seq == null) { nframes = n; seq = new int[n]; for (int k = 0; k < n; k++) seq[k] = k; seqIdx = 0; }
        cx = 0; cy = 0; cw = fw; ch = fh;
        updateSize();
    }
    private void updateSize() { if ((tr & 4) != 0) { w = fh; h = fw; } else { w = fw; h = fh; } }
    public void defineReferencePixel(int x, int y) { refX = x; refY = y; }
    public void setRefPixelPosition(int rx, int ry) { int[] p = refInDisplay(); x = rx - p[0]; y = ry - p[1]; }
    public int getRefPixelX() { return x + refInDisplay()[0]; }
    public int getRefPixelY() { return y + refInDisplay()[1]; }
    private int[] refInDisplay() {
        int rx = refX, ry = refY, ox, oy;
        switch (tr) {
        case TRANS_MIRROR: ox = fw - 1 - rx; oy = ry; break;
        case TRANS_MIRROR_ROT180: ox = rx; oy = fh - 1 - ry; break;
        case TRANS_ROT180: ox = fw - 1 - rx; oy = fh - 1 - ry; break;
        case TRANS_ROT90: ox = fh - 1 - ry; oy = rx; break;
        case TRANS_ROT270: ox = ry; oy = fw - 1 - rx; break;
        case TRANS_MIRROR_ROT90: ox = fh - 1 - ry; oy = fw - 1 - rx; break;
        case TRANS_MIRROR_ROT270: ox = ry; oy = rx; break;
        default: ox = rx; oy = ry;
        }
        return new int[] { ox, oy };
    }
    public void setFrame(int i) { if (i < 0 || i >= seq.length) throw new IndexOutOfBoundsException(); seqIdx = i; }
    public final int getFrame() { return seqIdx; }
    public int getRawFrameCount() { return nframes; }
    public int getFrameSequenceLength() { return seq.length; }
    public void nextFrame() { seqIdx = (seqIdx + 1) % seq.length; }
    public void prevFrame() { seqIdx = (seqIdx + seq.length - 1) % seq.length; }
    public void setFrameSequence(int[] s) {
        if (s == null) { seq = new int[nframes]; for (int k = 0; k < nframes; k++) seq[k] = k; }
        else { for (int k = 0; k < s.length; k++) if (s[k] < 0 || s[k] >= nframes) throw new ArrayIndexOutOfBoundsException(); seq = new int[s.length]; System.arraycopy(s, 0, seq, 0, s.length); }
        seqIdx = 0;
    }
    public void setTransform(int t) {
        int[] before = refInDisplay();
        tr = t; updateSize();
        int[] after = refInDisplay();
        x += before[0] - after[0]; y += before[1] - after[1];
    }
    public void defineCollisionRectangle(int x, int y, int w, int h) { cx = x; cy = y; cw = w; ch = h; }
    public final void paint(Graphics g) {
        if (!visible) return;
        int f = seq[seqIdx];
        g.drawRegion(img, (f % cols) * fw, (f / cols) * fh, fw, fh, tr, x, y, Graphics.TOP | Graphics.LEFT);
    }
    private int[] crect() {
        int rx = cx, ry = cy, rw = cw, rh = ch;
        switch (tr) {
        case TRANS_MIRROR: rx = fw - cx - cw; break;
        case TRANS_MIRROR_ROT180: ry = fh - cy - ch; break;
        case TRANS_ROT180: rx = fw - cx - cw; ry = fh - cy - ch; break;
        case TRANS_ROT90: rx = fh - cy - ch; ry = cx; rw = ch; rh = cw; break;
        case TRANS_ROT270: rx = cy; ry = fw - cx - cw; rw = ch; rh = cw; break;
        case TRANS_MIRROR_ROT90: rx = fh - cy - ch; ry = fw - cx - cw; rw = ch; rh = cw; break;
        case TRANS_MIRROR_ROT270: rx = cy; ry = cx; rw = ch; rh = cw; break;
        }
        return new int[] { x + rx, y + ry, rw, rh };
    }
    private static boolean hit(int[] a, int[] b) { return a[0] < b[0] + b[2] && b[0] < a[0] + a[2] && a[1] < b[1] + b[3] && b[1] < a[1] + a[3]; }
    public final boolean collidesWith(Sprite s, boolean pixel) {
        if (!visible || !s.visible) return false;
        return hit(crect(), s.crect());
    }
    public final boolean collidesWith(TiledLayer t, boolean pixel) {
        if (!visible || !t.visible) return false;
        int[] r = crect();
        int c0 = (r[0] - t.x) / t.cw, c1 = (r[0] + r[2] - 1 - t.x) / t.cw, r0 = (r[1] - t.y) / t.ch, r1 = (r[1] + r[3] - 1 - t.y) / t.ch;
        if (r[0] + r[2] <= t.x || r[1] + r[3] <= t.y || r[0] >= t.x + t.w || r[1] >= t.y + t.h) return false;
        for (int row = Math.max(0, r0); row <= Math.min(t.rows - 1, r1); row++)
            for (int col = Math.max(0, c0); col <= Math.min(t.cols - 1, c1); col++) if (t.getCell(col, row) != 0) return true;
        return false;
    }
    public final boolean collidesWith(Image i, int ix, int iy, boolean pixel) {
        if (!visible) return false;
        return hit(crect(), new int[] { ix, iy, i.getWidth(), i.getHeight() });
    }
}
