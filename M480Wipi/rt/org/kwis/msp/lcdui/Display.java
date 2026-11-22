package org.kwis.msp.lcdui;
import java.util.Vector;
public class Display implements m480.Screen {
    private static Display def;
    private final Vector cards = new Vector();
    private Card docked;
    private final Vector listeners = new Vector();
    private Graphics screen;
    static int screenW() { return m480.Sys.width(); }
    static int screenH() { return m480.Sys.height(); }
    Display() {}
    public static synchronized Display getDefaultDisplay() { if (def == null) def = new Display(); return def; }
    public static Display getDisplay(String name) { return getDefaultDisplay(); }
    boolean contains(Card c) { return cards.contains(c) || c == docked; }
    public void pushCard(Card c) {
        if (c == null) throw new NullPointerException();
        Card old = getTopCard();
        cards.removeElement(c);
        cards.addElement(c);
        if (old != null && old != c) { old.shown = false; old.showNotify(false); }
        c.shown = true;
        m480.Sys.setScreen(this);
        c.showNotify(true);
        m480.Sys.repaint();
    }
    public Card popCard() {
        int n = cards.size();
        if (n == 0) return null;
        Card c = (Card) cards.elementAt(n - 1);
        cards.removeElementAt(n - 1);
        c.shown = false;
        c.showNotify(false);
        Card t = getTopCard();
        if (t != null) { t.shown = true; t.showNotify(true); }
        m480.Sys.repaint();
        return c;
    }
    public boolean removeCard(Card c) {
        if (c == getTopCard()) { popCard(); return true; }
        boolean r = cards.removeElement(c);
        if (r) { c.shown = false; m480.Sys.repaint(); }
        return r;
    }
    public void removeAllCards() { while (cards.size() > 0) popCard(); }
    public int countCard() { return cards.size(); }
    public Card getTopCard() { int n = cards.size(); return n == 0 ? null : (Card) cards.elementAt(n - 1); }
    public Card getCard(int i) { return (Card) cards.elementAt(i); }
    public Card getDockedCard() { return docked; }
    public void setDockedCard(Card c, int pos) { docked = c; m480.Sys.repaint(); }
    public void setDockedCard(Card c) { docked = c; m480.Sys.repaint(); }
    public void addJletEventListener(JletEventListener l) { if (!listeners.contains(l)) listeners.addElement(l); }
    public void removeJletEventListener(JletEventListener l) { listeners.removeElement(l); }
    public void grabKey(int key, JletEventListener l) {}
    public void ungrabKey(int key) {}
    public int getWidth() { return screenW(); }
    public int getHeight() { return screenH(); }
    public boolean isColor() { return true; }
    public int numColors() { return 65536; }
    public int getBitsPerPixel() { return 16; }
    public boolean isDoubleBuffered() { return true; }
    public boolean hasPointerEvents() { return true; }
    public boolean hasPointerMotionEvents() { return true; }
    public boolean hasRepeatEvents() { return true; }
    public void callSerially(Runnable r) { m480.Sys.callSerially(r); }
    public void callSerially(Runnable r, int delay) { m480.Sys.callSerially(r); }
    public void flush() { m480.Sys.flush(); }
    public void setCurrent(Card c) { pushCard(c); }
    public static String getKeyName(int code) { return m480.Keys.name(code, true); }
    public static int getGameAction(int code) {
        int id = m480.Keys.wipiToId(code);
        return id < 0 ? 0 : m480.Keys.gameAction(m480.Keys.midp(id));
    }
    public static int getKeyCode(int action) {
        int c = m480.Keys.keyForAction(action);
        int id = m480.Keys.midpToId(c);
        return m480.Keys.wipi(id);
    }
    void notifyListeners(int type, int p1, int p2) {
        for (int i = 0; i < listeners.size(); i++) ((JletEventListener) listeners.elementAt(i)).notifyEvent(type, p1, p2);
    }
    /* m480.Screen */
    public void paintScreen() {
        if (screen == null) screen = new Graphics(1, screenW(), screenH(), null);
        int n = cards.size(), first = 0;
        for (int i = n - 1; i >= 0; i--) {
            Card c = (Card) cards.elementAt(i);
            if (!c.transparent && c.x <= 0 && c.y <= 0 && c.x + c.w >= screenW() && c.y + c.h >= screenH()) { first = i; break; }
        }
        if (n == 0 || first == 0 && n > 0 && ((Card) cards.elementAt(0)).transparent) {
            screen.reset(); screen.setColor(0xFFFFFF); screen.fillRect(0, 0, screenW(), screenH());
        }
        for (int i = first; i < n; i++) paintCard((Card) cards.elementAt(i));
        if (docked != null) paintCard(docked);
    }
    private void paintCard(Card c) {
        Graphics g = screen;
        g.reset();
        g.translate(c.x, c.y);
        g.setClip(0, 0, c.w, c.h);
        c.paint(g);
    }
    public void keyEvent(int type, int id) {
        int code = m480.Keys.wipi(id);
        if (code == 0) return;
        for (int i = cards.size() - 1; i >= 0; i--) {
            Card c = (Card) cards.elementAt(i);
            if (c.keyNotify(type, code)) break;
        }
    }
    public void pointerEvent(int type, int x, int y) {
        int t = type - m480.Sys.EV_PTR_PRESS + 1;
        for (int i = cards.size() - 1; i >= 0; i--) {
            Card c = (Card) cards.elementAt(i);
            if (c.pointerNotify(t, x - c.x, y - c.y)) break;
        }
    }
    public void shown() {}
    public void hidden() {}
}
