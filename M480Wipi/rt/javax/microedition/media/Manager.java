package javax.microedition.media;
import java.io.*;
import java.util.Vector;
import javax.microedition.media.control.VolumeControl;
public final class Manager {
    public static final String TONE_DEVICE_LOCATOR = "device://tone", MIDI_DEVICE_LOCATOR = "device://midi";
    public static Player createPlayer(InputStream in, String type) throws IOException, MediaException {
        if (in == null) throw new IllegalArgumentException();
        byte[] b = m480.Img.readAll(in);
        return new P(b, type);
    }
    public static Player createPlayer(String locator) throws IOException, MediaException {
        if (locator.startsWith("device://")) return new P(null, "audio/x-tone-seq");
        String n = locator;
        int i = n.indexOf("://");
        if (i >= 0) n = n.substring(i + 3);
        byte[] b = m480.Img.resource(n);
        if (b == null) throw new MediaException("not found " + locator);
        return new P(b, null);
    }
    public static String[] getSupportedContentTypes(String p) { return new String[] { "audio/x-wav", "audio/midi", "audio/mmf", "audio/x-smaf" }; }
    public static String[] getSupportedProtocols(String t) { return new String[] { "resource" }; }
    public static void playTone(int note, int ms, int vol) throws MediaException { m480.Snd.tone(note, ms, vol); }
    static class P implements Player, VolumeControl {
        int snd, state = UNREALIZED, loops = 1, level = 100; boolean muted; String type;
        Vector ls = new Vector();
        P(byte[] b, String t) { type = t; if (b != null && b.length > 0) snd = m480.Snd.load(b, 0, b.length); }
        public void realize() { if (state < REALIZED) state = REALIZED; }
        public void prefetch() { realize(); if (state < PREFETCHED) state = PREFETCHED; }
        public void start() { prefetch(); if (snd != 0) { m480.Snd.volume(snd, muted ? 0 : level); m480.Snd.play(snd, loops); } state = STARTED; fire(PlayerListener.STARTED, null); }
        public void stop() { if (snd != 0) m480.Snd.stop(snd); if (state == STARTED) state = PREFETCHED; fire(PlayerListener.STOPPED, null); }
        public void deallocate() { if (snd != 0) m480.Snd.stop(snd); if (state > REALIZED) state = REALIZED; }
        public void close() { if (snd != 0) { m480.Snd.free(snd); snd = 0; } state = CLOSED; fire(PlayerListener.CLOSED, null); }
        public long setMediaTime(long t) { return 0; }
        public long getMediaTime() { return snd != 0 ? m480.Snd.position(snd) * 1000L : 0; }
        public int getState() { if (state == STARTED && snd != 0 && !m480.Snd.playing(snd)) state = PREFETCHED; return state; }
        public long getDuration() { return snd != 0 ? m480.Snd.duration(snd) * 1000L : TIME_UNKNOWN; }
        public String getContentType() { return type; }
        public void setLoopCount(int c) { loops = c; }
        public void addPlayerListener(PlayerListener l) { if (l != null) ls.addElement(l); }
        public void removePlayerListener(PlayerListener l) { ls.removeElement(l); }
        void fire(String e, Object d) { for (int i = 0; i < ls.size(); i++) ((PlayerListener) ls.elementAt(i)).playerUpdate(this, e, d); }
        public Control[] getControls() { return new Control[] { this }; }
        public Control getControl(String t) { return t.indexOf("VolumeControl") >= 0 ? this : null; }
        public void setMute(boolean m) { muted = m; if (snd != 0) m480.Snd.volume(snd, m ? 0 : level); }
        public boolean isMuted() { return muted; }
        public int setLevel(int l) { level = l < 0 ? 0 : l > 100 ? 100 : l; if (snd != 0 && !muted) m480.Snd.volume(snd, level); return level; }
        public int getLevel() { return level; }
    }
}
