package org.kwis.msp.lcdui;
public class EventQueue {
    public static final int KEY_EVENT = 1, POINTER_EVENT = 2, REPAINT_EVENT = 41, NOTIFY_EVENT = 1000;
    public static final int KEY_PRESSED = 1, KEY_RELEASED = 2, KEY_REPEATED = 3, KEY_TYPED = 4;
    public static final int POINTER_PRESSED = 1, POINTER_RELEASED = 2, POINTER_DRAGGED = 3;
    Jlet jlet;
    public EventQueue(Jlet j) { jlet = j; }
    public void getNextEvent(int[] ev) { try { Thread.sleep(50); } catch (InterruptedException e) {} ev[0] = 0; }
    public void dispatchEvent(int[] ev) {}
    public boolean postEvent(int[] ev) { return true; }
    public static void postEvent(int type, int[] ev) {}
}
