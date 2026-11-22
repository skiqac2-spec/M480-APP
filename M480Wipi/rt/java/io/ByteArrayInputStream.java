package java.io;
public class ByteArrayInputStream extends InputStream {
    protected byte[] buf;
    protected int pos, mark, count;
    public ByteArrayInputStream(byte[] b) { buf = b; count = b.length; }
    public ByteArrayInputStream(byte[] b, int off, int len) { buf = b; pos = off; mark = off; count = Math.min(off + len, b.length); }
    public synchronized int read() { return pos < count ? buf[pos++] & 0xFF : -1; }
    public synchronized int read(byte[] b, int off, int len) {
        if (b == null) throw new NullPointerException();
        if (off < 0 || len < 0 || off + len > b.length) throw new IndexOutOfBoundsException();
        if (pos >= count) return -1;
        if (pos + len > count) len = count - pos;
        if (len <= 0) return 0;
        System.arraycopy(buf, pos, b, off, len);
        pos += len;
        return len;
    }
    public synchronized long skip(long n) { if (pos + n > count) n = count - pos; if (n < 0) return 0; pos += n; return n; }
    public synchronized int available() { return count - pos; }
    public boolean markSupported() { return true; }
    public synchronized void mark(int l) { mark = pos; }
    public synchronized void reset() { pos = mark; }
    public void close() {}
}
