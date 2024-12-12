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

import com.cubrid.jsp.data.DBType;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.plcsql.predefined.sp.SpLib;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;
import java.util.Calendar;

public class DatetimeValue extends Value {

    protected String getTypeName() {
        return TYPE_NAME_DATETIME;
    }

    private Timestamp timestamp;

    public DatetimeValue(int year, int mon, int day, int hour, int min, int sec, int msec)
            throws TypeMismatchException {
        super();

        Calendar c = Calendar.getInstance();
        c.set(year, mon, day, hour, min, sec);
        c.set(Calendar.MILLISECOND, msec);

        timestamp = new Timestamp(c.getTimeInMillis());
        this.dbType = DBType.DB_DATETIME;
        if (!SpLib.checkDatetime(timestamp)) {
            throw new TypeMismatchException("invalid Datetime " + timestamp);
        }
    }

    public DatetimeValue(Timestamp timestamp) throws TypeMismatchException {
        if (timestamp != null && !SpLib.checkDatetime(timestamp)) {
            throw new TypeMismatchException("invalid Datetime " + timestamp);
        }
        this.timestamp = timestamp;
        this.dbType = DBType.DB_DATETIME;
    }

    @Override
    public Date toDate() throws TypeMismatchException {
        return SpLib.convDatetimeToDate(timestamp);
    }

    @Override
    public Time toTime() throws TypeMismatchException {
        return SpLib.convDatetimeToTime(timestamp);
    }

    @Override
    public Timestamp toTimestamp() throws TypeMismatchException {
        return SpLib.convDatetimeToTimestamp(timestamp);
    }

    @Override
    public Timestamp toDatetime() throws TypeMismatchException {
        return timestamp;
    }

    @Override
    public String toString() {
        return SpLib.convDatetimeToString(timestamp);
    }

    @Override
    public Object toObject() throws TypeMismatchException {
        return timestamp;
    }
}
