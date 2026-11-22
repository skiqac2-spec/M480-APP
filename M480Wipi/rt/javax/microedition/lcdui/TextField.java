package javax.microedition.lcdui;
public class TextField extends Item {
    public static final int ANY = 0, EMAILADDR = 1, NUMERIC = 2, PHONENUMBER = 3, URL = 4, DECIMAL = 5, PASSWORD = 0x10000,
        UNEDITABLE = 0x20000, SENSITIVE = 0x40000, NON_PREDICTIVE = 0x80000, INITIAL_CAPS_WORD = 0x100000, INITIAL_CAPS_SENTENCE = 0x200000, CONSTRAINT_MASK = 0xFFFF;
    StringBuffer text = new StringBuffer(); int max, cons;
    public TextField(String l, String t, int max, int c) { super(l); this.max = max; cons = c; if (t != null) text.append(t); }
    public String getString() { return text.toString(); }
    public void setString(String s) { text.setLength(0); if (s != null) text.append(s); changed(); }
    public int getChars(char[] d) { String s = text.toString(); s.getChars(0, s.length(), d, 0); return s.length(); }
    public void setChars(char[] d, int o, int l) { text.setLength(0); text.append(d, o, l); }
    public void insert(String s, int pos) { text.insert(pos, s); }
    public void insert(char[] d, int o, int l, int pos) { text.insert(pos, new String(d, o, l)); }
    public void delete(int o, int l) { text.delete(o, o + l); }
    public int getMaxSize() { return max; }
    public int setMaxSize(int m) { max = m; return m; }
    public int size() { return text.length(); }
    public int getCaretPosition() { return text.length(); }
    public void setConstraints(int c) { cons = c; }
    public int getConstraints() { return cons; }
    public void setInitialInputMode(String m) {}
    int bodyHeight(Font f, int w) { return f.getHeight() + 4; }
    void paintBody(Graphics g, int x, int y, int w, boolean focused, Font f) {
        g.setColor(0xFFFFFF); g.fillRect(x, y, w, f.getHeight() + 3);
        g.setColor(focused ? 0x3060C0 : 0x909090); g.drawRect(x, y, w - 1, f.getHeight() + 3);
        String s = text.toString();
        if ((cons & PASSWORD) != 0) { StringBuffer b = new StringBuffer(); for (int i = 0; i < s.length(); i++) b.append('*'); s = b.toString(); }
        g.setColor(0); g.drawString(s + (focused ? "_" : ""), x + 2, y + 2, Graphics.LEFT | Graphics.TOP);
    }
    boolean key(int id) { return edit(text, max, id); }
    static boolean edit(StringBuffer text, int max, int id) {
        if (id >= 0 && id <= 9) { if (text.length() < max) text.append((char) ('0' + id)); return true; }
        if (id == m480.Keys.STAR) { if (text.length() < max) text.append('*'); return true; }
        if (id == m480.Keys.POUND) { if (text.length() < max) text.append('#'); return true; }
        if (id == m480.Keys.CLEAR && text.length() > 0) { text.setLength(text.length() - 1); return true; }
        return false;
    }
}
