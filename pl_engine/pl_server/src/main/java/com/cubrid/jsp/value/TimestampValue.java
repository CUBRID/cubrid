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
import com.cubrid.plcsql.predefined.sp.SpLib;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;
import java.util.Calendar;

public class TimestampValue extends Value {

    protected String getTypeName() {
        return TYPE_NAME_TIMESTAMP;
    }

    private Timestamp timestamp;

    public TimestampValue(int year, int mon, int day, int hour, int min, int sec)
            throws TypeMismatchException {
        super();
        Calendar cal = Calendar.getInstance();
        cal.set(year, mon, day, hour, min, sec);
        cal.set(Calendar.MILLISECOND, 0);

        this.timestamp = new Timestamp(cal.getTimeInMillis());
        if (SpLib.checkTimestamp(timestamp) == null) {
            throw new TypeMismatchException("invalid Timestamp " + timestamp);
        }
    }

    public TimestampValue(
            int year, int mon, int day, int hour, int min, int sec, int mode, int dbType)
            throws TypeMismatchException {
        super(mode);
        Calendar cal = Calendar.getInstance();
        cal.clear();
        cal.set(year, mon, day, hour, min, sec);
        cal.set(Calendar.MILLISECOND, 0);

        this.timestamp = new Timestamp(cal.getTimeInMillis());
        if (SpLib.checkTimestamp(timestamp) == null) {
            throw new TypeMismatchException("invalid Timestamp " + timestamp);
        }
        this.dbType = dbType;
    }

    public TimestampValue(Timestamp timestamp) throws TypeMismatchException {
        if (timestamp != null
                && (timestamp.getTime() % 1000L != 0L || SpLib.checkTimestamp(timestamp) == null)) {
            throw new TypeMismatchException("invalid Timestamp " + timestamp);
        }
        this.timestamp = timestamp;
    }

    @Override
    public Date toDate() throws TypeMismatchException {
        return SpLib.convTimestampToDate(timestamp);
    }

    @Override
    public Time toTime() throws TypeMismatchException {
        return SpLib.convTimestampToTime(timestamp);
    }

    @Override
    public Timestamp toTimestamp() throws TypeMismatchException {
        return timestamp;
    }

    @Override
    public Timestamp toDatetime() throws TypeMismatchException {
        return SpLib.convTimestampToDatetime(timestamp);
    }

    @Override
    public Object toObject() throws TypeMismatchException {
        return timestamp;
    }

    @Override
    public String toString() {
        return SpLib.convTimestampToString(timestamp);
    }
}
