package java.lang;
public class ArrayIndexOutOfBoundsException extends java.lang.IndexOutOfBoundsException {
    public ArrayIndexOutOfBoundsException() { super(); }
    public ArrayIndexOutOfBoundsException(String s) { super(s); }
    public ArrayIndexOutOfBoundsException(int i) { super("Index: " + i); }
}
