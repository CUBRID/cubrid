package com.cubrid.plcsql.compiler;

import java.text.DateFormat;
import java.text.SimpleDateFormat;
import java.text.ParsePosition;

import java.time.LocalDate;
import java.time.LocalTime;
import java.time.LocalDateTime;
import java.time.ZonedDateTime;
import java.time.ZoneOffset;
import java.time.DateTimeException;
import java.time.temporal.TemporalAccessor;

import java.util.TimeZone;
import java.util.Locale;
import java.util.Date;
import java.util.Calendar;
import java.util.GregorianCalendar;
import java.util.List;
import java.util.Arrays;

public class DateTimeParser {

    private static final ZoneOffset TIMEZONE_0 = ZoneOffset.of("Z");

    // min timestamp: 1970-01-01 00:00:01
    private static final LocalDateTime minTimestamp = LocalDateTime.of(1970, 1, 1, 0, 0, 1);
    // max timestamp: 2038-01-19 03:14:07
    private static final LocalDateTime maxTimestamp = LocalDateTime.of(2038, 1, 19, 3, 14, 7);
    // min datetime: 0001-01-01 00:00:00.000
    private static final LocalDateTime minDatetime = LocalDateTime.of(1, 1, 1, 0, 0, 0, 0);
    // max datetime: 9999-12-31 23:59:59.999
    private static final LocalDateTime maxDatetime = LocalDateTime.of(9999, 12, 31, 23, 59, 59, 999);

    private static final long minTimestampUTC = ZonedDateTime.of(minTimestamp, TIMEZONE_0).toInstant().toEpochMilli();
    private static final long maxTimestampUTC = ZonedDateTime.of(maxTimestamp, TIMEZONE_0).toInstant().toEpochMilli();
    private static final long minDatetimeUTC = ZonedDateTime.of(minDatetime, TIMEZONE_0).toInstant().toEpochMilli();
    private static final long maxDatetimeUTC = ZonedDateTime.of(maxDatetime, TIMEZONE_0).toInstant().toEpochMilli();

    public static final LocalDate nullDate = LocalDate.MAX;
    public static final LocalTime nullTime = LocalTime.MAX;
    public static final LocalDateTime nullDateTime = LocalDateTime.MAX;

    public static class DateLiteral {

        public static LocalDate parse(String s) {
            LocalDate ret = parseDateFragment(s);

            if (ret != null &&
                ret != nullDate &&
                (ret.compareTo(minDate) < 0 || ret.compareTo(maxDate) > 0)) {
                return null;
            }

            return ret;
        }

        // ---------------------------------------
        // Private
        // ---------------------------------------

        private static final LocalDate minDate = LocalDate.of(1, 1, 1);      // 0001-01-01
        private static final LocalDate maxDate = LocalDate.of(9999, 12, 31); // 9999-12-31
        static {
            //System.out.println("minDate=" + minDate);
            //System.out.println("maxDate=" + maxDate);
        }
    }

    public static class TimeLiteral {

        public static LocalTime parse(String s) {
            return parseTimeFragment(s, true);
        }

    }

    public static class TimestampLiteral {

        public static LocalDateTime parse(String s) {
            return parseDateAndTime(s, false);  // NOTE: range check must be done in the server with the session timezone
        }

        // ---------------------------------------
        // Private
        // ---------------------------------------

        // TODO: minTimestamp and maxTimestamp must be UTC
        private static final LocalDateTime minTimestamp = LocalDateTime.of(1970, 1, 1, 0, 0, 1);    // 1970-01-01 00:00:01
        private static final LocalDateTime maxTimestamp = LocalDateTime.of(2038, 1, 19, 3, 14, 7);  // 2038-01-19 03:14:07
        static {
            //System.out.println("minTimestamp=" + minTimestamp);
            //System.out.println("maxTimestamp=" + maxTimestamp);
        }

    }

    public static class DatetimeLiteral {

        public static LocalDateTime parse(String s) {

            LocalDateTime ret = parseDateAndTime(s, true);
            if (ret != null &&
                ret != nullDateTime &&
                (ret.compareTo(minDatetime) < 0 || ret.compareTo(maxDatetime) > 0)) {   // NOTE: no UTC comparison
                return null;
            }

            return ret;
        }

        // ---------------------------------------
        // Private
        // ---------------------------------------

    }

    public static class TimestampTZLiteral {

        public static TemporalAccessor parse(String stz) {

            TemporalAccessor ret = parseZonedDateAndTime(stz, false);
            if (ret instanceof ZonedDateTime) {
                ZonedDateTime zoned = (ZonedDateTime) ret;
                if (!zoned.toLocalDateTime().equals(nullDateTime)) {
                    long utc = zoned.toInstant().toEpochMilli();
                    if (utc < minTimestampUTC || utc > maxTimestampUTC) {
                        return null;
                    }
                }
            }

            return ret;
        }
    }

    public static class TimestampLTZLiteral {

        public static TemporalAccessor parse(String stz) {
            return TimestampTZLiteral.parse(stz);
        }
    }

    public static class DatetimeTZLiteral {

        public static TemporalAccessor parse(String stz) {

            TemporalAccessor ret = parseZonedDateAndTime(stz, true);
            if (ret instanceof ZonedDateTime) {
                ZonedDateTime zoned = (ZonedDateTime) ret;
                if (!zoned.toLocalDateTime().equals(nullDateTime)) {
                    long utc = zoned.toInstant().toEpochMilli();
                    if (utc < minDatetimeUTC || utc > maxDatetimeUTC) {
                        return null;
                    }
                }
            }

            return ret;
        }
    }

    public static class DatetimeLTZLiteral {

        public static TemporalAccessor parse(String stz) {
            return DatetimeTZLiteral.parse(stz);
        }
    }

    // ---------------------------------------
    // Private
    // ---------------------------------------

    // returns a ZonedDateTime when a timezone offset is given, otherwise returns a LocalDateTime
    public static TemporalAccessor parseZonedDateAndTime(String stz, boolean millis) {

        // get timezone offset
        ZoneOffset zone;
        int delim = stz.lastIndexOf(" ");
        if (delim < 0) {
            // no timezone offset
            return parseDateAndTime(stz, millis);
        } else {
            String tz = stz.substring(delim + 1);
            try {
                zone = ZoneOffset.of(tz);
            } catch (DateTimeException e) {
                // timezone offset is invalid. try timezone omitted string
                return parseDateAndTime(stz, millis);
            }
        }

        // get date and time
        String s = stz.substring(0, delim);
        LocalDateTime ldt = parseDateAndTime(s, millis);
        if (ldt == null) {
            return null;
        }
        if (ldt == nullDateTime) {
            return ZonedDateTime.of(ldt, zone);
        }

        return ZonedDateTime.of(ldt, zone);
    }

    private static LocalDateTime parseDateAndTime(String s, boolean millis) {

        s = s.trim();
        //System.out.println("[temp] ### " + s);

        String timeStr, dateStr;
        int colonIdx = s.indexOf(":");
        if (colonIdx == 1 || colonIdx == 2) {
            //  order: <time> <date>
            int cut = s.lastIndexOf(" ");
            if (cut < 0) {
                System.out.println("no date");
                return null;    // error
            } else {
                timeStr = s.substring(0, cut);
                dateStr = s.substring(cut + 1);
            }
        } else {
            // order: <date> <time>
            int cut = s.indexOf(" ");
            if (cut < 0) {
                dateStr = s;
                timeStr = null;
            } else {
                dateStr = s.substring(0, cut);
                timeStr = s.substring(cut + 1);
            }
        }

        LocalDate date = parseDateFragment(dateStr);
        if (date == null) {
            System.out.println("date error");
            return null;
        }

        LocalTime time;
        if (timeStr == null) {
            time = null;
        } else {
            time = parseTimeFragment(timeStr, millis);
            if (time == null) {
                System.out.println("time error");
                return null;
            }
        }

        if (date.equals(nullDate)) {
            if (time == null || time.equals(LocalTime.MIN)) {
                return nullDateTime;
            } else {
                System.out.println("time must be zero");
                return null;    // error
            }
        } else {

            LocalDateTime ret;

            if (time == null) {
                ret = date.atTime(0, 0, 0, 0);
            } else {
                ret = date.atTime(time.getHour(), time.getMinute(), time.getSecond(), time.getNano());
            }

            return ret;
        }
    }

    // ------------------------------------------------------
    // for parsing date fragment
    // ------------------------------------------------------

    private static final List<SimpleDateFormat> dateFormats = Arrays.asList(
        new SimpleDateFormat("MM/dd/yyyy"),
        new SimpleDateFormat("yyyy-MM-dd")
    );
    static {
        for (SimpleDateFormat f: dateFormats) {
            f.setLenient(false);
            assert f.getCalendar() instanceof GregorianCalendar;
        }
    }

    private static LocalDate parseDateFragment(String s) {

        s = s.trim();
        //System.out.println("[temp] ### " + s);

        int i = 0;
        for (SimpleDateFormat f: dateFormats) {

            ParsePosition pos = new ParsePosition(0);

            Calendar calendar = f.getCalendar();
            calendar.clear();

            Date d = f.parse(s, pos);

            //System.out.println("[temp] i=" + i);

            if (d != null && pos.getIndex() == s.length()) {

                int year = calendar.get(Calendar.YEAR);
                int month = calendar.get(Calendar.MONTH) + 1;
                int date = calendar.get(Calendar.DAY_OF_MONTH);

                if (i == 0) {
                    String[] split = s.split("/");
                    assert split.length == 3;
                    if (split[2].trim().length() == 2) {
                        // year is given in two digits
                        year = (year < 70) ? year + 2000 : year + 1900;
                    }
                } else {
                    assert i == 1;

                    String[] split = s.split("-");
                    assert split.length == 3;
                    if (split[0].trim().length() == 2) {
                        // year is given in two digits
                        year = (year < 70) ? year + 2000 : year + 1900;
                    }
                }

                return LocalDate.of(year, month, date);
            } else if (pos.getErrorIndex() == s.length()) {
                // check if it is 0000-00-00

                if (calendar.isSet(Calendar.YEAR) &&
                    calendar.isSet(Calendar.MONTH) &&
                    calendar.isSet(Calendar.DAY_OF_MONTH)) {

                    f.setLenient(true);
                    int era = calendar.get(Calendar.ERA);
                    int year = calendar.get(Calendar.YEAR);
                    int month = calendar.get(Calendar.MONTH);
                    int day = calendar.get(Calendar.DAY_OF_MONTH);
                    f.setLenient(false);

                    // 0000-00-00 == BC 0002-11-30
                    if (era == GregorianCalendar.BC && year == 2 && month == Calendar.NOVEMBER && day == 30) {
                        //System.out.println("[temp] failed calendar after get=" + calendar);
                        return nullDate;
                    }
                }
            }

            i++;
        }

        return null;
    }

    // ------------------------------------------------------
    // for parsing time fragment
    // ------------------------------------------------------

    private static final List<SimpleDateFormat> timeFormats12 = Arrays.asList(
        new SimpleDateFormat("KK:mm aa", Locale.US),
        new SimpleDateFormat("KK:mm:ss aa", Locale.US),
        new SimpleDateFormat("KK:mm:ss.SSS aa", Locale.US)  // must go at last
    );
    private static final List<SimpleDateFormat> timeFormats24 = Arrays.asList(
        new SimpleDateFormat("HH:mm"),
        new SimpleDateFormat("HH:mm:ss"),
        new SimpleDateFormat("HH:mm:ss.SSS")    // must go at last
    );
    static {
        for (SimpleDateFormat f: timeFormats12) {
            f.setLenient(false);
            assert f.getCalendar() instanceof GregorianCalendar;
        }
        for (SimpleDateFormat f: timeFormats24) {
            f.setLenient(false);
            assert f.getCalendar() instanceof GregorianCalendar;
        }
    }

    private static LocalTime parseTimeFragment(String s, boolean millis) {

        s = s.trim();
        //System.out.println("[temp] ### " + s);
        List<SimpleDateFormat> formats = (s.indexOf(" ") >= 0) ? timeFormats12 : timeFormats24;

        int j = 0;
        for (SimpleDateFormat f: formats) {

            ParsePosition pos = new ParsePosition(0);

            Calendar calendar = f.getCalendar();
            calendar.clear();

            Date d = f.parse(s, pos);

            //System.out.println("[temp] j=" + j);
            //System.out.println("[temp] d=" + d);
            //System.out.println("[temp] pos=" + pos);

            if (d != null && pos.getIndex() == s.length()) {
                return LocalTime.of(calendar.get(Calendar.HOUR_OF_DAY),
                                    calendar.get(Calendar.MINUTE),
                                    calendar.get(Calendar.SECOND),
                                    calendar.get(Calendar.MILLISECOND) * 1000000);
            }

            j++;
            if (j == 2 && !millis) {
                break;
            }
        }

        return null;
    }
}

