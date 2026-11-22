package javax.microedition.rms;
import java.util.Vector;
import java.util.Hashtable;
import java.io.*;
public class RecordStore {
    public static final int AUTHMODE_PRIVATE = 0, AUTHMODE_ANY = 1;
    private static Hashtable open = new Hashtable();
    private String name;
    private Vector ids = new Vector(), data = new Vector();
    private int nextId = 1, version, opens;
    private long modified;
    private Vector listeners = new Vector();
    private RecordStore(String n) { name = n; }
    public static synchronized RecordStore openRecordStore(String name, boolean create) throws RecordStoreException {
        if (name == null || name.length() == 0 || name.length() > 32) throw new IllegalArgumentException();
        RecordStore rs = (RecordStore) open.get(name);
        if (rs != null) { rs.opens++; return rs; }
        byte[] b = m480.Store.load("rms", name);
        if (b == null && !create) throw new RecordStoreNotFoundException(name);
        rs = new RecordStore(name);
        if (b != null) {
            try {
                DataInputStream in = new DataInputStream(new ByteArrayInputStream(b));
                rs.nextId = in.readInt(); rs.version = in.readInt(); rs.modified = in.readLong();
                int n = in.readInt();
                for (int i = 0; i < n; i++) {
                    int id = in.readInt(), l = in.readInt();
                    byte[] r = new byte[l]; in.readFully(r);
                    rs.ids.addElement(new Integer(id)); rs.data.addElement(r);
                }
            } catch (IOException e) { throw new RecordStoreException("corrupt " + name); }
        } else rs.save();
        rs.opens = 1;
        open.put(name, rs);
        return rs;
    }
    public static RecordStore openRecordStore(String name, boolean create, int auth, boolean writable) throws RecordStoreException { return openRecordStore(name, create); }
    public static RecordStore openRecordStore(String name, String vendor, String suite) throws RecordStoreException { return openRecordStore(name, false); }
    public static synchronized void deleteRecordStore(String name) throws RecordStoreException {
        if (open.get(name) != null) throw new RecordStoreException("open");
        if (!m480.Store.exists("rms", name)) throw new RecordStoreNotFoundException(name);
        m480.Store.remove("rms", name);
    }
    public static String[] listRecordStores() { String[] s = m480.Store.list("rms"); return s == null || s.length == 0 ? null : s; }
    private void save() {
        try {
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            DataOutputStream out = new DataOutputStream(bo);
            out.writeInt(nextId); out.writeInt(version); out.writeLong(modified); out.writeInt(ids.size());
            for (int i = 0; i < ids.size(); i++) { byte[] r = (byte[]) data.elementAt(i); out.writeInt(((Integer) ids.elementAt(i)).intValue()); out.writeInt(r.length); out.write(r); }
            byte[] d = bo.toByteArray();
            m480.Store.save("rms", name, d, d.length);
        } catch (IOException e) {}
    }
    private void check() throws RecordStoreNotOpenException { if (opens <= 0) throw new RecordStoreNotOpenException(); }
    private int index(int id) throws InvalidRecordIDException {
        for (int i = 0; i < ids.size(); i++) if (((Integer) ids.elementAt(i)).intValue() == id) return i;
        throw new InvalidRecordIDException("id " + id);
    }
    private void touch() { version++; modified = System.currentTimeMillis(); save(); }
    public synchronized void closeRecordStore() throws RecordStoreException {
        check();
        if (--opens <= 0) { synchronized (RecordStore.class) { open.remove(name); } listeners.removeAllElements(); }
    }
    public String getName() throws RecordStoreNotOpenException { check(); return name; }
    public int getVersion() throws RecordStoreNotOpenException { check(); return version; }
    public int getNumRecords() throws RecordStoreNotOpenException { check(); return ids.size(); }
    public int getSize() throws RecordStoreNotOpenException { check(); int s = 0; for (int i = 0; i < data.size(); i++) s += ((byte[]) data.elementAt(i)).length + 8; return s; }
    public int getSizeAvailable() throws RecordStoreNotOpenException { check(); return 512 * 1024; }
    public long getLastModified() throws RecordStoreNotOpenException { check(); return modified; }
    public int getNextRecordID() throws RecordStoreException { check(); return nextId; }
    public void addRecordListener(RecordListener l) { if (!listeners.contains(l)) listeners.addElement(l); }
    public void removeRecordListener(RecordListener l) { listeners.removeElement(l); }
    public void setMode(int a, boolean w) {}
    public synchronized int addRecord(byte[] b, int off, int len) throws RecordStoreException {
        check();
        byte[] r = new byte[len];
        if (b != null) System.arraycopy(b, off, r, 0, len);
        int id = nextId++;
        ids.addElement(new Integer(id)); data.addElement(r);
        touch();
        for (int i = 0; i < listeners.size(); i++) ((RecordListener) listeners.elementAt(i)).recordAdded(this, id);
        return id;
    }
    public synchronized void deleteRecord(int id) throws RecordStoreException {
        check();
        int i = index(id);
        ids.removeElementAt(i); data.removeElementAt(i);
        touch();
        for (int k = 0; k < listeners.size(); k++) ((RecordListener) listeners.elementAt(k)).recordDeleted(this, id);
    }
    public synchronized int getRecordSize(int id) throws RecordStoreException { check(); return ((byte[]) data.elementAt(index(id))).length; }
    public synchronized int getRecord(int id, byte[] buf, int off) throws RecordStoreException {
        check();
        byte[] r = (byte[]) data.elementAt(index(id));
        if (off + r.length > buf.length) throw new ArrayIndexOutOfBoundsException();
        System.arraycopy(r, 0, buf, off, r.length);
        return r.length;
    }
    public synchronized byte[] getRecord(int id) throws RecordStoreException {
        check();
        byte[] r = (byte[]) data.elementAt(index(id));
        if (r.length == 0) return null;
        byte[] c = new byte[r.length];
        System.arraycopy(r, 0, c, 0, r.length);
        return c;
    }
    public synchronized void setRecord(int id, byte[] b, int off, int len) throws RecordStoreException {
        check();
        int i = index(id);
        byte[] r = new byte[len];
        if (b != null) System.arraycopy(b, off, r, 0, len);
        data.setElementAt(r, i);
        touch();
        for (int k = 0; k < listeners.size(); k++) ((RecordListener) listeners.elementAt(k)).recordChanged(this, id);
    }
    public synchronized RecordEnumeration enumerateRecords(final RecordFilter f, final RecordComparator c, boolean keep) throws RecordStoreNotOpenException {
        check();
        final RecordStore rs = this;
        return new RecordEnumeration() {
            int[] list; int pos = 0;
            { rebuild(); }
            public void rebuild() {
                Vector v = new Vector();
                synchronized (rs) {
                    for (int i = 0; i < rs.ids.size(); i++) if (f == null || f.matches((byte[]) rs.data.elementAt(i))) v.addElement(rs.ids.elementAt(i));
                }
                list = new int[v.size()];
                for (int i = 0; i < list.length; i++) list[i] = ((Integer) v.elementAt(i)).intValue();
                if (c != null) {
                    for (int i = 1; i < list.length; i++) {
                        int x = list[i], j = i - 1;
                        try { while (j >= 0 && c.compare(rs.getRecord(list[j]), rs.getRecord(x)) == RecordComparator.FOLLOWS) { list[j + 1] = list[j]; j--; } } catch (RecordStoreException e) {}
                        list[j + 1] = x;
                    }
                }
                pos = 0;
            }
            public int numRecords() { return list.length; }
            public byte[] nextRecord() throws RecordStoreException { return rs.getRecord(nextRecordId()); }
            public int nextRecordId() throws InvalidRecordIDException { if (pos >= list.length) throw new InvalidRecordIDException(); return list[pos++]; }
            public byte[] previousRecord() throws RecordStoreException { return rs.getRecord(previousRecordId()); }
            public int previousRecordId() throws InvalidRecordIDException { if (pos <= 0) throw new InvalidRecordIDException(); return list[--pos]; }
            public boolean hasNextElement() { return pos < list.length; }
            public boolean hasPreviousElement() { return pos > 0; }
            public void reset() { pos = 0; }
            public void keepUpdated(boolean k) {}
            public boolean isKeptUpdated() { return false; }
            public void destroy() {}
        };
    }
}
