package java.util;
public class Date {
    private long t;
    public Date() { t = System.currentTimeMillis(); }
    public Date(long d) { t = d; }
    public long getTime() { return t; }
    public void setTime(long d) { t = d; }
    public boolean equals(Object o) { return o instanceof Date && ((Date) o).t == t; }
    public int hashCode() { return (int) (t ^ (t >>> 32)); }
    public String toString() { Calendar c = Calendar.getInstance(); c.setTime(this); return c.toString(); }
}
