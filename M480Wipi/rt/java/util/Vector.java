package java.util;
public class Vector {
    protected Object[] elementData;
    protected int elementCount;
    protected int capacityIncrement;
    public Vector() { this(10); }
    public Vector(int cap) { this(cap, 0); }
    public Vector(int cap, int inc) { if (cap < 0) throw new IllegalArgumentException(); elementData = new Object[cap]; capacityIncrement = inc; }
    private void grow(int need) {
        if (need <= elementData.length) return;
        int n = capacityIncrement > 0 ? elementData.length + capacityIncrement : elementData.length * 2;
        if (n < need) n = need;
        Object[] d = new Object[n];
        System.arraycopy(elementData, 0, d, 0, elementCount);
        elementData = d;
    }
    public synchronized void copyInto(Object[] a) { System.arraycopy(elementData, 0, a, 0, elementCount); }
    public synchronized void trimToSize() { if (elementCount < elementData.length) { Object[] d = new Object[elementCount]; System.arraycopy(elementData, 0, d, 0, elementCount); elementData = d; } }
    public synchronized void ensureCapacity(int n) { grow(n); }
    public synchronized void setSize(int n) { grow(n); for (int i = n; i < elementCount; i++) elementData[i] = null; elementCount = n; }
    public synchronized int capacity() { return elementData.length; }
    public synchronized int size() { return elementCount; }
    public synchronized boolean isEmpty() { return elementCount == 0; }
    public synchronized Enumeration elements() {
        return new Enumeration() {
            int i = 0;
            public boolean hasMoreElements() { return i < elementCount; }
            public Object nextElement() {
                synchronized (Vector.this) { if (i < elementCount) return elementData[i++]; }
                throw new NoSuchElementException();
            }
        };
    }
    public boolean contains(Object o) { return indexOf(o, 0) >= 0; }
    public int indexOf(Object o) { return indexOf(o, 0); }
    public synchronized int indexOf(Object o, int from) {
        for (int i = from; i < elementCount; i++) if (o == null ? elementData[i] == null : o.equals(elementData[i])) return i;
        return -1;
    }
    public int lastIndexOf(Object o) { return lastIndexOf(o, elementCount - 1); }
    public synchronized int lastIndexOf(Object o, int from) {
        if (from >= elementCount) throw new IndexOutOfBoundsException();
        for (int i = from; i >= 0; i--) if (o == null ? elementData[i] == null : o.equals(elementData[i])) return i;
        return -1;
    }
    public synchronized Object elementAt(int i) { if (i >= elementCount || i < 0) throw new ArrayIndexOutOfBoundsException(i); return elementData[i]; }
    public synchronized Object firstElement() { if (elementCount == 0) throw new NoSuchElementException(); return elementData[0]; }
    public synchronized Object lastElement() { if (elementCount == 0) throw new NoSuchElementException(); return elementData[elementCount - 1]; }
    public synchronized void setElementAt(Object o, int i) { if (i >= elementCount || i < 0) throw new ArrayIndexOutOfBoundsException(i); elementData[i] = o; }
    public synchronized void removeElementAt(int i) {
        if (i >= elementCount || i < 0) throw new ArrayIndexOutOfBoundsException(i);
        int n = elementCount - i - 1;
        if (n > 0) System.arraycopy(elementData, i + 1, elementData, i, n);
        elementData[--elementCount] = null;
    }
    public synchronized void insertElementAt(Object o, int i) {
        if (i > elementCount || i < 0) throw new ArrayIndexOutOfBoundsException(i);
        grow(elementCount + 1);
        System.arraycopy(elementData, i, elementData, i + 1, elementCount - i);
        elementData[i] = o;
        elementCount++;
    }
    public synchronized void addElement(Object o) { grow(elementCount + 1); elementData[elementCount++] = o; }
    public synchronized boolean removeElement(Object o) { int i = indexOf(o); if (i >= 0) { removeElementAt(i); return true; } return false; }
    public synchronized void removeAllElements() { for (int i = 0; i < elementCount; i++) elementData[i] = null; elementCount = 0; }
    /* Collections-style names some games use */
    public boolean add(Object o) { addElement(o); return true; }
    public void add(int i, Object o) { insertElementAt(o, i); }
    public Object get(int i) { return elementAt(i); }
    public synchronized Object set(int i, Object o) { Object old = elementAt(i); elementData[i] = o; return old; }
    public synchronized Object remove(int i) { Object old = elementAt(i); removeElementAt(i); return old; }
    public boolean remove(Object o) { return removeElement(o); }
    public void clear() { removeAllElements(); }
    public synchronized String toString() {
        StringBuffer sb = new StringBuffer("[");
        for (int i = 0; i < elementCount; i++) { if (i > 0) sb.append(", "); sb.append(String.valueOf(elementData[i])); }
        return sb.append("]").toString();
    }
}
