package java.io;
public class InputStreamReader extends Reader {
    private InputStream in;
    private int enc;
    private int pending = -1;
    public InputStreamReader(InputStream i) { in = i; enc = 0; }
    public InputStreamReader(InputStream i, String e) throws UnsupportedEncodingException {
        in = i;
        String u = e.toUpperCase();
        enc = (u.equals("UTF-8") || u.equals("UTF8")) ? 1 : (u.equals("ISO-8859-1") || u.equals("ISO8859_1") || u.equals("ASCII")) ? 2 : 0;
    }
    private int readChar() throws IOException {
        int a = in.read();
        if (a < 0) return -1;
        if (enc == 2 || a < 0x80) return a;
        if (enc == 1) {
            if ((a & 0xE0) == 0xC0) return ((a & 0x1F) << 6) | (in.read() & 0x3F);
            if ((a & 0xF0) == 0xE0) { int b = in.read(); return ((a & 0x0F) << 12) | ((b & 0x3F) << 6) | (in.read() & 0x3F); }
            return a;
        }
        int b = in.read();
        if (b < 0) return a;
        byte[] t = { (byte) a, (byte) b };
        String s = new String(t);
        return s.length() > 0 ? s.charAt(0) : '?';
    }
    public int read() throws IOException { return readChar(); }
    public int read(char[] c, int off, int len) throws IOException {
        int n = 0;
        while (n < len) { int ch = readChar(); if (ch < 0) break; c[off + n++] = (char) ch; if (in.available() <= 0 && n > 0) break; }
        return n == 0 && len > 0 ? -1 : n;
    }
    public boolean ready() throws IOException { return in.available() > 0; }
    public void close() throws IOException { in.close(); }
}
