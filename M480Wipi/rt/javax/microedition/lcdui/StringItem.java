package javax.microedition.lcdui;
import java.util.Vector;
public class StringItem extends Item {
    String text; int appearance; Font font;
    public StringItem(String l, String t) { this(l, t, PLAIN); }
    public StringItem(String l, String t, int a) { super(l); text = t; appearance = a; }
    public String getText() { return text; }
    public void setText(String t) { text = t; changed(); }
    public int getAppearanceMode() { return appearance; }
    public void setFont(Font f) { font = f; }
    public Font getFont() { return font == null ? Font.getDefaultFont() : font; }
    int bodyHeight(Font f, int w) { return Screen.wrap(text, f, w).size() * f.getHeight(); }
    void paintBody(Graphics g, int x, int y, int w, boolean focused, Font f) {
        Vector v = Screen.wrap(text, f, w);
        g.setColor(appearance == HYPERLINK ? 0x2040C0 : 0x202020);
        for (int i = 0; i < v.size(); i++) { g.drawString((String) v.elementAt(i), x, y, Graphics.LEFT | Graphics.TOP); y += f.getHeight(); }
    }
}
