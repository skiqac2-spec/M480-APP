package java.lang;
public final class Character {
    public static final int MIN_RADIX = 2;
    public static final int MAX_RADIX = 36;
    public static final char MIN_VALUE = '\u0000';
    public static final char MAX_VALUE = '￿';
    private final char value;
    public Character(char c) { value = c; }
    public char charValue() { return value; }
    public static Character valueOf(char c) { return new Character(c); }
    public static boolean isLowerCase(char c) { return (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7); }
    public static boolean isUpperCase(char c) { return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7); }
    public static boolean isDigit(char c) { return (c >= '0' && c <= '9') || (c >= 0xFF10 && c <= 0xFF19); }
    public static boolean isLetter(char c) { return isLowerCase(c) || isUpperCase(c) || (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0x3131 && c <= 0x318E) || (c >= 0x4E00 && c <= 0x9FFF); }
    public static boolean isLetterOrDigit(char c) { return isLetter(c) || isDigit(c); }
    public static boolean isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
    public static boolean isWhitespace(char c) { return isSpace(c) || c == 0x0B || (c >= 0x1C && c <= 0x1F); }
    public static char toLowerCase(char c) { return isUpperCase(c) ? (char) (c + 32) : c; }
    public static char toUpperCase(char c) { return isLowerCase(c) && c != 0xDF && c != 0xFF ? (char) (c - 32) : c; }
    public static int digit(char c, int radix) {
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        return d < radix ? d : -1;
    }
    public static char forDigit(int d, int radix) {
        if (d < 0 || d >= radix) return 0;
        return (char) (d < 10 ? '0' + d : 'a' + d - 10);
    }
    public String toString() { return String.valueOf(value); }
    public int hashCode() { return value; }
    public boolean equals(Object o) { return o instanceof Character && ((Character) o).value == value; }
}
