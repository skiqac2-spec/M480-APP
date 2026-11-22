package java.lang;
public class Throwable {
    private String message;
    private String trace;
    public Throwable() { trace = trace0(); }
    public Throwable(String m) { message = m; trace = trace0(); }
    public Throwable(String m, Throwable cause) { this(m); }
    private static native String trace0();
    public String getMessage() { return message; }
    public String getLocalizedMessage() { return message; }
    public String toString() {
        String n = getClass().getName();
        return message != null ? n + ": " + message : n;
    }
    public void printStackTrace() {
        System.err.println(toString());
        if (trace != null) System.err.println(trace);
    }
    public Throwable getCause() { return null; }
}
