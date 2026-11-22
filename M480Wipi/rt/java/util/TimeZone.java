package java.util;
public abstract class TimeZone {
    private static TimeZone def;
    public static synchronized TimeZone getDefault() { if (def == null) def = new Simple("Asia/Seoul", 9 * 3600000); return def; }
    public static TimeZone getTimeZone(String id) {
        if (id != null && (id.equals("GMT") || id.equals("UTC"))) return new Simple(id, 0);
        return getDefault();
    }
    public static String[] getAvailableIDs() { return new String[] { "GMT", "Asia/Seoul" }; }
    public abstract int getRawOffset();
    public abstract String getID();
    public int getOffset(int era, int y, int m, int d, int dow, int ms) { return getRawOffset(); }
    public boolean useDaylightTime() { return false; }
    static class Simple extends TimeZone {
        String id; int off;
        Simple(String i, int o) { id = i; off = o; }
        public int getRawOffset() { return off; }
        public String getID() { return id; }
    }
}
