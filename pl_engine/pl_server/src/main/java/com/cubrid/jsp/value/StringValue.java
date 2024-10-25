/*
 * Copyright (C) 2008 Search Solution Corporation.
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

import com.cubrid.jsp.exception.TypeMismatchException;
import java.math.BigDecimal;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;
import java.time.LocalDate;
import java.time.LocalDateTime;
import java.time.LocalTime;
import java.time.ZonedDateTime;

public class StringValue extends Value {

    private String value;

    public StringValue(String value) {
        super();
        this.value = value;
    }

    public StringValue(String value, int mode, int dbType) {
        super(mode);
        this.value = value;
        this.dbType = dbType;
    }

    @Override
    public byte toByte() throws TypeMismatchException {
        try {
            return Byte.parseByte(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    @Override
    public short toShort() throws TypeMismatchException {
        try {
            return Short.parseShort(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    @Override
    public int toInt() throws TypeMismatchException {
        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    @Override
    public long toLong() throws TypeMismatchException {
        try {
            return Long.parseLong(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    @Override
    public float toFloat() throws TypeMismatchException {
        try {
            return Float.parseFloat(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    @Override
    public double toDouble() throws TypeMismatchException {
        try {
            return Double.parseDouble(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    @Override
    public Byte toByteObject() throws TypeMismatchException {
        try {
            return Byte.valueOf(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    @Override
    public Short toShortObject() throws TypeMismatchException {
        try {
            return Short.valueOf(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    @Override
    public Integer toIntegerObject() throws TypeMismatchException {
        try {
            return Integer.valueOf(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    @Override
    public Long toLongObject() throws TypeMismatchException {
        try {
            return Long.valueOf(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    @Override
    public Float toFloatObject() throws TypeMismatchException {
        try {
            return Float.valueOf(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    @Override
    public Double toDoubleObject() throws TypeMismatchException {
        try {
            return Double.valueOf(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    @Override
    public Date toDate() throws TypeMismatchException {
        LocalDate lDate = DateTimeParser.DateLiteral.parse(value);
        if (lDate == null) {
            throw new TypeMismatchException("invalid DATE string: " + value);
        } else if (lDate.equals(DateTimeParser.nullDate)) {
            return new Date(0 - 1900, 0 - 1, 0); // 0000-00-00
        } else {
            return Date.valueOf(lDate);
        }
    }

    @Override
    public Time toTime() throws TypeMismatchException {
        LocalTime lTime = DateTimeParser.TimeLiteral.parse(value);
        if (lTime == null) {
            throw new TypeMismatchException("invalid TIME string: " + value);
        } else {
            return Time.valueOf(lTime);
        }
    }

    @Override
    public Timestamp toTimestamp() throws TypeMismatchException {
        ZonedDateTime lTimestamp = DateTimeParser.TimestampLiteral.parse(value);
        if (lTimestamp == null) {
            throw new TypeMismatchException("invalid TIMESTAMP string: " + value);
        } else if (lTimestamp.equals(DateTimeParser.nullDatetimeGMT)) {
            return new Timestamp(0 - 1900, 0 - 1, 0, 0, 0, 0, 0); // 0000-00-00 00:00:00
        } else {
            return Timestamp.valueOf(lTimestamp.toLocalDateTime());
        }
    }

    @Override
    public Timestamp toDatetime() throws TypeMismatchException {
        LocalDateTime lDatetime = DateTimeParser.DatetimeLiteral.parse(value);
        if (lDatetime == null) {
            throw new TypeMismatchException("invalid DATETIME string: " + value);
        } else if (lDatetime.equals(DateTimeParser.nullDatetime)) {
            return new Timestamp(0 - 1900, 0 - 1, 0, 0, 0, 0, 0); // 0000-00-00 00:00:00.000
        } else {
            return Timestamp.valueOf(lDatetime);
        }
    }

    @Override
    public BigDecimal toBigDecimal() throws TypeMismatchException {
        try {
            return new BigDecimal(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    @Override
    public Object toObject() throws TypeMismatchException {
        return toString();
    }

    @Override
    public String toString() {
        return value;
    }
}
