/*
 * Copyright (c) 2016 CUBRID Corporation.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

package com.cubrid.plcsql.compiler;

import java.text.ParsePosition;
import java.text.SimpleDateFormat;
import java.time.DateTimeException;
import java.time.LocalDate;
import java.time.LocalDateTime;
import java.time.LocalTime;
import java.time.ZoneOffset;
import java.time.ZonedDateTime;
import java.time.temporal.TemporalAccessor;
import java.util.Arrays;
import java.util.Calendar;
import java.util.Date;
import java.util.GregorianCalendar;
import java.util.List;
import java.util.Locale;

public class DateTimeParser {

    private static final ZoneOffset TIMEZONE_0 = ZoneOffset.of("Z");
    private static final ZoneOffset TIMEZONE_SESSION = ZoneOffset.of("+05:00");  // temporary code TODO: fix this

    // zoneless part of min timestamp: 1970-01-01 00:00:01
    private static final LocalDateTime minTimestampLocal = LocalDateTime.of(1970, 1, 1, 0, 0, 1);
    // zoneless part of max timestamp local part: 2038-01-19 03:14:07
    private static final LocalDateTime maxTimestampLocal = LocalDateTime.of(2038, 1, 19, 3, 14, 7);
    // min datetime: 0001-01-01 00:00:00.000
    private static final LocalDateTime minDatetime = LocalDateTime.of(1, 1, 1, 0, 0, 0, 0);
    // max datetime: 9999-12-31 23:59:59.999
    private static final LocalDateTime maxDatetime =
            LocalDateTime.of(9999, 12, 31, 23, 59, 59, 999);

    private static final ZonedDateTime minTimestamp = ZonedDateTime.of(minTimestampLocal, TIMEZONE_0);
    private static final ZonedDateTime maxTimestamp = ZonedDateTime.of(maxTimestampLocal, TIMEZONE_0);
    private static final ZonedDateTime minDatetimeUTC = ZonedDateTime.of(minDatetime, TIMEZONE_0);
    private static final ZonedDateTime maxDatetimeUTC = ZonedDateTime.of(maxDatetime, TIMEZONE_0);

    public static final LocalDate nullDate = LocalDate.MAX;
    public static final LocalDateTime nullDatetime = LocalDateTime.MAX;
    public static final ZonedDateTime nullDatetimeUTC = ZonedDateTime.of(nullDatetime, TIMEZONE_0);

    public static class DateLiteral {

        public static LocalDate parse(String s) {
            LocalDate ret = parseDateFragment(s);

            if (ret != null
                    && ret != nullDate
                    && (ret.compareTo(minDate) < 0 || ret.compareTo(maxDate) > 0)) {
                return null;
            }

            return ret;
        }

        // ---------------------------------------
        // Private
        // ---------------------------------------

        private static final LocalDate minDate = LocalDate.of(1, 1, 1); // 0001-01-01
        private static final LocalDate maxDate = LocalDate.of(9999, 12, 31); // 9999-12-31
    }

    public static class TimeLiteral {

        public static LocalTime parse(String s) {
            return parseTimeFragment(s, false);
        }
    }

    public static class TimestampLiteral {

        public static ZonedDateTime parse(String s) {
            return ZonedDateTimeLiteral.parse(s, false); // same as TIMESTAMPLTZ with timezone omitted
        }
    }

    public static class DatetimeLiteral {

        public static LocalDateTime parse(String s) {

            LocalDateTime ret = parseDateAndTime(s, true);
            if (ret != null
                    && ret != nullDatetime
                    && (ret.compareTo(minDatetime) < 0
                            || ret.compareTo(maxDatetime) > 0)) {
                return null;
            }

            return ret;
        }
    }

    public static class ZonedDateTimeLiteral {
        public static ZonedDateTime parse(String s, boolean forDatetime) {
            return parseZonedDateAndTime(s, forDatetime);
        }
    }

    // ---------------------------------------
    // Private
    // ---------------------------------------

    // returns a ZonedDateTime when a timezone offset is given, otherwise returns a LocalDateTime
    public static ZonedDateTime parseZonedDateAndTime(String s, boolean forDatetime) {

        // get timezone offset
        LocalDateTime localPart;
        ZoneOffset zone;
        int delim = s.lastIndexOf(" ");
        if (delim < 0) {
            // no timezone offset
            localPart = parseDateAndTime(s, forDatetime);
            zone = TIMEZONE_SESSION;
        } else {
            String dt = s.substring(0, delim);
            String z = s.substring(delim + 1);
            try {
                localPart = parseDateAndTime(dt, forDatetime);
                zone = ZoneOffset.of(z);
            } catch (DateTimeException e) {
                // z turn out not to be a timezone offset. try timezone omitted string
                localPart = parseDateAndTime(s, forDatetime);
                zone = TIMEZONE_SESSION;
            }
        }

        if (localPart == null) {
            return null;
        }
        if (localPart == nullDatetime) {
            return nullDatetimeUTC;
        }

        ZonedDateTime ret = ZonedDateTime.of(localPart, zone);
        if (forDatetime) {
            if (ret.compareTo(minDatetimeUTC) < 0 || ret.compareTo(maxDatetimeUTC) > 0) {
                return null;
            }
        } else {
            // in this case, for TIMESTAMP*
            if (ret.compareTo(minTimestamp) < 0 || ret.compareTo(maxTimestamp) > 0) {
                return null;
            }
        }

        return ret;
    }

    private static LocalDateTime parseDateAndTime(String s, boolean millis) {

        s = s.trim();

        String timeStr, dateStr;
        int colonIdx = s.indexOf(":");
        if (colonIdx == 1 || colonIdx == 2) {
            //  order: <time> <date>
            int cut = s.lastIndexOf(" ");
            if (cut < 0) {
                return null; // error
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
            return null;
        }

        LocalTime time;
        if (timeStr == null) {
            time = null;
        } else {
            time = parseTimeFragment(timeStr, millis);
            if (time == null) {
                return null;
            }
        }

        if (date.equals(nullDate)) {
            if (time == null || time.equals(LocalTime.MIN)) {
                return nullDatetime;
            } else {
                return null; // error
            }
        } else {

            LocalDateTime ret;

            if (time == null) {
                ret = date.atTime(0, 0, 0, 0);
            } else {
                ret =
                        date.atTime(
                                time.getHour(), time.getMinute(), time.getSecond(), time.getNano());
            }

            return ret;
        }
    }

    private static final List<SimpleDateFormat> dateFormats =
            Arrays.asList(new SimpleDateFormat("MM/dd/yyyy"), new SimpleDateFormat("yyyy-MM-dd"));

    static {
        for (SimpleDateFormat f : dateFormats) {
            f.setLenient(false);
            assert f.getCalendar() instanceof GregorianCalendar;
        }
    }

    private static LocalDate parseDateFragment(String s) {

        s = s.trim();

        int i = 0;
        for (SimpleDateFormat f : dateFormats) {

            ParsePosition pos = new ParsePosition(0);

            Calendar calendar = f.getCalendar();
            calendar.clear();

            Date d = f.parse(s, pos);


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

                if (calendar.isSet(Calendar.YEAR)
                        && calendar.isSet(Calendar.MONTH)
                        && calendar.isSet(Calendar.DAY_OF_MONTH)) {

                    f.setLenient(true);
                    int era = calendar.get(Calendar.ERA);
                    int year = calendar.get(Calendar.YEAR);
                    int month = calendar.get(Calendar.MONTH);
                    int day = calendar.get(Calendar.DAY_OF_MONTH);
                    f.setLenient(false);

                    // 0000-00-00 == BC 0002-11-30
                    if (era == GregorianCalendar.BC
                            && year == 2
                            && month == Calendar.NOVEMBER
                            && day == 30) {
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

    private static final List<SimpleDateFormat> timeFormats12 =
            Arrays.asList(
                    new SimpleDateFormat("KK:mm aa", Locale.US),
                    new SimpleDateFormat("KK:mm:ss aa", Locale.US),
                    new SimpleDateFormat("KK:mm:ss.SSS aa", Locale.US) // must go at last
                    );
    private static final List<SimpleDateFormat> timeFormats24 =
            Arrays.asList(
                    new SimpleDateFormat("HH:mm"),
                    new SimpleDateFormat("HH:mm:ss"),
                    new SimpleDateFormat("HH:mm:ss.SSS") // must go at last
                    );

    static {
        for (SimpleDateFormat f : timeFormats12) {
            f.setLenient(false);
            assert f.getCalendar() instanceof GregorianCalendar;
        }
        for (SimpleDateFormat f : timeFormats24) {
            f.setLenient(false);
            assert f.getCalendar() instanceof GregorianCalendar;
        }
    }

    private static LocalTime parseTimeFragment(String s, boolean millis) {

        s = s.trim();
        List<SimpleDateFormat> formats = (s.indexOf(" ") >= 0) ? timeFormats12 : timeFormats24;

        int j = 0;
        for (SimpleDateFormat f : formats) {

            ParsePosition pos = new ParsePosition(0);

            Calendar calendar = f.getCalendar();
            calendar.clear();

            Date d = f.parse(s, pos);

            if (d != null && pos.getIndex() == s.length()) {
                return LocalTime.of(
                        calendar.get(Calendar.HOUR_OF_DAY),
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
