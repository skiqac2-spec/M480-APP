package org.kwis.msp.io;
import java.util.Vector;
public class FileSystem {
    public static final int PRIVATE_ACCESS = 1, SHARED_ACCESS = 2, SYSTEM_ACCESS = 4;
    public FileSystem() {}
    static String n(String s) { while (s.startsWith("/")) s = s.substring(1); return s.replace('/', '_'); }
    public static boolean exists(String f) { return m480.Store.exists("file", n(f)); }
    public static boolean exists(String f, int m) { return exists(f); }
    public static boolean isFile(String f) { return exists(f); }
    public static boolean isFile(String f, int m) { return exists(f); }
    public static boolean isDirectory(String f) { return false; }
    public static boolean isDirectory(String f, int m) { return false; }
    public static void mkdir(String d) {}
    public static void mkdir(String d, int m) {}
    public static void rmdir(String d) {}
    public static void rmdir(String d, int m) {}
    public static void remove(String f) { m480.Store.remove("file", n(f)); }
    public static void remove(String f, int m) { remove(f); }
    public static Vector list(String d) { Vector v = new Vector(); String[] a = m480.Store.list("file"); for (int i = 0; a != null && i < a.length; i++) v.addElement(a[i]); return v; }
    public static Vector list(String d, int m) { return list(d); }
    public static int available() { return 4 << 20; }
    public static int getMaxFilenameLength() { return 64; }
    public static byte[] toCString(String s) { byte[] b = s.getBytes(); byte[] r = new byte[b.length + 1]; System.arraycopy(b, 0, r, 0, b.length); return r; }
    public static int getCreationTime(String f) { return 0; }
    public static int getCreationTime(String f, int m) { return 0; }
    public static void rename(String a, String b) { byte[] d = m480.Store.load("file", n(a)); if (d != null) { m480.Store.save("file", n(b), d, d.length); remove(a); } }
    public static void rename(String a, String b, int m) { rename(a, b); }
}
