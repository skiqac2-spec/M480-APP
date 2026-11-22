package java.lang;
import java.io.InputStream;
import java.io.ByteArrayInputStream;
public final class Class {
    private int vmref;
    private Class() {}
    public native String getName();
    private static native Class find0(String name);
    private native void init0();
    private native Object alloc0();
    private static native void construct0(Object o);
    public static Class forName(String name) throws ClassNotFoundException {
        if (name == null) throw new NullPointerException();
        Class c = find0(name.replace('.', '/'));
        if (c == null) throw new ClassNotFoundException(name);
        c.init0();
        return c;
    }
    public Object newInstance() throws InstantiationException, IllegalAccessException {
        if (isInterface() || isArray()) throw new InstantiationException(getName());
        init0();
        Object o = alloc0();
        if (o == null) throw new InstantiationException(getName());
        construct0(o);
        return o;
    }
    public native boolean isInstance(Object o);
    public native boolean isAssignableFrom(Class c);
    public native boolean isInterface();
    public native boolean isArray();
    public native Class getSuperclass();
    private static native byte[] resource0(String name);
    public InputStream getResourceAsStream(String name) {
        if (name == null) return null;
        if (!name.startsWith("/")) {
            String n = getName();
            int i = n.lastIndexOf('.');
            if (i >= 0) name = n.substring(0, i).replace('.', '/') + "/" + name;
        } else name = name.substring(1);
        byte[] b = resource0(name);
        return b == null ? null : new ByteArrayInputStream(b);
    }
    public String toString() { return (isInterface() ? "interface " : "class ") + getName(); }
}
