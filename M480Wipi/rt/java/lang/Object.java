package java.lang;
public class Object {
    public Object() {}
    public final native Class getClass();
    public native int hashCode();
    public boolean equals(Object o) { return this == o; }
    protected native Object clone() throws CloneNotSupportedException;
    public String toString() { return getClass().getName() + "@" + Integer.toHexString(hashCode()); }
    public final native void notify();
    public final native void notifyAll();
    public final native void wait(long ms) throws InterruptedException;
    public final void wait() throws InterruptedException { wait(0); }
    public final void wait(long ms, int ns) throws InterruptedException { wait(ms == 0 && ns > 0 ? 1 : ms); }
    protected void finalize() throws Throwable {}
}
