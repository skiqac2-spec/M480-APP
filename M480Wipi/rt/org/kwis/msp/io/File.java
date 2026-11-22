package org.kwis.msp.io;
import java.io.*;
public class File {
    public static final int READ_ONLY = 1, WRITE = 2, READ_WRITE = 3, WRITE_TRUNC = 4, CREATE = 8;
    private String name;
    private byte[] buf;
    private int len, pos;
    private boolean dirty, closed;
    public File(String n, int mode) throws IOException { this(n, mode, 1); }
    public File(String n, int mode, int access) throws IOException {
        name = FileSystem.n(n);
        byte[] d = m480.Store.load("file", name);
        if (d == null) {
            if (mode == READ_ONLY) throw new IOException("not found " + n);
            d = new byte[0];
            dirty = true;
        }
        if (mode == WRITE_TRUNC) { d = new byte[0]; dirty = true; }
        buf = d; len = d.length;
    }
    private void grow(int n) { if (n > buf.length) { byte[] b = new byte[Math.max(n, buf.length * 2 + 64)]; System.arraycopy(buf, 0, b, 0, len); buf = b; } }
    public int write(byte[] b) throws IOException { return write(b, 0, b.length); }
    public int write(byte[] b, int off, int n) throws IOException { grow(pos + n); System.arraycopy(b, off, buf, pos, n); pos += n; if (pos > len) len = pos; dirty = true; return n; }
    public int write(int b) throws IOException { grow(pos + 1); buf[pos++] = (byte) b; if (pos > len) len = pos; dirty = true; return 1; }
    public int read(byte[] b) throws IOException { return read(b, 0, b.length); }
    public int read(byte[] b, int off, int n) throws IOException { if (pos >= len) return -1; if (n > len - pos) n = len - pos; System.arraycopy(buf, pos, b, off, n); pos += n; return n; }
    public int read() throws IOException { return pos < len ? buf[pos++] & 255 : -1; }
    public void seek(int p) throws IOException { pos = p < 0 ? 0 : p; }
    public int tell() { return pos; }
    public int sizeOf() { return len; }
    public void close() throws IOException { if (!closed && dirty) m480.Store.save("file", name, buf, len); closed = true; }
    public InputStream openInputStream() { return new ByteArrayInputStream(buf, 0, len); }
    public DataInputStream openDataInputStream() { return new DataInputStream(openInputStream()); }
    public OutputStream openOutputStream() {
        return new OutputStream() {
            public void write(int b) throws IOException { File.this.write(b); }
            public void write(byte[] b, int off, int n) throws IOException { File.this.write(b, off, n); }
            public void close() throws IOException { File.this.close(); }
        };
    }
    public DataOutputStream openDataOutputStream() { return new DataOutputStream(openOutputStream()); }
}
