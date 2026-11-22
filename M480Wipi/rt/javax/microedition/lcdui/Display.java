package javax.microedition.lcdui;
public class Display {
    public static final int LIST_ELEMENT = 1, CHOICE_GROUP_ELEMENT = 2, ALERT = 3;
    public static final int COLOR_BACKGROUND = 0, COLOR_FOREGROUND = 1, COLOR_HIGHLIGHTED_BACKGROUND = 2, COLOR_HIGHLIGHTED_FOREGROUND = 3, COLOR_BORDER = 4, COLOR_HIGHLIGHTED_BORDER = 5;
    private static Display def;
    private Displayable current;
    Display() {}
    public static synchronized Display getDisplay(javax.microedition.midlet.MIDlet m) { if (def == null) def = new Display(); return def; }
    public Displayable getCurrent() { return current; }
    public void setCurrent(Displayable d) {
        if (d == null) return;
        if (d instanceof Alert) { Alert a = (Alert) d; if (a.next == null) a.next = current; }
        current = d;
        m480.Sys.setScreen(d);
    }
    public void setCurrent(Alert a, Displayable next) { a.next = next; current = a; m480.Sys.setScreen(a); }
    public void setCurrentItem(Item i) {}
    public void callSerially(Runnable r) { m480.Sys.callSerially(r); }
    public boolean isColor() { return true; }
    public int numColors() { return 65536; }
    public int numAlphaLevels() { return 256; }
    public boolean vibrate(int ms) { m480.Sys.vibrate(ms); return true; }
    public boolean flashBacklight(int ms) { m480.Sys.backlight(1); return true; }
    public int getColor(int which) { return which == COLOR_BACKGROUND ? 0xFFFFFF : which == COLOR_HIGHLIGHTED_BACKGROUND ? 0x3060C0 : which == COLOR_HIGHLIGHTED_FOREGROUND ? 0xFFFFFF : 0; }
    public int getBorderStyle(boolean h) { return 0; }
    public int getBestImageWidth(int t) { return 32; }
    public int getBestImageHeight(int t) { return 32; }
}
