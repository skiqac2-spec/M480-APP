package org.kwis.msp.lcdui;
public class InputMethodHandler {
    public static final int MODE_NUMERIC = 1, MODE_ALPHA = 2, MODE_KOREAN = 4, MODE_SYMBOL = 8;
    int mode; int allowed; InputMethodListener listener;
    public InputMethodHandler(int allowed) { this.allowed = allowed; mode = allowed; }
    public int getCurrentMode() { return mode; }
    public boolean setCurrentMode(int m) { mode = m; return true; }
    public void setInputMethodListener(InputMethodListener l) { listener = l; }
    public void hideSymbolCard() {}
}
