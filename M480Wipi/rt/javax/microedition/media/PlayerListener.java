package javax.microedition.media;
public interface PlayerListener {
    String STARTED = "started", STOPPED = "stopped", END_OF_MEDIA = "endOfMedia", DURATION_UPDATED = "durationUpdated",
        DEVICE_UNAVAILABLE = "deviceUnavailable", DEVICE_AVAILABLE = "deviceAvailable", VOLUME_CHANGED = "volumeChanged",
        ERROR = "error", CLOSED = "closed", STOPPED_AT_TIME = "stoppedAtTime";
    void playerUpdate(Player p, String event, Object data);
}
