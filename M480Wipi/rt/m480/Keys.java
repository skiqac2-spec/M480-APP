package m480;
/* phone key ids (from the native side) -> application key codes, per carrier */
public final class Keys {
    public static final int NUM = 24;
    public static final int STAR = 10, POUND = 11, UP = 12, DOWN = 13, LEFT = 14, RIGHT = 15, FIRE = 16,
        SOFT1 = 17, SOFT2 = 18, CLEAR = 19, SEND = 20, END = 21, VOLUP = 22, VOLDN = 23;
    /* WIPI (org.kwis.msp.lcdui) codes - identical for all carriers */
    static final int[] WIPI = { 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 42, 35, -1, -2, -3, -4, -5, -6, -7, -16, -10, -11, -13, -14 };
    /* MIDP Canvas codes: SKT SK-VM style */
    static final int[] MIDP_SKT = { 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 42, 35, 141, 146, 142, 145, 148, 6, 7, 8, 10, -1, 13, 14 };
    /* MIDP Canvas codes: KTF / LGT (common J2ME layout) */
    static final int[] MIDP_STD = { 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 42, 35, -1, -2, -3, -4, -5, -6, -7, -8, -10, -11, -13, -14 };
    public static int carrier = Sys.carrier();
    public static int wipi(int id) { return id >= 0 && id < NUM ? WIPI[id] : 0; }
    public static int midp(int id) {
        if (id < 0 || id >= NUM) return 0;
        String o = Sys.prop("m480.midpkeys." + id);
        if (o != null) { try { return Integer.parseInt(o); } catch (Exception e) {} }
        return carrier == 0 ? MIDP_SKT[id] : MIDP_STD[id];
    }
    public static int midpToId(int code) {
        int[] t = carrier == 0 ? MIDP_SKT : MIDP_STD;
        for (int i = 0; i < NUM; i++) if (t[i] == code) return i;
        return -1;
    }
    public static int wipiToId(int code) {
        for (int i = 0; i < NUM; i++) if (WIPI[i] == code) return i;
        return -1;
    }
    /* MIDP game actions */
    public static int gameAction(int code) {
        int id = midpToId(code);
        switch (id) {
        case UP: case 2: return 1;
        case DOWN: case 8: return 6;
        case LEFT: case 4: return 2;
        case RIGHT: case 6: return 5;
        case FIRE: case 5: return 8;
        case 1: return 9;
        case 3: return 10;
        case 7: return 11;
        case 9: return 12;
        }
        return 0;
    }
    public static int keyForAction(int a) {
        switch (a) {
        case 1: return midp(UP);
        case 6: return midp(DOWN);
        case 2: return midp(LEFT);
        case 5: return midp(RIGHT);
        case 8: return midp(FIRE);
        case 9: return midp(1);
        case 10: return midp(3);
        case 11: return midp(7);
        case 12: return midp(9);
        }
        throw new IllegalArgumentException();
    }
    public static String name(int code, boolean wipi) {
        int id = wipi ? wipiToId(code) : midpToId(code);
        if (id >= 0 && id <= 9) return String.valueOf((char) ('0' + id));
        switch (id) {
        case STAR: return "*"; case POUND: return "#"; case UP: return "UP"; case DOWN: return "DOWN";
        case LEFT: return "LEFT"; case RIGHT: return "RIGHT"; case FIRE: return "SELECT";
        case SOFT1: return "SOFT1"; case SOFT2: return "SOFT2"; case CLEAR: return "CLR";
        case SEND: return "SEND"; case END: return "END";
        }
        return null;
    }
}
