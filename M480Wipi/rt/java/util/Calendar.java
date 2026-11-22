package java.util;
public abstract class Calendar {
    public static final int ERA = 0, YEAR = 1, MONTH = 2, WEEK_OF_YEAR = 3, WEEK_OF_MONTH = 4, DATE = 5, DAY_OF_MONTH = 5,
        DAY_OF_YEAR = 6, DAY_OF_WEEK = 7, DAY_OF_WEEK_IN_MONTH = 8, AM_PM = 9, HOUR = 10, HOUR_OF_DAY = 11, MINUTE = 12,
        SECOND = 13, MILLISECOND = 14, ZONE_OFFSET = 15, DST_OFFSET = 16;
    public static final int SUNDAY = 1, MONDAY = 2, TUESDAY = 3, WEDNESDAY = 4, THURSDAY = 5, FRIDAY = 6, SATURDAY = 7;
    public static final int JANUARY = 0, FEBRUARY = 1, MARCH = 2, APRIL = 3, MAY = 4, JUNE = 5, JULY = 6, AUGUST = 7,
        SEPTEMBER = 8, OCTOBER = 9, NOVEMBER = 10, DECEMBER = 11;
    public static final int AM = 0, PM = 1;
    protected int[] fields = new int[17];
    protected boolean[] isSet = new boolean[17];
    protected long time;
    private TimeZone zone;
    protected Calendar() { zone = TimeZone.getDefault(); setTimeInMillis(System.currentTimeMillis()); }
    public static synchronized Calendar getInstance() { return new Greg(); }
    public static synchronized Calendar getInstance(TimeZone z) { Calendar c = new Greg(); c.setTimeZone(z); return c; }
    public final Date getTime() { return new Date(getTimeInMillis()); }
    public final void setTime(Date d) { setTimeInMillis(d.getTime()); }
    protected long getTimeInMillis() { return time; }
    protected void setTimeInMillis(long ms) { time = ms; computeFields(); }
    public final int get(int f) { return fields[f]; }
    public final void set(int f, int v) { fields[f] = v; computeTime(); computeFields(); }
    public void setTimeZone(TimeZone z) { zone = z; computeFields(); }
    public TimeZone getTimeZone() { return zone; }
    protected abstract void computeFields();
    protected abstract void computeTime();
    public boolean equals(Object o) { return o instanceof Calendar && ((Calendar) o).time == time; }
    public boolean before(Object o) { return o instanceof Calendar && time < ((Calendar) o).time; }
    public boolean after(Object o) { return o instanceof Calendar && time > ((Calendar) o).time; }
    public String toString() { return fields[YEAR] + "-" + (fields[MONTH] + 1) + "-" + fields[DATE] + " " + fields[HOUR_OF_DAY] + ":" + fields[MINUTE] + ":" + fields[SECOND]; }
    static class Greg extends Calendar {
        private static long daysFrom(int y, int m, int d) {
            /* days since 1970-01-01, m 0-based */
            int yy = y, mm = m + 1;
            if (mm <= 2) { yy--; mm += 12; }
            long era = (yy >= 0 ? yy : yy - 399) / 400;
            long yoe = yy - era * 400;
            long doy = (153 * (mm - 3) + 2) / 5 + d - 1;
            long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + doe - 719468;
        }
        protected void computeFields() {
            long local = time + getTimeZone().getRawOffset();
            long days = local / 86400000L;
            long ms = local % 86400000L;
            if (ms < 0) { ms += 86400000L; days--; }
            long z = days + 719468;
            long era = (z >= 0 ? z : z - 146096) / 146097;
            long doe = z - era * 146097;
            long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
            long y = yoe + era * 400;
            long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            long mp = (5 * doy + 2) / 153;
            long d = doy - (153 * mp + 2) / 5 + 1;
            long m = mp < 10 ? mp + 3 : mp - 9;
            if (m <= 2) y++;
            fields[ERA] = 1;
            fields[YEAR] = (int) y;
            fields[MONTH] = (int) m - 1;
            fields[DATE] = (int) d;
            fields[DAY_OF_WEEK] = (int) (((days % 7) + 7 + 4) % 7) + 1;
            fields[DAY_OF_YEAR] = (int) (days - daysFrom((int) y, 0, 1)) + 1;
            fields[HOUR_OF_DAY] = (int) (ms / 3600000L);
            fields[AM_PM] = fields[HOUR_OF_DAY] >= 12 ? PM : AM;
            fields[HOUR] = fields[HOUR_OF_DAY] % 12;
            fields[MINUTE] = (int) (ms / 60000L % 60);
            fields[SECOND] = (int) (ms / 1000L % 60);
            fields[MILLISECOND] = (int) (ms % 1000L);
            fields[ZONE_OFFSET] = getTimeZone().getRawOffset();
            fields[WEEK_OF_YEAR] = (fields[DAY_OF_YEAR] - 1) / 7 + 1;
            fields[WEEK_OF_MONTH] = (fields[DATE] - 1) / 7 + 1;
            fields[DAY_OF_WEEK_IN_MONTH] = (fields[DATE] - 1) / 7 + 1;
        }
        protected void computeTime() {
            long days = daysFrom(fields[YEAR], fields[MONTH], fields[DATE]);
            time = days * 86400000L + fields[HOUR_OF_DAY] * 3600000L + fields[MINUTE] * 60000L + fields[SECOND] * 1000L + fields[MILLISECOND]
                - getTimeZone().getRawOffset();
        }
    }
}
