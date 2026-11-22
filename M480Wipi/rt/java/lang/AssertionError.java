package java.lang;
public class AssertionError extends java.lang.Error {
    public AssertionError() { super(); }
    public AssertionError(String s) { super(s); }
    public AssertionError(Object o) { super(String.valueOf(o)); }
}
