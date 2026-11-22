package javax.microedition.lcdui;
public class TextBox extends Screen {
    StringBuffer text = new StringBuffer(); int max, cons;
    public TextBox(String t, String s, int max, int c) { title = t; this.max = max; cons = c; if (s != null) text.append(s); }
    public String getString() { return text.toString(); }
    public void setString(String s) { text.setLength(0); if (s != null) text.append(s); repaintSoft(); }
    public int getChars(char[] d) { String s = text.toString(); s.getChars(0, s.length(), d, 0); return s.length(); }
    public void setChars(char[] d, int o, int l) { text.setLength(0); text.append(d, o, l); }
    public void insert(String s, int p) { text.insert(p, s); }
    public void delete(int o, int l) { text.delete(o, o + l); }
    public int getMaxSize() { return max; }
    public int setMaxSize(int m) { max = m; return m; }
    public int size() { return text.length(); }
    public int getCaretPosition() { return text.length(); }
    public void setConstraints(int c) { cons = c; }
    public int getConstraints() { return cons; }
    public void setInitialInputMode(String m) {}
    int count() { return 1; }
    int itemHeight(int i, Font f, int w) { return Math.max(1, wrap(text.toString() + "_", f, w).size()) * f.getHeight() + 4; }
    void paintItem(Graphics g, int i, int x, int y, int w, boolean focused, Font f) {
        java.util.Vector v = wrap(text.toString() + "_", f, w);
        g.setColor(0);
        for (int k = 0; k < v.size(); k++) g.drawString((String) v.elementAt(k), x, y + 2 + k * f.getHeight(), Graphics.LEFT | Graphics.TOP);
    }
    boolean keyItem(int i, int id) { return TextField.edit(text, max, id); }
}
