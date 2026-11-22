package java.lang;
public final class StringBuffer implements CharSequence {

    char[] value;
    int count;
    public StringBuffer() { value = new char[16]; }
    public StringBuffer(int cap) { value = new char[cap < 0 ? 16 : cap]; }
    public StringBuffer(String s) { value = new char[s.length() + 16]; append(s); }
    public synchronized int length() { return count; }
    public synchronized int capacity() { return value.length; }
    private void grow(int need) {
        if (need <= value.length) return;
        int n = value.length * 2 + 2;
        if (n < need) n = need;
        char[] v = new char[n];
        System.arraycopy(value, 0, v, 0, count);
        value = v;
    }
    public synchronized void ensureCapacity(int n) { grow(n); }
    public synchronized void setLength(int n) {
        if (n < 0) throw new StringIndexOutOfBoundsException();
        grow(n);
        for (int i = count; i < n; i++) value[i] = 0;
        count = n;
    }
    public synchronized char charAt(int i) { if (i < 0 || i >= count) throw new StringIndexOutOfBoundsException(i); return value[i]; }
    public synchronized void getChars(int sb, int se, char[] dst, int db) {
        if (sb < 0 || se > count || sb > se) throw new StringIndexOutOfBoundsException();
        System.arraycopy(value, sb, dst, db, se - sb);
    }
    public synchronized void setCharAt(int i, char c) { if (i < 0 || i >= count) throw new StringIndexOutOfBoundsException(i); value[i] = c; }
    public synchronized StringBuffer append(String s) {
        if (s == null) s = "null";
        int n = s.count;
        grow(count + n);
        System.arraycopy(s.value, s.offset, value, count, n);
        count += n;
        return this;
    }
    public StringBuffer append(Object o) { return append(String.valueOf(o)); }
    public StringBuffer append(StringBuffer sb) { return append(sb == null ? "null" : sb.toString()); }
    public StringBuffer append(CharSequence cs) { return append(cs == null ? "null" : cs.toString()); }
    public synchronized StringBuffer append(char[] v) { return append(v, 0, v.length); }
    public synchronized StringBuffer append(char[] v, int o, int n) { grow(count + n); System.arraycopy(v, o, value, count, n); count += n; return this; }
    public StringBuffer append(boolean b) { return append(b ? "true" : "false"); }
    public synchronized StringBuffer append(char c) { grow(count + 1); value[count++] = c; return this; }
    public StringBuffer append(int i) { return append(Integer.toString(i)); }
    public StringBuffer append(long l) { return append(Long.toString(l)); }
    public StringBuffer append(float f) { return append(Float.toString(f)); }
    public StringBuffer append(double d) { return append(Double.toString(d)); }
    public synchronized StringBuffer delete(int s, int e) {
        if (e > count) e = count;
        if (s < 0 || s > e) throw new StringIndexOutOfBoundsException();
        System.arraycopy(value, e, value, s, count - e);
        count -= e - s;
        return this;
    }
    public synchronized StringBuffer deleteCharAt(int i) {
        if (i < 0 || i >= count) throw new StringIndexOutOfBoundsException(i);
        System.arraycopy(value, i + 1, value, i, count - i - 1);
        count--;
        return this;
    }
    public synchronized StringBuffer insert(int off, String s) {
        if (off < 0 || off > count) throw new StringIndexOutOfBoundsException();
        if (s == null) s = "null";
        int n = s.length();
        grow(count + n);
        System.arraycopy(value, off, value, off + n, count - off);
        s.getChars(0, n, value, off);
        count += n;
        return this;
    }
    public StringBuffer insert(int off, char[] v) { return insert(off, new String(v)); }
    public StringBuffer insert(int off, Object o) { return insert(off, String.valueOf(o)); }
    public StringBuffer insert(int off, boolean b) { return insert(off, String.valueOf(b)); }
    public StringBuffer insert(int off, char c) { return insert(off, String.valueOf(c)); }
    public StringBuffer insert(int off, int i) { return insert(off, String.valueOf(i)); }
    public StringBuffer insert(int off, long l) { return insert(off, String.valueOf(l)); }
    public StringBuffer insert(int off, float f) { return insert(off, String.valueOf(f)); }
    public StringBuffer insert(int off, double d) { return insert(off, String.valueOf(d)); }
    public synchronized StringBuffer replace(int s, int e, String str) { delete(s, e); return insert(s, str); }
    public synchronized StringBuffer reverse() {
        for (int i = 0, j = count - 1; i < j; i++, j--) { char c = value[i]; value[i] = value[j]; value[j] = c; }
        return this;
    }
    public synchronized int indexOf(String s) { return toString().indexOf(s); }
    public synchronized int indexOf(String s, int f) { return toString().indexOf(s, f); }
    public synchronized int lastIndexOf(String s) { return toString().lastIndexOf(s); }
    public synchronized String substring(int b) { return substring(b, count); }
    public synchronized String substring(int b, int e) { if (b < 0 || e > count || b > e) throw new StringIndexOutOfBoundsException(); return new String(value, b, e - b); }
    public CharSequence subSequence(int b, int e) { return substring(b, e); }
    public synchronized String toString() { return new String(value, 0, count); }
}
