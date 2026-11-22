package java.io;
public class PrintStream extends OutputStream {
    private OutputStream out;
    private boolean error;
    public PrintStream(OutputStream o) { out = o; }
    public void write(int b) { try { out.write(b); } catch (IOException e) { error = true; } }
    public void write(byte[] b, int off, int len) { try { out.write(b, off, len); } catch (IOException e) { error = true; } }
    public void flush() { try { out.flush(); } catch (IOException e) { error = true; } }
    public void close() { try { out.close(); } catch (IOException e) { error = true; } }
    public boolean checkError() { return error; }
    public synchronized void print(String s) { if (s == null) s = "null"; byte[] b = s.getBytes(); write(b, 0, b.length); }
    public void print(Object o) { print(String.valueOf(o)); }
    public void print(char[] c) { print(new String(c)); }
    public void print(boolean b) { print(String.valueOf(b)); }
    public void print(char c) { print(String.valueOf(c)); }
    public void print(int i) { print(String.valueOf(i)); }
    public void print(long l) { print(String.valueOf(l)); }
    public void print(float f) { print(String.valueOf(f)); }
    public void print(double d) { print(String.valueOf(d)); }
    public synchronized void println() { write('\n'); flush(); }
    public synchronized void println(String s) { print(s); println(); }
    public void println(Object o) { println(String.valueOf(o)); }
    public void println(char[] c) { println(new String(c)); }
    public void println(boolean b) { println(String.valueOf(b)); }
    public void println(char c) { println(String.valueOf(c)); }
    public void println(int i) { println(String.valueOf(i)); }
    public void println(long l) { println(String.valueOf(l)); }
    public void println(float f) { println(String.valueOf(f)); }
    public void println(double d) { println(String.valueOf(d)); }
}
