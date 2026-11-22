package java.io;
public class OutputStreamWriter extends Writer {
    private OutputStream out;
    private String enc;
    public OutputStreamWriter(OutputStream o) { out = o; }
    public OutputStreamWriter(OutputStream o, String e) throws UnsupportedEncodingException { out = o; enc = e; }
    public void write(char[] c, int off, int len) throws IOException {
        String s = new String(c, off, len);
        byte[] b = enc == null ? s.getBytes() : s.getBytes(enc);
        out.write(b, 0, b.length);
    }
    public void flush() throws IOException { out.flush(); }
    public void close() throws IOException { out.close(); }
}
