package java.util;
public abstract class TimerTask implements Runnable {
    long next;
    long period;
    boolean fixed;
    boolean cancelled;
    boolean scheduled;
    protected TimerTask() {}
    public abstract void run();
    public boolean cancel() { boolean r = scheduled && !cancelled; cancelled = true; return r; }
    public long scheduledExecutionTime() { return next - period; }
}
