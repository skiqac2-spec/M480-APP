package org.kwis.msp.lcdui;
public abstract class Jlet {
    private static Jlet active;
    private EventQueue eq;
    private boolean destroyed;
    protected Jlet() { active = this; }
    public static Jlet getActiveJlet() { return active; }
    public static Jlet getCurrentJlet() { return active; }
    public EventQueue getEventQueue() { if (eq == null) eq = new EventQueue(this); return eq; }
    public String getAppProperty(String key) { return m480.Sys.prop("app." + key); }
    protected abstract void startApp(String[] args);
    protected abstract void pauseApp();
    protected abstract void resumeApp();
    protected abstract void destroyApp(boolean unconditional);
    public final void notifyDestroyed() {
        if (destroyed) return;
        destroyed = true;
        m480.Sys.quit();
    }
    public final void notifyPaused() {}
    public final void resumeRequest() {}
    public static void launch(String mc, String[] args) throws Exception {
        final Jlet j = (Jlet) Class.forName(mc).newInstance();
        active = j;
        m480.Sys.app = new m480.App() {
            public void pause() { j.pauseApp(); }
            public void resume() { j.resumeApp(); }
            public void destroy() { j.destroyApp(true); }
        };
        j.startApp(args == null ? new String[0] : args);
    }
}
