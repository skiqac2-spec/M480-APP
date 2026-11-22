package javax.microedition.io;
import java.io.*;
public class Connector {
    public static final int READ = 1, WRITE = 2, READ_WRITE = 3;
    public static Connection open(String n) throws IOException { throw new ConnectionNotFoundException(n); }
    public static Connection open(String n, int m) throws IOException { throw new ConnectionNotFoundException(n); }
    public static Connection open(String n, int m, boolean t) throws IOException { throw new ConnectionNotFoundException(n); }
    public static InputStream openInputStream(String n) throws IOException { throw new ConnectionNotFoundException(n); }
    public static DataInputStream openDataInputStream(String n) throws IOException { throw new ConnectionNotFoundException(n); }
    public static OutputStream openOutputStream(String n) throws IOException { throw new ConnectionNotFoundException(n); }
    public static DataOutputStream openDataOutputStream(String n) throws IOException { throw new ConnectionNotFoundException(n); }
}
