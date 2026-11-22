package javax.microedition.lcdui;
public abstract class Canvas extends Displayable {
    public static final int UP = 1, DOWN = 6, LEFT = 2, RIGHT = 5, FIRE = 8, GAME_A = 9, GAME_B = 10, GAME_C = 11, GAME_D = 12;
    public static final int KEY_NUM0 = 48, KEY_NUM1 = 49, KEY_NUM2 = 50, KEY_NUM3 = 51, KEY_NUM4 = 52, KEY_NUM5 = 53,
        KEY_NUM6 = 54, KEY_NUM7 = 55, KEY_NUM8 = 56, KEY_NUM9 = 57, KEY_STAR = 42, KEY_POUND = 35;
    boolean full;
    protected Canvas() {}
    protected abstract void paint(Graphics g);
    protected void keyPressed(int k) {}
    protected void keyReleased(int k) {}
    protected void keyRepeated(int k) {}
    protected void pointerPressed(int x, int y) {}
    protected void pointerReleased(int x, int y) {}
    protected void pointerDragged(int x, int y) {}
    protected void showNotify() {}
    protected void hideNotify() {}
    public final void repaint() { if (visible) m480.Sys.repaint(); }
    public final void repaint(int x, int y, int w, int h) { repaint(); }
    public final void serviceRepaints() { m480.Sys.serviceRepaints(); }
    public int getGameAction(int code) { return m480.Keys.gameAction(code); }
    public int getKeyCode(int action) { return m480.Keys.keyForAction(action); }
    public String getKeyName(int code) { String s = m480.Keys.name(code, false); return s != null ? s : String.valueOf(code); }
    public boolean hasPointerEvents() { return true; }
    public boolean hasPointerMotionEvents() { return true; }
    public boolean hasRepeatEvents() { return true; }
    public boolean isDoubleBuffered() { return true; }
    public void setFullScreenMode(boolean f) { full = f; repaint(); }
    public int getWidth() { return m480.Sys.width(); }
    public int getHeight() { return m480.Sys.height(); }
    public void paintScreen() {
        Graphics g = screenGraphics();
        paint(g);
    }
    public void keyEvent(int type, int id) {
        if (type == m480.Sys.EV_KEY_PRESS && (id == m480.Keys.SOFT1 || id == m480.Keys.SOFT2) && commands.size() > 0) {
            if (fireSoft(id)) return;
        }
        int code = m480.Keys.midp(id);
        if (code == 0) return;
        if (type == m480.Sys.EV_KEY_PRESS) keyPressed(code);
        else if (type == m480.Sys.EV_KEY_RELEASE) keyReleased(code);
        else keyRepeated(code);
    }
    public void pointerEvent(int type, int x, int y) {
        if (type == m480.Sys.EV_PTR_PRESS) pointerPressed(x, y);
        else if (type == m480.Sys.EV_PTR_RELEASE) pointerReleased(x, y);
        else pointerDragged(x, y);
    }
    public void shown() { visible = true; showNotify(); }
    public void hidden() { visible = false; hideNotify(); }
}
