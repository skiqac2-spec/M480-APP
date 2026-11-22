package m480;
/* something that can be shown full screen: WIPI Display (card stack) or a MIDP Displayable */
public interface Screen {
    void paintScreen();
    void keyEvent(int type, int code);
    void pointerEvent(int type, int x, int y);
    void shown();
    void hidden();
}
