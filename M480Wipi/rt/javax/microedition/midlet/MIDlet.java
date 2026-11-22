package javax.microedition.midlet;
public abstract class MIDlet {
    private static MIDlet active;
    private boolean destroyed;
    protected MIDlet() { active = this; }
    protected abstract void startApp() throws MIDletStateChangeException;
    protected abstract void pauseApp();
    protected abstract void destroyApp(boolean unconditional) throws MIDletStateChangeException;
    public final String getAppProperty(String key) { return m480.Sys.prop("app." + key); }
    public final void notifyDestroyed() { if (!destroyed) { destroyed = true; m480.Sys.quit(); } }
    public final void notifyPaused() {}
    public final void resumeRequest() {}
    public final boolean platformRequest(String url) { return false; }
    public final int checkPermission(String p) { return 1; }
    public static void launch(String mc) throws Exception {
        final MIDlet m = (MIDlet) Class.forName(mc).newInstance();
        active = m;
        m480.Sys.app = new m480.App() {
            public void pause() { m.pauseApp(); }
            public void resume() { try { m.startApp(); } catch (Exception e) {} }
            public void destroy() { try { m.destroyApp(true); } catch (Exception e) {} }
        };
        m.startApp();
    }
}
