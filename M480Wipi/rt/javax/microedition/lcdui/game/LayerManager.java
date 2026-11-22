package javax.microedition.lcdui.game;
import javax.microedition.lcdui.Graphics;
import java.util.Vector;
public class LayerManager {
    Vector layers = new Vector(); int vx, vy, vw = Integer.MAX_VALUE, vh = Integer.MAX_VALUE;
    public LayerManager() {}
    public void append(Layer l) { layers.removeElement(l); layers.addElement(l); }
    public void insert(Layer l, int i) { layers.removeElement(l); layers.insertElementAt(l, i); }
    public Layer getLayerAt(int i) { return (Layer) layers.elementAt(i); }
    public int getSize() { return layers.size(); }
    public void remove(Layer l) { layers.removeElement(l); }
    public void setViewWindow(int x, int y, int w, int h) { vx = x; vy = y; vw = w; vh = h; }
    public void paint(Graphics g, int x, int y) {
        int ox = g.getClipX(), oy = g.getClipY(), ow = g.getClipWidth(), oh = g.getClipHeight();
        g.translate(x - vx, y - vy);
        g.clipRect(vx, vy, vw, vh);
        for (int i = layers.size() - 1; i >= 0; i--) { Layer l = (Layer) layers.elementAt(i); if (l.visible) l.paint(g); }
        g.translate(vx - x, vy - y);
        g.setClip(ox, oy, ow, oh);
    }
}
