package javax.microedition.lcdui;
public class ImageItem extends Item {
    Image img; String alt; int appearance;
    public ImageItem(String l, Image i, int layout, String alt) { this(l, i, layout, alt, PLAIN); }
    public ImageItem(String l, Image i, int layout, String alt, int ap) { super(l); img = i; this.layout = layout; this.alt = alt; appearance = ap; }
    public Image getImage() { return img; }
    public void setImage(Image i) { img = i; changed(); }
    public String getAltText() { return alt; }
    public void setAltText(String a) { alt = a; }
    public int getAppearanceMode() { return appearance; }
    int bodyHeight(Font f, int w) { return img != null ? img.getHeight() : 0; }
    void paintBody(Graphics g, int x, int y, int w, boolean focused, Font f) {
        if (img == null) return;
        int a = layout & 3;
        if (a == LAYOUT_CENTER) g.drawImage(img, x + w / 2, y, Graphics.HCENTER | Graphics.TOP);
        else if (a == LAYOUT_RIGHT) g.drawImage(img, x + w, y, Graphics.RIGHT | Graphics.TOP);
        else g.drawImage(img, x, y, Graphics.LEFT | Graphics.TOP);
    }
}
