package com.skt.m;
public class ProgressBar {
    int max = 100, val; String label;
    public ProgressBar(String l) { label = l; }
    public int getMaxValue() { return max; }
    public void setMaxValue(int m) { max = m; }
    public int getValue() { return val; }
    public void setValue(int v) { val = v; }
}
