package javax.microedition.lcdui;
import java.util.Vector;
public class Form extends Screen {
    Vector items = new Vector();
    ItemStateListener isl;
    public Form(String t) { title = t; }
    public Form(String t, Item[] it) { title = t; if (it != null) for (int i = 0; i < it.length; i++) append(it[i]); }
    public int append(Item i) { i.owner = this; items.addElement(i); repaintSoft(); return items.size() - 1; }
    public int append(String s) { return append(new StringItem(null, s)); }
    public int append(Image img) { return append(new ImageItem(null, img, 0, null)); }
    public void insert(int n, Item i) { i.owner = this; items.insertElementAt(i, n); repaintSoft(); }
    public void delete(int n) { ((Item) items.elementAt(n)).owner = null; items.removeElementAt(n); repaintSoft(); }
    public void deleteAll() { items.removeAllElements(); repaintSoft(); }
    public void set(int n, Item i) { i.owner = this; items.setElementAt(i, n); repaintSoft(); }
    public Item get(int n) { return (Item) items.elementAt(n); }
    public int size() { return items.size(); }
    public void setItemStateListener(ItemStateListener l) { isl = l; }
    int count() { return items.size(); }
    int itemHeight(int i, Font f, int w) { return get(i).height(f, w); }
    void paintItem(Graphics g, int i, int x, int y, int w, boolean focused, Font f) { get(i).paint(g, x, y, w, focused, f); }
    boolean selectItem(int i) { return i >= 0 && i < items.size() && get(i).select(); }
    boolean keyItem(int i, int id) { return i >= 0 && i < items.size() && get(i).key(id); }
}
