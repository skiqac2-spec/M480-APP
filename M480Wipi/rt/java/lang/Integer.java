package java.lang;
public final class Integer {
    public static final int MIN_VALUE = 0x80000000;
    public static final int MAX_VALUE = 0x7fffffff;
    private final int value;
    public Integer(int v) { value = v; }
    public static Integer valueOf(int v) { return new Integer(v); }
    public static Integer valueOf(String s) throws NumberFormatException { return new Integer(parseInt(s, 10)); }
    public static Integer valueOf(String s, int r) throws NumberFormatException { return new Integer(parseInt(s, r)); }
    public static int parseInt(String s) throws NumberFormatException { return parseInt(s, 10); }
    public static int parseInt(String s, int radix) throws NumberFormatException {
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
            r = r * radix + d;
            if (r > 0x80000000L) throw new NumberFormatException(s);
        }
        if (neg) r = -r;
        if (r > MAX_VALUE || r < MIN_VALUE) throw new NumberFormatException(s);
        return (int) r;
    }
    public static String toString(int i) { return toString(i, 10); }
    public static String toString(int i, int radix) {
        if (radix < 2 || radix > 36) radix = 10;
        if (i == 0) return "0";
        char[] buf = new char[33];
        int p = 33;
        boolean neg = i < 0;
        long v = i;
        if (neg) v = -v;
        while (v > 0) { buf[--p] = Character.forDigit((int) (v % radix), radix); v /= radix; }
        if (neg) buf[--p] = '-';
        return new String(buf, p, 33 - p);
    }
    private static String unsigned(int i, int shift) {
        char[] buf = new char[32];
        int p = 32, mask = (1 << shift) - 1;
        do { buf[--p] = Character.forDigit(i & mask, 1 << shift); i >>>= shift; } while (i != 0);
        return new String(buf, p, 32 - p);
    }
    public static String toHexString(int i) { return unsigned(i, 4); }
    public static String toOctalString(int i) { return unsigned(i, 3); }
    public static String toBinaryString(int i) { return unsigned(i, 1); }
    public byte byteValue() { return (byte) value; }
    public short shortValue() { return (short) value; }
    public int intValue() { return value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public String toString() { return toString(value); }
    public int hashCode() { return value; }
    public boolean equals(Object o) { return o instanceof Integer && ((Integer) o).value == value; }
    public int compareTo(Integer o) { return value < o.value ? -1 : value > o.value ? 1 : 0; }
}
