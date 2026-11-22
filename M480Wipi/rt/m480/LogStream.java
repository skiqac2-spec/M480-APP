package m480;
public class LogStream extends java.io.OutputStream {
    private int kind;
    private byte[] buf = new byte[256];
    private int n;
    public LogStream(int k) { kind = k; }
    public synchronized void write(int b) {
        if (b == '\n' || n == buf.length) { flush(); if (b == '\n') return; }
        buf[n++] = (byte) b;
    }
    public synchronized void flush() { if (n > 0) { log0(kind, buf, n); n = 0; } }
    private static native void log0(int kind, byte[] b, int n);
}
