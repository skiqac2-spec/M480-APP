package javax.microedition.lcdui;
import java.util.Vector;
public abstract class Item {
    public static final int LAYOUT_DEFAULT = 0, LAYOUT_LEFT = 1, LAYOUT_RIGHT = 2, LAYOUT_CENTER = 3, LAYOUT_TOP = 0x10, LAYOUT_BOTTOM = 0x20,
        LAYOUT_VCENTER = 0x30, LAYOUT_NEWLINE_BEFORE = 0x100, LAYOUT_NEWLINE_AFTER = 0x200, LAYOUT_SHRINK = 0x400, LAYOUT_EXPAND = 0x800,
        LAYOUT_VSHRINK = 0x1000, LAYOUT_VEXPAND = 0x2000, LAYOUT_2 = 0x4000, PLAIN = 0, HYPERLINK = 1, BUTTON = 2;
    String label;
    int layout;
    Form owner;
    Vector cmds = new Vector();
    ItemCommandListener icl;
    Command defCmd;
    Item(String l) { label = l; }
    public String getLabel() { return label; }
    public void setLabel(String l) { label = l; changed(); }
    public int getLayout() { return layout; }
    public void setLayout(int l) { layout = l; }
    public void addCommand(Command c) { cmds.addElement(c); }
    public void removeCommand(Command c) { cmds.removeElement(c); }
    public void setItemCommandListener(ItemCommandListener l) { icl = l; }
    public void setDefaultCommand(Command c) { defCmd = c; if (c != null && !cmds.contains(c)) cmds.addElement(c); }
    public void setPreferredSize(int w, int h) {}
    public int getPreferredWidth() { return 0; }
    public int getPreferredHeight() { return 0; }
    public int getMinimumWidth() { return 0; }
    public int getMinimumHeight() { return 0; }
    public void notifyStateChanged() { if (owner != null && owner.isl != null) owner.isl.itemStateChanged(this); }
    void changed() { if (owner != null) m480.Sys.repaint(); }
    int height(Font f, int w) {
        int h = label != null ? Screen.wrap(label, f, w).size() * f.getHeight() : 0;
        return h + bodyHeight(f, w) + 4;
    }
    int bodyHeight(Font f, int w) { return 0; }
    void paint(Graphics g, int x, int y, int w, boolean focused, Font f) {
        if (focused) { g.setColor(0xD8E4FF); g.fillRect(x - 1, y, w + 2, height(f, w)); }
        g.setColor(0);
        int yy = y + 2;
        if (label != null) {
            Vector v = Screen.wrap(label, f, w);
            for (int i = 0; i < v.size(); i++) { g.drawString((String) v.elementAt(i), x, yy, Graphics.LEFT | Graphics.TOP); yy += f.getHeight(); }
        }
        paintBody(g, x, yy, w, focused, f);
    }
    void paintBody(Graphics g, int x, int y, int w, boolean focused, Font f) {}
    boolean select() {
        if (defCmd != null && icl != null) { icl.commandAction(defCmd, this); return true; }
        return false;
    }
    boolean key(int id) { return false; }
}
