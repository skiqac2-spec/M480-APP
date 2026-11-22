package javax.microedition.rms;
public interface RecordListener { void recordAdded(RecordStore s, int id); void recordChanged(RecordStore s, int id); void recordDeleted(RecordStore s, int id); }
