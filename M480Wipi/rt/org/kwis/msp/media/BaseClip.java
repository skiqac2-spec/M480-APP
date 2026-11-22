package org.kwis.msp.media;
public class BaseClip {
    int snd;
    byte[] data;
    int dlen;
    public BaseClip() {}
    public int putData(byte[] b, int off, int len) {
        byte[] n = new byte[dlen + len];
        if (data != null) System.arraycopy(data, 0, n, 0, dlen);
        System.arraycopy(b, off, n, dlen, len);
        data = n; dlen += len;
        reload();
        return len;
    }
    public boolean setBuffer(byte[] b, int len) { data = b; dlen = len; reload(); return true; }
    public void clearData() { data = null; dlen = 0; reload(); }
    public int availableDataSize() { return 1 << 20; }
    void reload() {
        if (snd != 0) { m480.Snd.free(snd); snd = 0; }
        if (data != null && dlen > 0) snd = m480.Snd.load(data, 0, dlen);
    }
    protected void finalize() { if (snd != 0) m480.Snd.free(snd); }
}
