package m480;
import java.util.Vector;
public final class Sys {
    public static final int EV_KEY_PRESS = 1, EV_KEY_RELEASE = 2, EV_KEY_REPEAT = 3,
        EV_PTR_PRESS = 4, EV_PTR_RELEASE = 5, EV_PTR_DRAG = 6,
        EV_REPAINT = 10, EV_SERIAL = 11, EV_PAUSE = 20, EV_RESUME = 21, EV_QUIT = 22;
    public static native void getEvent(int[] ev);
    public static native void postEvent(int type, int a, int b, int c);
    public static native void flush();
    public static native int width();
    public static native int height();
    public static native int carrier();
    public static native String prop(String key);
    public static native void vibrate(int ms);
    public static native void backlight(int on);
    public static native void quit();
    public static native void fail(String why);

    public static Screen current;
    static Thread eventThread;
    static final Object lock = new Object();
    static boolean repaintPending;
    static final Vector serial = new Vector();
    public static App app;

    public static void setScreen(Screen s) {
        Screen old = current;
        if (old == s) { repaint(); return; }
        current = s;
        if (old != null) old.hidden();
        if (s != null) s.shown();
        repaint();
    }
    public static void repaint() {
        synchronized (lock) {
            if (!repaintPending) { repaintPending = true; postEvent(EV_REPAINT, 0, 0, 0); }
        }
    }
    public static void serviceRepaints() {
        if (Thread.currentThread() == eventThread) { if (repaintPending) paintNow(); return; }
        synchronized (lock) {
            int guard = 0;
            while (repaintPending && guard++ < 20) { try { lock.wait(25); } catch (InterruptedException e) { return; } }
        }
    }
    public static void callSerially(Runnable r) {
        synchronized (serial) { serial.addElement(r); }
        postEvent(EV_SERIAL, 0, 0, 0);
    }
    static void paintNow() {
        synchronized (lock) { repaintPending = false; }
        Screen s = current;
        if (s != null) {
            try { s.paintScreen(); } catch (Throwable t) { System.err.println("paint: " + t); t.printStackTrace(); }
        }
        flush();
        synchronized (lock) { lock.notifyAll(); }
    }
    public static boolean isEventThread() { return Thread.currentThread() == eventThread; }

    /* event loop - runs on the boot thread after the app was started */
    static void loop() {
        int[] ev = new int[4];
        eventThread = Thread.currentThread();
        while (true) {
            getEvent(ev);
            try {
                switch (ev[0]) {
                case EV_KEY_PRESS: case EV_KEY_RELEASE: case EV_KEY_REPEAT:
                    if (current != null) current.keyEvent(ev[0], ev[1]);
                    break;
                case EV_PTR_PRESS: case EV_PTR_RELEASE: case EV_PTR_DRAG:
                    if (current != null) current.pointerEvent(ev[0], ev[1], ev[2]);
                    break;
                case EV_REPAINT:
                    if (repaintPending) paintNow();
                    break;
                case EV_SERIAL: {
                    Runnable r = null;
                    synchronized (serial) { if (serial.size() > 0) { r = (Runnable) serial.elementAt(0); serial.removeElementAt(0); } }
                    if (r != null) r.run();
                    break;
                }
                case EV_PAUSE: if (app != null) app.pause(); break;
                case EV_RESUME: if (app != null) app.resume(); repaint(); break;
                case EV_QUIT:
                    if (app != null) { try { app.destroy(); } catch (Throwable t) {} }
                    quit();
                    return;
                }
            } catch (Throwable t) {
                System.err.println("event " + ev[0] + ": " + t);
                t.printStackTrace();
            }
        }
    }
}
