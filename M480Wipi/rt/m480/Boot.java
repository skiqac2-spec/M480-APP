package m480;
public final class Boot {
    public static void main(String[] args) {
        String mc = Sys.prop("m480.main");
        String type = Sys.prop("m480.type");
        System.out.println("boot " + type + " " + mc);
        Sys.eventThread = Thread.currentThread();
        try {
            if ("jlet".equals(type)) org.kwis.msp.lcdui.Jlet.launch(mc, args);
            else javax.microedition.midlet.MIDlet.launch(mc);
        } catch (Throwable t) {
            System.err.println("start failed: " + t);
            t.printStackTrace();
            Sys.fail(t.toString());
        }
        Sys.loop();
    }
}
