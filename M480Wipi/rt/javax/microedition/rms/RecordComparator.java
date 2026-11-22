package javax.microedition.rms;
public interface RecordComparator { int EQUIVALENT = 0, FOLLOWS = 1, PRECEDES = -1; int compare(byte[] a, byte[] b); }
