package m480;
/* per-game persistent storage (RMS, WIPI DataBase, WIPI files) */
public final class Store {
    public static native byte[] load(String kind, String name);
    public static native boolean save(String kind, String name, byte[] data, int len);
    public static native boolean remove(String kind, String name);
    public static native String[] list(String kind);
    public static native boolean exists(String kind, String name);
}
