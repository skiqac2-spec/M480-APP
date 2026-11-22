package java.lang;
public class Thread implements Runnable {
    public static final int MIN_PRIORITY = 1;
    public static final int NORM_PRIORITY = 5;
    public static final int MAX_PRIORITY = 10;
    private int vmref;
    private Runnable target;
    private String name;
    private int priority = NORM_PRIORITY;
    private boolean started;
    boolean alive;
    private boolean daemon;
    private static int count;
    public Thread() { this(null, null); }
    public Thread(Runnable r) { this(r, null); }
    public Thread(String n) { this(null, n); }
    public Thread(Runnable r, String n) {
        target = r;
        synchronized (Thread.class) { count++; name = n != null ? n : "Thread-" + count; }
    }
    public static native Thread currentThread();
    public static native void sleep(long ms) throws InterruptedException;
    public static native void yield();
    private native void start0();
    public synchronized void start() {
        if (started) throw new IllegalThreadStateException();
        started = true;
        alive = true;
        start0();
    }
    private void runWrapper() {
        try {
            run();
        } catch (Throwable t) {
            System.err.println("Uncaught in " + name + ": " + t);
            t.printStackTrace();
        }
        alive = false;
        synchronized (this) { notifyAll(); }
    }
    public void run() { if (target != null) target.run(); }
    public final boolean isAlive() { return alive; }
    public final void join() throws InterruptedException { synchronized (this) { while (alive) wait(100); } }
    public final void join(long ms) throws InterruptedException {
        long end = System.currentTimeMillis() + ms;
        synchronized (this) { while (alive) { long d = end - System.currentTimeMillis(); if (ms > 0 && d <= 0) break; wait(ms > 0 ? (d < 100 ? d : 100) : 100); } }
    }
    public final void setPriority(int p) { priority = p; }
    public final int getPriority() { return priority; }
    public final String getName() { return name; }
    public final void setName(String n) { name = n; }
    public final void setDaemon(boolean d) { daemon = d; }
    public final boolean isDaemon() { return daemon; }
    public native void interrupt();
    public native boolean isInterrupted();
    public static boolean interrupted() { Thread t = currentThread(); boolean b = t.isInterrupted(); if (b) t.clear0(); return b; }
    private native void clear0();
    public static native int activeCount();
    public String toString() { return "Thread[" + name + "," + priority + "]"; }
}
