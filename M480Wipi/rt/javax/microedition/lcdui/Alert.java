package javax.microedition.lcdui;
public class Alert extends Screen {
    public static final int FOREVER = -2;
    public static final Command DISMISS_COMMAND = new Command("OK", Command.OK, 0);
    String text; Image img; AlertType type; int timeout = 2000; Displayable next; Gauge ind;
    public Alert(String t) { this(t, null, null, null); }
    public Alert(String t, String s, Image i, AlertType ty) { title = t; text = s; img = i; type = ty; }
    public int getDefaultTimeout() { return 2000; }
    public int getTimeout() { return timeout; }
    public void setTimeout(int t) { timeout = t; }
    public AlertType getType() { return type; }
    public void setType(AlertType t) { type = t; }
    public String getString() { return text; }
    public void setString(String s) { text = s; repaintSoft(); }
    public Image getImage() { return img; }
    public void setImage(Image i) { img = i; }
    public void setIndicator(Gauge g) { ind = g; }
    public Gauge getIndicator() { return ind; }
    int count() { return 1; }
    int itemHeight(int i, Font f, int w) { return wrap(text, f, w).size() * f.getHeight() + (img != null ? img.getHeight() + 4 : 0) + 8; }
    void paintItem(Graphics g, int i, int x, int y, int w, boolean focused, Font f) {
        if (img != null) { g.drawImage(img, x + w / 2, y + 2, Graphics.HCENTER | Graphics.TOP); y += img.getHeight() + 4; }
        java.util.Vector v = wrap(text, f, w);
        g.setColor(0);
        for (int k = 0; k < v.size(); k++) g.drawString((String) v.elementAt(k), x, y + 4 + k * f.getHeight(), Graphics.LEFT | Graphics.TOP);
    }
    private Thread timer;
    public void shown() {
        super.shown();
        if (commands.size() == 0) addCommand(DISMISS_COMMAND);
        if (timeout != FOREVER && timeout > 0) {
            final Alert self = this;
            timer = new Thread() { public void run() { try { Thread.sleep(timeout); } catch (InterruptedException e) {} if (self.visible) m480.Sys.callSerially(new Runnable() { public void run() { self.dismiss(); } }); } };
            timer.start();
        }
    }
    void dismiss() { if (next != null) m480.Sys.setScreen(next); }
    boolean fireSoft(int id) {
        if (listener != null && !(commands.size() == 1 && commands.elementAt(0) == DISMISS_COMMAND)) return super.fireSoft(id);
        dismiss();
        return true;
    }
    boolean selectItem(int i) { dismiss(); return true; }
}
