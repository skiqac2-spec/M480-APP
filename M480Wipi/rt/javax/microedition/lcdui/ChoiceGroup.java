package javax.microedition.lcdui;
import java.util.Vector;
public class ChoiceGroup extends Item implements Choice {
    int type; Vector strs = new Vector(), imgs = new Vector(), sel = new Vector(); int cur;
    public ChoiceGroup(String l, int t) { super(l); type = t; }
    public ChoiceGroup(String l, int t, String[] s, Image[] im) { this(l, t); for (int i = 0; i < s.length; i++) append(s[i], im != null ? im[i] : null); }
    public int size() { return strs.size(); }
    public String getString(int i) { return (String) strs.elementAt(i); }
    public Image getImage(int i) { return (Image) imgs.elementAt(i); }
    public int append(String s, Image i) { strs.addElement(s); imgs.addElement(i); sel.addElement(new Boolean(strs.size() == 1 && type != MULTIPLE)); changed(); return strs.size() - 1; }
    public void insert(int n, String s, Image i) { strs.insertElementAt(s, n); imgs.insertElementAt(i, n); sel.insertElementAt(new Boolean(false), n); changed(); }
    public void delete(int n) { strs.removeElementAt(n); imgs.removeElementAt(n); sel.removeElementAt(n); changed(); }
    public void deleteAll() { strs.removeAllElements(); imgs.removeAllElements(); sel.removeAllElements(); changed(); }
    public void set(int n, String s, Image i) { strs.setElementAt(s, n); imgs.setElementAt(i, n); changed(); }
    public boolean isSelected(int i) { return ((Boolean) sel.elementAt(i)).booleanValue(); }
    public int getSelectedIndex() { if (type == MULTIPLE) return -1; for (int i = 0; i < size(); i++) if (isSelected(i)) return i; return -1; }
    public int getSelectedFlags(boolean[] f) { int n = 0; for (int i = 0; i < size(); i++) { f[i] = isSelected(i); if (f[i]) n++; } return n; }
    public void setSelectedIndex(int i, boolean s) {
        if (type != MULTIPLE && s) for (int k = 0; k < size(); k++) sel.setElementAt(new Boolean(k == i), k);
        else sel.setElementAt(new Boolean(s), i);
        changed();
    }
    public void setSelectedFlags(boolean[] f) { for (int i = 0; i < size(); i++) sel.setElementAt(new Boolean(f[i]), i); }
    public void setFitPolicy(int p) {}
    public int getFitPolicy() { return 0; }
    public void setFont(int i, Font f) {}
    public Font getFont(int i) { return Font.getDefaultFont(); }
    int bodyHeight(Font f, int w) { return size() * (f.getHeight() + 2); }
    void paintBody(Graphics g, int x, int y, int w, boolean focused, Font f) {
        for (int i = 0; i < size(); i++) {
            if (focused && i == cur) { g.setColor(0xA8C0F0); g.fillRect(x, y, w, f.getHeight() + 2); }
            g.setColor(0);
            g.drawString((isSelected(i) ? (type == MULTIPLE ? "[v] " : "(o) ") : (type == MULTIPLE ? "[ ] " : "( ) ")) + getString(i), x + 2, y + 1, Graphics.LEFT | Graphics.TOP);
            y += f.getHeight() + 2;
        }
    }
    boolean key(int id) {
        if (id == m480.Keys.UP && cur > 0) { cur--; return true; }
        if (id == m480.Keys.DOWN && cur < size() - 1) { cur++; return true; }
        return false;
    }
    boolean select() { if (size() > 0) { setSelectedIndex(cur, type == MULTIPLE ? !isSelected(cur) : true); notifyStateChanged(); } return true; }
}
