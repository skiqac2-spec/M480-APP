package m480;
public final class Gfx {
    /* draw an image to the screen at x,y and present immediately (GameCanvas.flushGraphics) */
    public static native void present(Object img, int x, int y);
}
