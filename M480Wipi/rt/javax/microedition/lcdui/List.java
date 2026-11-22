package javax.microedition.lcdui;
import java.util.Vector;
public class List extends Screen implements Choice {
    public static final Command SELECT_COMMAND = new Command("", Command.SCREEN, 0);
    int type; Vector strs = new Vector(), imgs = new Vector(); boolean[] sel = new boolean[0];
    Command selCmd = SELECT_COMMAND;
    public List(String t, int type) { title = t; this.type = type; }
    public List(String t, int type, String[] s, Image[] im) { this(t, type); for (int i = 0; i < s.length; i++) append(s[i], im != null ? im[i] : null); }
    private void grow() { boolean[] n = new boolean[strs.size()]; System.arraycopy(sel, 0, n, 0, Math.min(sel.length, n.length)); sel = n; }
    public int size() { return strs.size(); }
    public String getString(int i) { return (String) strs.elementAt(i); }
    public Image getImage(int i) { return (Image) imgs.elementAt(i); }
    public int append(String s, Image i) { strs.addElement(s); imgs.addElement(i); grow(); if (strs.size() == 1 && type != MULTIPLE) sel[0] = true; repaintSoft(); return strs.size() - 1; }
    public void insert(int n, String s, Image i) { strs.insertElementAt(s, n); imgs.insertElementAt(i, n); grow(); repaintSoft(); }
    public void delete(int n) { strs.removeElementAt(n); imgs.removeElementAt(n); grow(); repaintSoft(); }
    public void deleteAll() { strs.removeAllElements(); imgs.removeAllElements(); grow(); repaintSoft(); }
    public void set(int n, String s, Image i) { strs.setElementAt(s, n); imgs.setElementAt(i, n); repaintSoft(); }
    public boolean isSelected(int i) { return type == IMPLICIT ? i == focus : sel[i]; }
    public int getSelectedIndex() { if (type == IMPLICIT) return size() > 0 ? focus : -1; for (int i = 0; i < sel.length; i++) if (sel[i]) return i; return -1; }
    public int getSelectedFlags(boolean[] f) { int n = 0; for (int i = 0; i < size(); i++) { f[i] = isSelected(i); if (f[i]) n++; } return n; }
    public void setSelectedIndex(int i, boolean s) {
        if (type == IMPLICIT) { if (s) focus = i; }
        else if (type == EXCLUSIVE) { if (s) for (int k = 0; k < sel.length; k++) sel[k] = k == i; }
        else sel[i] = s;
        repaintSoft();
    }
    public void setSelectedFlags(boolean[] f) { for (int i = 0; i < sel.length; i++) sel[i] = f[i]; }
    public void setSelectCommand(Command c) { selCmd = c; }
    public void setFitPolicy(int p) {}
    public int getFitPolicy() { return 0; }
    public void setFont(int i, Font f) {}
    public Font getFont(int i) { return Font.getDefaultFont(); }
    int count() { return strs.size(); }
    int itemHeight(int i, Font f, int w) { Image im = getImage(i); return Math.max(f.getHeight(), im != null ? im.getHeight() : 0) + 6; }
    void paintItem(Graphics g, int i, int x, int y, int w, boolean focused, Font f) {
        int h = itemHeight(i, f, w);
        if (focused) { g.setColor(0x3060C0); g.fillRect(x - 2, y, w + 4, h); }
        int tx = x + 2;
        Image im = getImage(i);
        if (im != null) { g.drawImage(im, tx, y + h / 2, Graphics.LEFT | Graphics.VCENTER); tx += im.getWidth() + 3; }
        String pre = type == MULTIPLE ? (sel[i] ? "[v] " : "[ ] ") : type == EXCLUSIVE ? (sel[i] ? "(o) " : "( ) ") : "";
        g.setColor(focused ? 0xFFFFFF : 0);
        g.drawString(pre + getString(i), tx, y + 3, Graphics.LEFT | Graphics.TOP);
    }
    boolean selectItem(int i) {
        if (size() == 0) return false;
        if (type == IMPLICIT) { if (listener != null && selCmd != null) { listener.commandAction(selCmd, this); return true; } return false; }
        setSelectedIndex(i, type == MULTIPLE ? !sel[i] : true);
        return true;
    }
}
