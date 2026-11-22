package java.io;
public class DataInputStream extends InputStream implements DataInput {
    protected InputStream in;
    public DataInputStream(InputStream i) { in = i; }
    public int read() throws IOException { return in.read(); }
    public final int read(byte[] b) throws IOException { return in.read(b, 0, b.length); }
    public final int read(byte[] b, int off, int len) throws IOException { return in.read(b, off, len); }
    public final void readFully(byte[] b) throws IOException { readFully(b, 0, b.length); }
    public final void readFully(byte[] b, int off, int len) throws IOException {
        int n = 0;
        while (n < len) { int c = in.read(b, off + n, len - n); if (c < 0) throw new EOFException(); n += c; }
    }
    public final int skipBytes(int n) throws IOException { int k = 0; while (k < n) { long s = in.skip(n - k); if (s <= 0) break; k += (int) s; } return k; }
    private int rd() throws IOException { int c = in.read(); if (c < 0) throw new EOFException(); return c; }
    public final boolean readBoolean() throws IOException { return rd() != 0; }
    public final byte readByte() throws IOException { return (byte) rd(); }
    public final int readUnsignedByte() throws IOException { return rd(); }
    public final short readShort() throws IOException { int a = rd(), b = rd(); return (short) ((a << 8) | b); }
    public final int readUnsignedShort() throws IOException { int a = rd(), b = rd(); return (a << 8) | b; }
    public final char readChar() throws IOException { int a = rd(), b = rd(); return (char) ((a << 8) | b); }
    public final int readInt() throws IOException { int a = rd(), b = rd(), c = rd(), d = rd(); return (a << 24) | (b << 16) | (c << 8) | d; }
    public final long readLong() throws IOException { return ((long) readInt() << 32) | (readInt() & 0xFFFFFFFFL); }
    public final float readFloat() throws IOException { return Float.intBitsToFloat(readInt()); }
    public final double readDouble() throws IOException { return Double.longBitsToDouble(readLong()); }
    public final String readUTF() throws IOException { return readUTF(this); }
    public static final String readUTF(DataInput in) throws IOException {
        int len = in.readUnsignedShort();
        byte[] b = new byte[len];
        in.readFully(b, 0, len);
        char[] c = new char[len];
        int n = 0, i = 0;
        while (i < len) {
            int x = b[i] & 0xFF;
            if (x < 0x80) { c[n++] = (char) x; i++; }
            else if ((x & 0xE0) == 0xC0) { if (i + 1 >= len) throw new UTFDataFormatException(); c[n++] = (char) (((x & 0x1F) << 6) | (b[i + 1] & 0x3F)); i += 2; }
            else if ((x & 0xF0) == 0xE0) { if (i + 2 >= len) throw new UTFDataFormatException(); c[n++] = (char) (((x & 0x0F) << 12) | ((b[i + 1] & 0x3F) << 6) | (b[i + 2] & 0x3F)); i += 3; }
            else throw new UTFDataFormatException();
        }
        return new String(c, 0, n);
    }
    public long skip(long n) throws IOException { return in.skip(n); }
    public int available() throws IOException { return in.available(); }
    public void close() throws IOException { in.close(); }
    public synchronized void mark(int l) { in.mark(l); }
    public synchronized void reset() throws IOException { in.reset(); }
    public boolean markSupported() { return in.markSupported(); }
}
