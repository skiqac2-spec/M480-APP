package java.lang;
import java.io.UnsupportedEncodingException;
public final class String implements Comparable, CharSequence {
    char[] value;
    int offset;
    int count;
    private int hash;

    public String() { value = new char[0]; }
    public String(String s) { value = s.value; offset = s.offset; count = s.count; }
    public String(char[] v) { this(v, 0, v.length); }
    public String(char[] v, int off, int n) {
        if (off < 0 || n < 0 || off > v.length - n) throw new StringIndexOutOfBoundsException();
        value = new char[n];
        System.arraycopy(v, off, value, 0, n);
        count = n;
    }
    String(int off, int n, char[] v) { value = v; offset = off; count = n; }
    public String(byte[] b) { this(b, 0, b.length); }
    public String(byte[] b, int off, int n) {
        if (off < 0 || n < 0 || off > b.length - n) throw new StringIndexOutOfBoundsException();
        value = decode0(b, off, n, 0); count = value.length;
    }
    public String(byte[] b, String enc) throws UnsupportedEncodingException { this(b, 0, b.length, enc); }
    public String(byte[] b, int off, int n, String enc) throws UnsupportedEncodingException {
        if (off < 0 || n < 0 || off > b.length - n) throw new StringIndexOutOfBoundsException();
        value = decode0(b, off, n, encId(enc)); count = value.length;
    }
    public String(StringBuffer sb) { this(sb.toString()); }
    static int encId(String enc) throws UnsupportedEncodingException {
        if (enc == null) throw new NullPointerException();
        String e = enc.toUpperCase();
        if (e.equals("UTF-8") || e.equals("UTF8")) return 1;
        if (e.equals("ISO-8859-1") || e.equals("ISO8859_1") || e.equals("ASCII") || e.equals("US-ASCII") || e.equals("8859_1")) return 2;
        if (e.equals("UTF-16BE") || e.equals("UNICODE") || e.equals("UTF-16") || e.equals("UNICODEBIG")) return 3;
        if (e.equals("UTF-16LE") || e.equals("UNICODELITTLE")) return 4;
        return 0; /* KSC5601, EUC-KR, MS949, CP949 and unknown */
    }
    static native char[] decode0(byte[] b, int off, int n, int enc);
    static native byte[] encode0(char[] v, int off, int n, int enc);

    public int length() { return count; }
    public char charAt(int i) {
        if (i < 0 || i >= count) throw new StringIndexOutOfBoundsException(i);
        return value[offset + i];
    }
    public void getChars(int sb, int se, char[] dst, int db) {
        if (sb < 0 || se > count || sb > se) throw new StringIndexOutOfBoundsException();
        System.arraycopy(value, offset + sb, dst, db, se - sb);
    }
    public byte[] getBytes() { return encode0(value, offset, count, 0); }
    public byte[] getBytes(String enc) throws UnsupportedEncodingException { return encode0(value, offset, count, encId(enc)); }
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof String)) return false;
        String s = (String) o;
        if (s.count != count) return false;
        char[] a = value, b = s.value;
        int i = offset, j = s.offset, n = count;
        while (n-- > 0) if (a[i++] != b[j++]) return false;
        return true;
    }
    public boolean equalsIgnoreCase(String s) {
        if (s == null || s.count != count) return false;
        return regionMatches(true, 0, s, 0, count);
    }
    public int compareTo(String s) {
        int n = count < s.count ? count : s.count;
        char[] a = value, b = s.value;
        int i = offset, j = s.offset;
        while (n-- > 0) { int d = a[i++] - b[j++]; if (d != 0) return d; }
        return count - s.count;
    }
    public int compareTo(Object o) { return compareTo((String) o); }
    public boolean regionMatches(boolean ic, int to, String other, int oo, int len) {
        if (oo < 0 || to < 0 || to > (long) count - len || oo > (long) other.count - len) return false;
        char[] a = value, b = other.value;
        int i = offset + to, j = other.offset + oo;
        while (len-- > 0) {
            char c1 = a[i++], c2 = b[j++];
            if (c1 == c2) continue;
            if (ic) {
                char u1 = Character.toUpperCase(c1), u2 = Character.toUpperCase(c2);
                if (u1 == u2 || Character.toLowerCase(u1) == Character.toLowerCase(u2)) continue;
            }
            return false;
        }
        return true;
    }
    public boolean regionMatches(int to, String other, int oo, int len) { return regionMatches(false, to, other, oo, len); }
    public boolean startsWith(String p, int off) {
        if (off < 0 || off > count - p.count) return false;
        char[] a = value, b = p.value;
        int i = offset + off, j = p.offset, n = p.count;
        while (n-- > 0) if (a[i++] != b[j++]) return false;
        return true;
    }
    public boolean startsWith(String p) { return startsWith(p, 0); }
    public boolean endsWith(String s) { return startsWith(s, count - s.count); }
    public int hashCode() {
        int h = hash;
        if (h == 0 && count > 0) {
            char[] v = value;
            int o = offset;
            for (int i = 0; i < count; i++) h = 31 * h + v[o + i];
            hash = h;
        }
        return h;
    }
    public int indexOf(int ch) { return indexOf(ch, 0); }
    public int indexOf(int ch, int from) {
        if (from < 0) from = 0;
        char[] v = value;
        for (int i = from; i < count; i++) if (v[offset + i] == ch) return i;
        return -1;
    }
    public int lastIndexOf(int ch) { return lastIndexOf(ch, count - 1); }
    public int lastIndexOf(int ch, int from) {
        char[] v = value;
        if (from >= count) from = count - 1;
        for (int i = from; i >= 0; i--) if (v[offset + i] == ch) return i;
        return -1;
    }
    public int indexOf(String s) { return indexOf(s, 0); }
    public int indexOf(String s, int from) {
        if (from < 0) from = 0;
        int n = s.count;
        if (n == 0) return from <= count ? from : count;
        char first = s.value[s.offset];
        int max = count - n;
        for (int i = from; i <= max; i++) {
            if (value[offset + i] != first) continue;
            int j = 1;
            while (j < n && value[offset + i + j] == s.value[s.offset + j]) j++;
            if (j == n) return i;
        }
        return -1;
    }
    public int lastIndexOf(String s) { return lastIndexOf(s, count); }
    public int lastIndexOf(String s, int from) {
        int n = s.count;
        if (from > count - n) from = count - n;
        for (int i = from; i >= 0; i--) if (startsWith(s, i)) return i;
        return -1;
    }
    public String substring(int b) { return substring(b, count); }
    public String substring(int b, int e) {
        if (b < 0 || e > count || b > e) throw new StringIndexOutOfBoundsException();
        if (b == 0 && e == count) return this;
        return new String(offset + b, e - b, value);
    }
    public CharSequence subSequence(int b, int e) { return substring(b, e); }
    public String concat(String s) {
        if (s.count == 0) return this;
        char[] v = new char[count + s.count];
        System.arraycopy(value, offset, v, 0, count);
        System.arraycopy(s.value, s.offset, v, count, s.count);
        return new String(0, v.length, v);
    }
    public String replace(char a, char b) {
        if (indexOf(a) < 0) return this;
        char[] v = new char[count];
        for (int i = 0; i < count; i++) { char c = value[offset + i]; v[i] = c == a ? b : c; }
        return new String(0, count, v);
    }
    public String toLowerCase() {
        char[] v = new char[count];
        for (int i = 0; i < count; i++) v[i] = Character.toLowerCase(value[offset + i]);
        return new String(0, count, v);
    }
    public String toUpperCase() {
        char[] v = new char[count];
        for (int i = 0; i < count; i++) v[i] = Character.toUpperCase(value[offset + i]);
        return new String(0, count, v);
    }
    public String trim() {
        int b = 0, e = count;
        while (b < e && value[offset + b] <= ' ') b++;
        while (e > b && value[offset + e - 1] <= ' ') e--;
        return substring(b, e);
    }
    public char[] toCharArray() { char[] v = new char[count]; System.arraycopy(value, offset, v, 0, count); return v; }
    public String toString() { return this; }
    public native String intern();
    public boolean contains(CharSequence s) { return indexOf(s.toString()) >= 0; }
    public boolean isEmpty() { return count == 0; }

    public static String valueOf(Object o) { return o == null ? "null" : o.toString(); }
    public static String valueOf(char[] v) { return new String(v); }
    public static String valueOf(char[] v, int o, int n) { return new String(v, o, n); }
    public static String copyValueOf(char[] v) { return new String(v); }
    public static String copyValueOf(char[] v, int o, int n) { return new String(v, o, n); }
    public static String valueOf(boolean b) { return b ? "true" : "false"; }
    public static String valueOf(char c) { char[] v = { c }; return new String(0, 1, v); }
    public static String valueOf(int i) { return Integer.toString(i); }
    public static String valueOf(long l) { return Long.toString(l); }
    public static String valueOf(float f) { return Float.toString(f); }
    public static String valueOf(double d) { return Double.toString(d); }
}
