package java.io;
public class DataOutputStream extends OutputStream implements DataOutput {
    protected OutputStream out;
    protected int written;
    public DataOutputStream(OutputStream o) { out = o; }
    public void write(int b) throws IOException { out.write(b); written++; }
    public void write(byte[] b, int off, int len) throws IOException { out.write(b, off, len); written += len; }
    public void flush() throws IOException { out.flush(); }
    public void close() throws IOException { out.close(); }
    public final void writeBoolean(boolean v) throws IOException { write(v ? 1 : 0); }
    public final void writeByte(int v) throws IOException { write(v); }
    public final void writeShort(int v) throws IOException { write(v >>> 8); write(v); }
    public final void writeChar(int v) throws IOException { write(v >>> 8); write(v); }
    public final void writeInt(int v) throws IOException { write(v >>> 24); write(v >>> 16); write(v >>> 8); write(v); }
    public final void writeLong(long v) throws IOException { writeInt((int) (v >>> 32)); writeInt((int) v); }
    public final void writeFloat(float v) throws IOException { writeInt(Float.floatToIntBits(v)); }
    public final void writeDouble(double v) throws IOException { writeLong(Double.doubleToLongBits(v)); }
    public final void writeChars(String s) throws IOException { for (int i = 0; i < s.length(); i++) writeChar(s.charAt(i)); }
    public final void writeUTF(String s) throws IOException {
        int n = s.length(), len = 0;
        for (int i = 0; i < n; i++) { char c = s.charAt(i); len += (c >= 1 && c < 0x80) ? 1 : c < 0x800 ? 2 : 3; }
        if (len > 65535) throw new UTFDataFormatException();
        byte[] b = new byte[len + 2];
        int p = 0;
        b[p++] = (byte) (len >>> 8); b[p++] = (byte) len;
        for (int i = 0; i < n; i++) {
            char c = s.charAt(i);
            if (c >= 1 && c < 0x80) b[p++] = (byte) c;
            else if (c < 0x800) { b[p++] = (byte) (0xC0 | (c >> 6)); b[p++] = (byte) (0x80 | (c & 0x3F)); }
            else { b[p++] = (byte) (0xE0 | (c >> 12)); b[p++] = (byte) (0x80 | ((c >> 6) & 0x3F)); b[p++] = (byte) (0x80 | (c & 0x3F)); }
        }
        write(b, 0, b.length);
    }
    public final int size() { return written; }
}
