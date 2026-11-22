package java.lang;
import java.io.PrintStream;
public final class System {
    public static final PrintStream out = new PrintStream(new m480.LogStream(0));
    public static final PrintStream err = new PrintStream(new m480.LogStream(1));
    private System() {}
    public static native void arraycopy(Object src, int sp, Object dst, int dp, int n);
    public static native long currentTimeMillis();
    public static native int identityHashCode(Object o);
    public static native String getProperty(String key);
    public static String getProperty(String key, String def) { String v = getProperty(key); return v == null ? def : v; }
    public static void gc() { Runtime.getRuntime().gc(); }
    public static native void exit(int code);
}
