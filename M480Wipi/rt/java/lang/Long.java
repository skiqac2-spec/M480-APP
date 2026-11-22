package java.lang;
public final class Long {
    public static final long MIN_VALUE = 0x8000000000000000L;
    public static final long MAX_VALUE = 0x7fffffffffffffffL;
    private final long value;
    public Long(long v) { value = v; }
    public static Long valueOf(long v) { return new Long(v); }
    public static long parseLong(String s) throws NumberFormatException { return parseLong(s, 10); }
    public static long parseLong(String s, int radix) throws NumberFormatException {
        if (s == null) throw new NumberFormatException("null");
        int n = s.length(), i = 0;
        boolean neg = false;
        if (n == 0) throw new NumberFormatException(s);
        char c = s.charAt(0);
        if (c == '-' || c == '+') { neg = c == '-'; i++; if (n == 1) throw new NumberFormatException(s); }
        long r = 0;
        for (; i < n; i++) {
            int d = Character.digit(s.charAt(i), radix);
            if (d < 0) throw new NumberFormatException(s);
            r = r * radix - d;
        }
        return neg ? r : -r;
    }
    public static String toString(long i) { return toString(i, 10); }
    public static String toString(long i, int radix) {
        if (radix < 2 || radix > 36) radix = 10;
        if (i == 0) return "0";
        char[] buf = new char[65];
        int p = 65;
        boolean neg = i < 0;
        if (!neg) i = -i;
        while (i < 0) { buf[--p] = Character.forDigit((int) -(i % radix), radix); i /= radix; }
        if (neg) buf[--p] = '-';
        return new String(buf, p, 65 - p);
    }
    public static String toHexString(long i) {
        char[] buf = new char[16]; int p = 16;
        do { buf[--p] = Character.forDigit((int) (i & 15), 16); i >>>= 4; } while (i != 0);
        return new String(buf, p, 16 - p);
    }
    public int intValue() { return (int) value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public String toString() { return toString(value); }
    public int hashCode() { return (int) (value ^ (value >>> 32)); }
    public boolean equals(Object o) { return o instanceof Long && ((Long) o).value == value; }
}
