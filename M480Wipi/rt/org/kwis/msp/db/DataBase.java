package org.kwis.msp.db;
import java.util.Vector;
public class DataBase {
    public static final int PRIVATE = 1, SHARED = 2, SYSTEM = 4;
    private String name;
    private int recordSize;
    private Vector recs;           /* byte[] or null (deleted); id = index + 1 */
    private boolean open;
    private long modified;
    private DataBase(String n, int rs) { name = n; recordSize = rs; }
    public static DataBase openDataBase(String name, int recSize, boolean create) throws DataBaseException { return openDataBase(name, recSize, create, PRIVATE); }
    public static DataBase openDataBase(String name, int recSize, boolean create, int mode) throws DataBaseException {
        DataBase db = new DataBase(name, recSize);
        byte[] b = m480.Store.load("db", name);
        if (b == null && !create) throw new DataBaseException("not found: " + name);
        db.recs = new Vector();
        if (b != null) {
            try {
                java.io.DataInputStream in = new java.io.DataInputStream(new java.io.ByteArrayInputStream(b));
                int rs = in.readInt();
                if (recSize <= 0) db.recordSize = rs;
                int n = in.readInt();
                for (int i = 0; i < n; i++) {
                    int l = in.readInt();
                    if (l < 0) { db.recs.addElement(null); continue; }
                    byte[] r = new byte[l];
                    in.readFully(r);
                    db.recs.addElement(r);
                }
            } catch (java.io.IOException e) { throw new DataBaseException("corrupt " + name); }
        } else db.save();
        db.open = true;
        return db;
    }
    private void save() {
        try {
            java.io.ByteArrayOutputStream bo = new java.io.ByteArrayOutputStream();
            java.io.DataOutputStream out = new java.io.DataOutputStream(bo);
            out.writeInt(recordSize);
            out.writeInt(recs.size());
            for (int i = 0; i < recs.size(); i++) {
                byte[] r = (byte[]) recs.elementAt(i);
                if (r == null) out.writeInt(-1); else { out.writeInt(r.length); out.write(r); }
            }
            byte[] d = bo.toByteArray();
            m480.Store.save("db", name, d, d.length);
            modified = System.currentTimeMillis();
        } catch (java.io.IOException e) {}
    }
    private void check() throws DataBaseException { if (!open) throw new DataBaseException("closed"); }
    private byte[] get(int id) throws DataBaseRecordException {
        if (id < 1 || id > recs.size() || recs.elementAt(id - 1) == null) throw new DataBaseRecordException("bad record " + id);
        return (byte[]) recs.elementAt(id - 1);
    }
    public synchronized int getNumberOfRecords() throws DataBaseException {
        check();
        int n = 0;
        for (int i = 0; i < recs.size(); i++) if (recs.elementAt(i) != null) n++;
        return n;
    }
    public void closeDataBase() throws DataBaseException { open = false; }
    public synchronized int insertRecord(byte[] b) throws DataBaseException { return insertRecord(b, 0, b.length); }
    public synchronized int insertRecord(byte[] b, int off, int len) throws DataBaseException {
        check();
        byte[] r = new byte[len];
        System.arraycopy(b, off, r, 0, len);
        recs.addElement(r);
        save();
        return recs.size();
    }
    public synchronized byte[] selectRecord(int id) throws DataBaseException {
        check();
        byte[] r = get(id), c = new byte[r.length];
        System.arraycopy(r, 0, c, 0, r.length);
        return c;
    }
    public synchronized void selectRecord(int id, byte[] buf, int off) throws DataBaseException {
        check();
        byte[] r = get(id);
        System.arraycopy(r, 0, buf, off, Math.min(r.length, buf.length - off));
    }
    public synchronized void updateRecord(int id, byte[] b) throws DataBaseException { updateRecord(id, b, 0, b.length); }
    public synchronized void updateRecord(int id, byte[] b, int off, int len) throws DataBaseException {
        check();
        get(id);
        byte[] r = new byte[len];
        System.arraycopy(b, off, r, 0, len);
        recs.setElementAt(r, id - 1);
        save();
    }
    public synchronized void deleteRecord(int id) throws DataBaseException { check(); get(id); recs.setElementAt(null, id - 1); save(); }
    public synchronized int[] sortRecord(DataFilter f, DataComparator c) throws DataBaseException {
        check();
        Vector ids = new Vector();
        for (int i = 0; i < recs.size(); i++) {
            byte[] r = (byte[]) recs.elementAt(i);
            if (r != null && (f == null || f.matches(r))) ids.addElement(new Integer(i + 1));
        }
        int[] out = new int[ids.size()];
        for (int i = 0; i < out.length; i++) out[i] = ((Integer) ids.elementAt(i)).intValue();
        if (c != null) {
            for (int i = 1; i < out.length; i++) {
                int v = out[i], j = i - 1;
                while (j >= 0 && c.compare((byte[]) recs.elementAt(out[j] - 1), (byte[]) recs.elementAt(v - 1)) > 0) { out[j + 1] = out[j]; j--; }
                out[j + 1] = v;
            }
        }
        return out;
    }
    public static void deleteDataBase(String name) throws DataBaseException { m480.Store.remove("db", name); }
    public static void deleteDataBase(String name, int mode) throws DataBaseException { deleteDataBase(name); }
    public static String[] listDataBases() { return m480.Store.list("db"); }
    public static int getAccessMode(String name) { return PRIVATE; }
    public String getDataBaseName() { return name; }
    public int getDataBaseSize() { int s = 0; for (int i = 0; i < recs.size(); i++) { byte[] r = (byte[]) recs.elementAt(i); if (r != null) s += r.length; } return s; }
    public int getRecordSize() { return recordSize; }
    public int getSizeAvailable() { return 1 << 20; }
    public long getLastModified() { return modified; }
}
