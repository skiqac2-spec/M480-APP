package javax.microedition.lcdui;
public class Command {
    public static final int SCREEN = 1, BACK = 2, CANCEL = 3, OK = 4, HELP = 5, STOP = 6, EXIT = 7, ITEM = 8;
    String label, longLabel; int type, prio;
    public Command(String l, int t, int p) { label = l; type = t; prio = p; }
    public Command(String l, String ll, int t, int p) { label = l; longLabel = ll; type = t; prio = p; }
    public String getLabel() { return label; }
    public String getLongLabel() { return longLabel; }
    public int getCommandType() { return type; }
    public int getPriority() { return prio; }
}
