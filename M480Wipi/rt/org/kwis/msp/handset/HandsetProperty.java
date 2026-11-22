package org.kwis.msp.handset;
public class HandsetProperty {
    public static String getSystemProperty(String k) { String v = m480.Sys.prop(k); return v != null ? v : System.getProperty(k); }
    public static boolean setSystemProperty(String k, String v) { return false; }
}
