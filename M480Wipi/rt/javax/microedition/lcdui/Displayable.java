package javax.microedition.lcdui;
import java.util.Vector;
public abstract class Displayable implements m480.Screen {
    Vector commands = new Vector();
    CommandListener listener;
    String title;
    Ticker ticker;
    boolean visible;
    Graphics screenG;
    public void addCommand(Command c) { if (c == null) throw new NullPointerException(); if (!commands.contains(c)) { commands.addElement(c); repaintSoft(); } }
    public void removeCommand(Command c) { commands.removeElement(c); repaintSoft(); }
    public void setCommandListener(CommandListener l) { listener = l; }
    public boolean isShown() { return visible; }
    public int getWidth() { return m480.Sys.width(); }
    public int getHeight() { return m480.Sys.height(); }
    public String getTitle() { return title; }
    public void setTitle(String t) { title = t; repaintSoft(); }
    public Ticker getTicker() { return ticker; }
    public void setTicker(Ticker t) { ticker = t; }
    protected void sizeChanged(int w, int h) {}
    void repaintSoft() { if (visible) m480.Sys.repaint(); }
    Graphics screenGraphics() {
        if (screenG == null) screenG = new Graphics(1, m480.Sys.width(), m480.Sys.height(), null);
        screenG.reset();
        return screenG;
    }
    /* soft keys trigger commands: SOFT1 = first non-back command, SOFT2 = back/exit/cancel or second */
    Command softCommand(int which) {
        int n = commands.size();
        if (n == 0) return null;
        Command neg = null, pos = null;
        for (int i = 0; i < n; i++) {
            Command c = (Command) commands.elementAt(i);
            int t = c.getCommandType();
            if ((t == Command.BACK || t == Command.EXIT || t == Command.CANCEL || t == Command.STOP) && neg == null) neg = c;
            else if (pos == null) pos = c;
        }
        if (n == 1) return which == 1 ? (Command) commands.elementAt(0) : (Command) commands.elementAt(0);
        if (which == 1) return pos != null ? pos : (Command) commands.elementAt(0);
        if (neg != null) return neg;
        for (int i = 0; i < n; i++) if (commands.elementAt(i) != pos) return (Command) commands.elementAt(i);
        return null;
    }
    boolean fireSoft(int id) {
        if (listener == null) return false;
        Command c = null;
        if (id == m480.Keys.SOFT1) c = softCommand(1);
        else if (id == m480.Keys.SOFT2) c = softCommand(2);
        if (c == null) return false;
        listener.commandAction(c, this);
        return true;
    }
    void drawSoftBar(Graphics g) {
        Command a = softCommand(1), b = softCommand(2);
        if (a == null && b == null) return;
        int W = getWidth(), H = getHeight();
        Font f = Font.getFont(0, 0, Font.SIZE_SMALL);
        int fh = f.getHeight() + 4;
        g.setFont(f);
        g.setColor(0x202840); g.fillRect(0, H - fh, W, fh);
        g.setColor(0xFFFFFF);
        if (a != null) g.drawString(a.getLabel(), 3, H - 2, Graphics.LEFT | Graphics.BOTTOM);
        if (b != null && b != a) g.drawString(b.getLabel(), W - 3, H - 2, Graphics.RIGHT | Graphics.BOTTOM);
    }
    public void shown() { visible = true; }
    public void hidden() { visible = false; }
    public void pointerEvent(int type, int x, int y) {}
}
