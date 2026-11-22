package com.skt.m;
public class Vibration { public static void start(int level, int ms) { m480.Sys.vibrate(ms); } public static void stop() { m480.Sys.vibrate(0); } }
