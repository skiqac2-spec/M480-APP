package java.lang;
public final class StringBuilder implements CharSequence {

    char[] value;
    int count;
    public StringBuilder() { value = new char[16]; }
    public StringBuilder(int cap) { value = new char[cap < 0 ? 16 : cap]; }
    public StringBuilder(String s) { value = new char[s.length() + 16]; append(s); }
    public int length() { return count; }
    public int capacity() { return value.length; }
    private void grow(int need) {
        if (need <= value.length) return;
        int n = value.length * 2 + 2;
        if (n < need) n = need;
        char[] v = new char[n];
        System.arraycopy(value, 0, v, 0, count);
        value = v;
    }
    public void ensureCapacity(int n) { grow(n); }
    public void setLength(int n) {
        if (n < 0) throw new StringIndexOutOfBoundsException();
        grow(n);
        for (int i = count; i < n; i++) value[i] = 0;
        count = n;
    }
    public char charAt(int i) { if (i < 0 || i >= count) throw new StringIndexOutOfBoundsException(i); return value[i]; }
    public void getChars(int sb, int se, char[] dst, int db) {
        if (sb < 0 || se > count || sb > se) throw new StringIndexOutOfBoundsException();
        System.arraycopy(value, sb, dst, db, se - sb);
    }
    public void setCharAt(int i, char c) { if (i < 0 || i >= count) throw new StringIndexOutOfBoundsException(i); value[i] = c; }
    public StringBuilder append(String s) {
        if (s == null) s = "null";
        int n = s.count;
        grow(count + n);
        System.arraycopy(s.value, s.offset, value, count, n);
        count += n;
        return this;
    }
    public StringBuilder append(Object o) { return append(String.valueOf(o)); }
    public StringBuilder append(StringBuffer sb) { return append(sb == null ? "null" : sb.toString()); }
    public StringBuilder append(CharSequence cs) { return append(cs == null ? "null" : cs.toString()); }
    public StringBuilder append(char[] v) { return append(v, 0, v.length); }
    public StringBuilder append(char[] v, int o, int n) { grow(count + n); System.arraycopy(v, o, value, count, n); count += n; return this; }
    public StringBuilder append(boolean b) { return append(b ? "true" : "false"); }
    public StringBuilder append(char c) { grow(count + 1); value[count++] = c; return this; }
    public StringBuilder append(int i) { return append(Integer.toString(i)); }
    public StringBuilder append(long l) { return append(Long.toString(l)); }
    public StringBuilder append(float f) { return append(Float.toString(f)); }
    public StringBuilder append(double d) { return append(Double.toString(d)); }
    public StringBuilder delete(int s, int e) {
        if (e > count) e = count;
        if (s < 0 || s > e) throw new StringIndexOutOfBoundsException();
        System.arraycopy(value, e, value, s, count - e);
        count -= e - s;
        return this;
    }
    public StringBuilder deleteCharAt(int i) {
        if (i < 0 || i >= count) throw new StringIndexOutOfBoundsException(i);
        System.arraycopy(value, i + 1, value, i, count - i - 1);
        count--;
        return this;
    }
    public StringBuilder insert(int off, String s) {
        if (off < 0 || off > count) throw new StringIndexOutOfBoundsException();
        if (s == null) s = "null";
        int n = s.length();
        grow(count + n);
        System.arraycopy(value, off, value, off + n, count - off);
        s.getChars(0, n, value, off);
        count += n;
        return this;
    }
    public StringBuilder insert(int off, char[] v) { return insert(off, new String(v)); }
    public StringBuilder insert(int off, Object o) { return insert(off, String.valueOf(o)); }
    public StringBuilder insert(int off, boolean b) { return insert(off, String.valueOf(b)); }
    public StringBuilder insert(int off, char c) { return insert(off, String.valueOf(c)); }
    public StringBuilder insert(int off, int i) { return insert(off, String.valueOf(i)); }
    public StringBuilder insert(int off, long l) { return insert(off, String.valueOf(l)); }
    public StringBuilder insert(int off, float f) { return insert(off, String.valueOf(f)); }
    public StringBuilder insert(int off, double d) { return insert(off, String.valueOf(d)); }
    public StringBuilder replace(int s, int e, String str) { delete(s, e); return insert(s, str); }
    public StringBuilder reverse() {
        for (int i = 0, j = count - 1; i < j; i++, j--) { char c = value[i]; value[i] = value[j]; value[j] = c; }
        return this;
    }
    public int indexOf(String s) { return toString().indexOf(s); }
    public int indexOf(String s, int f) { return toString().indexOf(s, f); }
    public int lastIndexOf(String s) { return toString().lastIndexOf(s); }
    public String substring(int b) { return substring(b, count); }
    public String substring(int b, int e) { if (b < 0 || e > count || b > e) throw new StringIndexOutOfBoundsException(); return new String(value, b, e - b); }
    public CharSequence subSequence(int b, int e) { return substring(b, e); }
    public String toString() { return new String(value, 0, count); }
}
