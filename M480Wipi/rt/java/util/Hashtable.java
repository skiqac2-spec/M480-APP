package java.util;
public class Hashtable {
    private static class Entry { int hash; Object key, value; Entry next; }
    private Entry[] table;
    private int count, threshold;
    public Hashtable() { this(11); }
    public Hashtable(int cap) { if (cap <= 0) cap = 1; table = new Entry[cap]; threshold = cap * 3 / 4; }
    public int size() { return count; }
    public boolean isEmpty() { return count == 0; }
    private Enumeration enumerate(final boolean keys) {
        final Vector v = new Vector(count);
        synchronized (this) {
            for (int i = table.length - 1; i >= 0; i--)
                for (Entry e = table[i]; e != null; e = e.next) v.addElement(keys ? e.key : e.value);
        }
        return v.elements();
    }
    public Enumeration keys() { return enumerate(true); }
    public Enumeration elements() { return enumerate(false); }
    public synchronized boolean contains(Object value) {
        if (value == null) throw new NullPointerException();
        for (int i = table.length - 1; i >= 0; i--)
            for (Entry e = table[i]; e != null; e = e.next) if (e.value.equals(value)) return true;
        return false;
    }
    public boolean containsValue(Object v) { return contains(v); }
    public synchronized boolean containsKey(Object key) {
        int h = key.hashCode();
        for (Entry e = table[(h & 0x7FFFFFFF) % table.length]; e != null; e = e.next) if (e.hash == h && e.key.equals(key)) return true;
        return false;
    }
    public synchronized Object get(Object key) {
        int h = key.hashCode();
        for (Entry e = table[(h & 0x7FFFFFFF) % table.length]; e != null; e = e.next) if (e.hash == h && e.key.equals(key)) return e.value;
        return null;
    }
    protected void rehash() {
        Entry[] old = table;
        Entry[] nt = new Entry[old.length * 2 + 1];
        for (int i = old.length - 1; i >= 0; i--) {
            for (Entry e = old[i]; e != null;) {
                Entry n = e.next;
                int idx = (e.hash & 0x7FFFFFFF) % nt.length;
                e.next = nt[idx]; nt[idx] = e;
                e = n;
            }
        }
        table = nt;
        threshold = nt.length * 3 / 4;
    }
    public synchronized Object put(Object key, Object value) {
        if (value == null || key == null) throw new NullPointerException();
        int h = key.hashCode(), idx = (h & 0x7FFFFFFF) % table.length;
        for (Entry e = table[idx]; e != null; e = e.next)
            if (e.hash == h && e.key.equals(key)) { Object o = e.value; e.value = value; return o; }
        if (count >= threshold) { rehash(); idx = (h & 0x7FFFFFFF) % table.length; }
        Entry e = new Entry();
        e.hash = h; e.key = key; e.value = value; e.next = table[idx];
        table[idx] = e;
        count++;
        return null;
    }
    public synchronized Object remove(Object key) {
        int h = key.hashCode(), idx = (h & 0x7FFFFFFF) % table.length;
        for (Entry e = table[idx], p = null; e != null; p = e, e = e.next) {
            if (e.hash == h && e.key.equals(key)) {
                if (p != null) p.next = e.next; else table[idx] = e.next;
                count--;
                return e.value;
            }
        }
        return null;
    }
    public synchronized void clear() { for (int i = 0; i < table.length; i++) table[i] = null; count = 0; }
    public synchronized String toString() {
        StringBuffer sb = new StringBuffer("{");
        boolean first = true;
        for (int i = 0; i < table.length; i++)
            for (Entry e = table[i]; e != null; e = e.next) { if (!first) sb.append(", "); first = false; sb.append(String.valueOf(e.key)).append("=").append(String.valueOf(e.value)); }
        return sb.append("}").toString();
    }
}
