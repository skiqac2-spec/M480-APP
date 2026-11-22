package java.lang.ref;
public abstract class Reference {
    private Object referent;
    Reference(Object o) { referent = o; }
    public Object get() { return referent; }
    public void clear() { referent = null; }
}
