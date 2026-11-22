package java.util;
public class Timer {
    private final Vector tasks = new Vector();
    private boolean cancelled;
    private Thread thread;
    public Timer() {
        thread = new Thread() { public void run() { loop(); } };
        thread.start();
    }
    private void loop() {
        while (true) {
            TimerTask run = null;
            synchronized (tasks) {
                if (cancelled) return;
                long now = System.currentTimeMillis(), wait = 1000;
                for (int i = 0; i < tasks.size(); i++) {
                    TimerTask t = (TimerTask) tasks.elementAt(i);
                    if (t.cancelled) { tasks.removeElementAt(i--); continue; }
                    long d = t.next - now;
                    if (d <= 0) { run = t; break; }
                    if (d < wait) wait = d;
                }
                if (run == null) { try { tasks.wait(wait); } catch (InterruptedException e) {} continue; }
                if (run.period > 0) run.next = run.fixed ? run.next + run.period : now + run.period;
                else { tasks.removeElement(run); }
            }
            try { run.run(); } catch (Throwable e) { System.err.println("TimerTask: " + e); e.printStackTrace(); }
        }
    }
    private void add(TimerTask t, long first, long period, boolean fixed) {
        synchronized (tasks) {
            if (cancelled) throw new IllegalStateException();
            if (t.scheduled) throw new IllegalStateException();
            t.scheduled = true; t.next = first; t.period = period; t.fixed = fixed;
            tasks.addElement(t);
            tasks.notifyAll();
        }
    }
    public void schedule(TimerTask t, long delay) { if (delay < 0) throw new IllegalArgumentException(); add(t, System.currentTimeMillis() + delay, 0, false); }
    public void schedule(TimerTask t, Date d) { add(t, d.getTime(), 0, false); }
    public void schedule(TimerTask t, long delay, long period) { if (delay < 0 || period <= 0) throw new IllegalArgumentException(); add(t, System.currentTimeMillis() + delay, period, false); }
    public void schedule(TimerTask t, Date d, long period) { add(t, d.getTime(), period, false); }
    public void scheduleAtFixedRate(TimerTask t, long delay, long period) { if (delay < 0 || period <= 0) throw new IllegalArgumentException(); add(t, System.currentTimeMillis() + delay, period, true); }
    public void scheduleAtFixedRate(TimerTask t, Date d, long period) { add(t, d.getTime(), period, true); }
    public void cancel() { synchronized (tasks) { cancelled = true; tasks.removeAllElements(); tasks.notifyAll(); } }
}
