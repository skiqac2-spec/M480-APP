package java.lang;
public class StringIndexOutOfBoundsException extends java.lang.IndexOutOfBoundsException {
    public StringIndexOutOfBoundsException() { super(); }
    public StringIndexOutOfBoundsException(String s) { super(s); }
    public StringIndexOutOfBoundsException(int i) { super("Index: " + i); }
}
