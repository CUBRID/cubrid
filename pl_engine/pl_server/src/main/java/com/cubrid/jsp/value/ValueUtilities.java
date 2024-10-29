/*
 *
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

package com.cubrid.jsp.value;

import com.cubrid.jsp.data.DBType;
import com.cubrid.jsp.exception.TypeMismatchException;
import java.math.BigDecimal;
import java.math.RoundingMode;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;

public class ValueUtilities {
    public static Object resolveValue(int dbType, Value value) throws TypeMismatchException {
        Object resolvedResult = null;

        if (value == null) return null;
        switch (dbType) {
            case DBType.DB_INT:
                resolvedResult = value.toIntegerObject();
                break;
            case DBType.DB_BIGINT:
                resolvedResult = value.toLongObject();
                break;
            case DBType.DB_FLOAT:
                resolvedResult = value.toFloatObject();
                break;
            case DBType.DB_DOUBLE:
            case DBType.DB_MONETARY:
                resolvedResult = value.toDoubleObject();
                break;
            case DBType.DB_CHAR:
            case DBType.DB_STRING:
                resolvedResult = value.toString();
                break;
            case DBType.DB_SET:
            case DBType.DB_MULTISET:
            case DBType.DB_SEQUENCE:
                resolvedResult = value.toObjectArray();
                break;
            case DBType.DB_TIME:
                resolvedResult = value.toTime();
                break;
            case DBType.DB_DATE:
                resolvedResult = value.toDate();
                break;
            case DBType.DB_TIMESTAMP:
                resolvedResult = value.toTimestamp();
                break;
            case DBType.DB_DATETIME:
                resolvedResult = value.toDatetime();
                break;
            case DBType.DB_SHORT:
                resolvedResult = value.toShortObject();
                break;
            case DBType.DB_NUMERIC:
                resolvedResult = value.toBigDecimal();
                break;
            case DBType.DB_OID:
            case DBType.DB_OBJECT:
                resolvedResult = value.toOid();
                break;
            case DBType.DB_RESULTSET:
                resolvedResult = value.toResultSet(null);
                break;
            default:
                break;
        }
        return resolvedResult;
    }

    public static Time longToTime(long l) throws TypeMismatchException {
        if (l < 0L) {
            // negative values seem to result in a invalid time value
            // e.g.
            // select cast(cast(-1 as bigint) as time);
            // === <Result of SELECT Command in Line 1> ===
            //
            // <00001>  cast( cast(-1 as bigint) as time): 12:00:0/ AM
            //
            // 1 row selected. (0.004910 sec) Committed. (0.000020 sec)
            throw new TypeMismatchException("negative values not allowed");
        }

        int totalSec = (int) (l % 86400L);
        int hour = totalSec / 3600;
        int minuteSec = totalSec % 3600;
        int min = minuteSec / 60;
        int sec = minuteSec % 60;
        return new Time(hour, min, sec);
    }

    public static Timestamp longToTimestamp(long l) throws TypeMismatchException {
        if (l < 0L) {
            //   select cast(cast(-100 as bigint) as timestamp);
            //   ERROR: Cannot coerce value of domain "bigint" to domain "timestamp"
            throw new TypeMismatchException("negative values not allowed");
        } else if (l > 2147483647L) {
            // 2147483647L : see section 'implicit type conversion' in the user manual
            throw new TypeMismatchException("values over 2,147,483,647 not allowed");
        } else {
            return new Timestamp(l * 1000L); // * 1000 : converts it to milli-seconds
        }
    }

    public static long bigDecimalToLong(BigDecimal bd) throws TypeMismatchException {
        // 1.5 -->2, and -1.5 --> -2 NOTE; different from // Math.round
        BigDecimal bdp = bd.setScale(0, RoundingMode.HALF_UP);
        try {
            return bdp.longValueExact();
        } catch (ArithmeticException e) {
            throw new TypeMismatchException("not fit in a BIGINT: " + bd);
        }
    }

    public static int bigDecimalToInt(BigDecimal bd) throws TypeMismatchException {
        // 1.5 -->2, and -1.5 --> -2 NOTE; different from // Math.round
        BigDecimal bdp = bd.setScale(0, RoundingMode.HALF_UP);
        try {
            return bdp.intValueExact();
        } catch (ArithmeticException e) {
            throw new TypeMismatchException("not fit in an INTEGER: " + bd);
        }
    }

    public static short bigDecimalToShort(BigDecimal bd) throws TypeMismatchException {
        // 1.5 -->2, and -1.5 --> -2 NOTE; different from // Math.round
        BigDecimal bdp = bd.setScale(0, RoundingMode.HALF_UP);
        try {
            return bdp.shortValueExact();
        } catch (ArithmeticException e) {
            throw new TypeMismatchException("not fit in a SHORT: " + bd);
        }
    }

    public static boolean checkValidDate(Date d) {
        if (d == null) {
            return false;
        }

        if (d.equals(NULL_DATE)) {
            return true;
        }

        return d.compareTo(MIN_DATE) >= 0 && d.compareTo(MAX_DATE) <= 0;
    }

    public static Timestamp checkValidTimestamp(Timestamp ts) {
        if (ts == null) {
            return null;
        }

        // '1970-01-01 00:00:00' (GMT) amounts to the Null Timestamp '0000-00-00 00:00:00' in CUBRID
        if (ts.equals(NULL_TIMESTAMP) || ts.getTime() == 0L) {
            return NULL_TIMESTAMP;
        }

        if (ts.compareTo(MIN_TIMESTAMP) >= 0 && ts.compareTo(MAX_TIMESTAMP) <= 0) {
            return ts;
        } else {
            return null;
        }
    }

    public static boolean checkValidDatetime(Timestamp dt) {
        if (dt == null) {
            return false;
        }

        if (dt.equals(NULL_DATETIME)) {
            return true;
        }

        return dt.compareTo(MIN_DATETIME) >= 0 && dt.compareTo(MAX_DATETIME) <= 0;
    }

    public static final Date MIN_DATE = new Date(1 - 1900, 1 - 1, 1);
    public static final Date MAX_DATE = new Date(9999 - 1900, 12 - 1, 31);
    public static final Timestamp MIN_TIMESTAMP =
            new Timestamp(DateTimeParser.minTimestamp.toEpochSecond() * 1000);
    public static final Timestamp MAX_TIMESTAMP =
            new Timestamp(DateTimeParser.maxTimestamp.toEpochSecond() * 1000);
    public static final Timestamp MIN_DATETIME = Timestamp.valueOf(DateTimeParser.minDatetimeLocal);
    public static final Timestamp MAX_DATETIME = Timestamp.valueOf(DateTimeParser.maxDatetimeLocal);

    public static final Date NULL_DATE = new Date(0 - 1900, 0 - 1, 0);
    public static final Timestamp NULL_TIMESTAMP =
            new Timestamp(0 - 1900, 0 - 1, 0, 0, 0, 0, 0); // TODO: CBRD-25595
    public static final Timestamp NULL_DATETIME = new Timestamp(0 - 1900, 0 - 1, 0, 0, 0, 0, 0);
}
