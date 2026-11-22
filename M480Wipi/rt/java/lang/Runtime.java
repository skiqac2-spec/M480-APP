package java.lang;
public class Runtime {
    private static Runtime rt = new Runtime();
    private Runtime() {}
    public static Runtime getRuntime() { return rt; }
    public native long freeMemory();
    public native long totalMemory();
    public native void gc();
    public void exit(int code) { System.exit(code); }
}
