package com.skt.m;
public class AudioClip {
    int snd; String type; int vol = 100;
    AudioClip(String t) { type = t; }
    public void open(byte[] b, int off, int len) { close(); snd = m480.Snd.load(b, off, len); if (snd == 0) throw new UnsupportedFormatException(type); }
    public void close() { if (snd != 0) { m480.Snd.free(snd); snd = 0; } }
    public void play() { if (snd != 0) { m480.Snd.volume(snd, vol * AudioSystem.vol / 5); m480.Snd.play(snd, 1); } }
    public void loop() { if (snd != 0) { m480.Snd.volume(snd, vol * AudioSystem.vol / 5); m480.Snd.play(snd, -1); } }
    public void stop() { if (snd != 0) m480.Snd.stop(snd); }
    public void pause() { if (snd != 0) m480.Snd.pause(snd); }
    public void resume() { if (snd != 0) m480.Snd.resume(snd); }
    public void setVolume(int v) { vol = v; }
}
