package javax.microedition.lcdui.game;
import javax.microedition.lcdui.*;
public abstract class GameCanvas extends Canvas {
    public static final int UP_PRESSED = 1 << 1, DOWN_PRESSED = 1 << 6, LEFT_PRESSED = 1 << 2, RIGHT_PRESSED = 1 << 5,
        FIRE_PRESSED = 1 << 8, GAME_A_PRESSED = 1 << 9, GAME_B_PRESSED = 1 << 10, GAME_C_PRESSED = 1 << 11, GAME_D_PRESSED = 1 << 12;
    private Image buf;
    private boolean suppress;
    private int states, latched;
    protected GameCanvas(boolean suppressKeys) {
        suppress = suppressKeys;
        buf = Image.createImage(m480.Sys.width(), m480.Sys.height());
    }
    protected Graphics getGraphics() { return buf.getGraphics(); }
    public int getKeyStates() { int s = states | latched; latched = 0; return s; }
    public void paint(Graphics g) { g.drawImage(buf, 0, 0, Graphics.TOP | Graphics.LEFT); }
    public void flushGraphics() { m480.Gfx.present(buf, 0, 0); }
    public void flushGraphics(int x, int y, int w, int h) { flushGraphics(); }
    public void keyEvent(int type, int id) {
        int code = m480.Keys.midp(id);
        int a = m480.Keys.gameAction(code);
        if (a != 0) {
            if (type == m480.Sys.EV_KEY_PRESS) { states |= 1 << a; latched |= 1 << a; }
            else if (type == m480.Sys.EV_KEY_RELEASE) states &= ~(1 << a);
        }
        if (!suppress || a == 0) super.keyEvent(type, id);
        else if (id == m480.Keys.SOFT1 || id == m480.Keys.SOFT2) super.keyEvent(type, id);
    }
}
