package java.io;
public class ByteArrayOutputStream extends OutputStream {
    protected byte[] buf;
    protected int count;
    public ByteArrayOutputStream() { this(32); }
    public ByteArrayOutputStream(int n) { buf = new byte[n < 1 ? 1 : n]; }
    private void grow(int need) { if (need > buf.length) { byte[] b = new byte[Math.max(buf.length * 2, need)]; System.arraycopy(buf, 0, b, 0, count); buf = b; } }
    public synchronized void write(int b) { grow(count + 1); buf[count++] = (byte) b; }
    public synchronized void write(byte[] b, int off, int len) {
        if (off < 0 || len < 0 || off + len > b.length) throw new IndexOutOfBoundsException();
        grow(count + len); System.arraycopy(b, off, buf, count, len); count += len;
    }
    public synchronized void reset() { count = 0; }
    public synchronized byte[] toByteArray() { byte[] b = new byte[count]; System.arraycopy(buf, 0, b, 0, count); return b; }
    public int size() { return count; }
    public String toString() { return new String(buf, 0, count); }
    public void close() {}
}
