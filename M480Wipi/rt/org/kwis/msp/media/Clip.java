package org.kwis.msp.media;
public class Clip extends BaseClip {
    String type;
    int volume = 100;
    int position, stopTime;
    PlayListener listener;
    public Clip(String type) { this.type = type; }
    public Clip(String type, int size) { this.type = type; }
    public Clip(String type, byte[] b) { this.type = type; if (b != null) setBuffer(b, b.length); }
    public Clip(String type, String resource) {
        this.type = type;
        byte[] b = m480.Img.resource(resource);
        if (b != null) setBuffer(b, b.length);
    }
    public void setBuffer(byte[] b, int len, boolean x) { setBuffer(b, len); }
    public String getType() { return type; }
    public boolean setVolume(int v) { volume = v; if (snd != 0) m480.Snd.volume(snd, v); return true; }
    public int getVolume() { return volume; }
    public void setListener(PlayListener l) { listener = l; }
    public boolean setPosition(int p) { position = p; return true; }
    public int getPosition() { return snd != 0 ? m480.Snd.position(snd) : position; }
    public boolean setStopTime(int t) { stopTime = t; return true; }
    public int getStopTime() { return stopTime; }
}
