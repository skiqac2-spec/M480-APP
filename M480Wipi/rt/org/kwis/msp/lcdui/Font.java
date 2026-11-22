package org.kwis.msp.lcdui;
public class Font {
    int fid;
    int face, style, size;
    public static final int FACE_SYSTEM = 0, FACE_MONOSPACE = 32, FACE_PROPORTIONAL = 64;
    public static final int STYLE_PLAIN = 0, STYLE_BOLD = 1, STYLE_ITALIC = 2, STYLE_UNDERLINED = 4;
    public static final int SIZE_SMALL = 8, SIZE_MEDIUM = 0, SIZE_LARGE = 16;
    private static Font def;
    private static Font[] cache = new Font[64];
    Font(int face, int style, int size) {
        this.face = face; this.style = style; this.size = size;
        fid = (m480.FontN.pixelSize(size) << 4) | (style & 7);
    }
    public static Font getDefaultFont() { if (def == null) def = getFont(FACE_SYSTEM, STYLE_PLAIN, SIZE_MEDIUM); return def; }
    public static Font getFont(int face, int style, int size) {
        int k = ((size == SIZE_SMALL ? 1 : size == SIZE_LARGE ? 2 : 0) * 3 + (face == FACE_MONOSPACE ? 1 : face == FACE_PROPORTIONAL ? 2 : 0)) * 8 + (style & 7);
        if (k < 0 || k >= 64) return new Font(face, style, size);
        if (cache[k] == null) cache[k] = new Font(face, style, size);
        return cache[k];
    }
    public int getHeight() { return m480.FontN.height(fid); }
    public int getBaselinePosition() { return m480.FontN.ascent(fid); }
    public int getFace() { return face; }
    public int getStyle() { return style; }
    public int getSize() { return size; }
    public boolean isBold() { return (style & 1) != 0; }
    public boolean isItalic() { return (style & 2) != 0; }
    public boolean isPlain() { return style == 0; }
    public boolean isUnderlined() { return (style & 4) != 0; }
    public int stringWidth(String s) { return m480.FontN.strWidth(fid, s, 0, s.length()); }
    public int substringWidth(String s, int off, int len) { return m480.FontN.strWidth(fid, s, off, len); }
    public int charWidth(char c) { char[] a = { c }; return m480.FontN.charsWidth(fid, a, 0, 1); }
    public int charsWidth(char[] c, int off, int len) { return m480.FontN.charsWidth(fid, c, off, len); }
}
