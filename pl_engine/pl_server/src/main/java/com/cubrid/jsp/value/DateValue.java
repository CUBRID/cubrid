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
import java.sql.Timestamp;
import java.util.Calendar;

public class DateValue extends Value {

    protected String getTypeName() {
        return TYPE_NAME_DATE;
    }

    private Date date;

    public DateValue(int year, int mon, int day) throws TypeMismatchException {
        super();
        Calendar cal = Calendar.getInstance();
        cal.set(year, mon, day, 0, 0, 0);
        cal.set(Calendar.MILLISECOND, 0);

        date = new Date(cal.getTimeInMillis());
        if (!SpLib.checkDate(date)) {
            throw new TypeMismatchException("invalid Date " + date);
        }
        this.dbType = DBType.DB_DATE;
    }

    public DateValue(Date date) throws TypeMismatchException {
        if (date != null && !SpLib.checkDate(date)) {
            throw new TypeMismatchException("invalid Date " + date);
        }
        this.date = date;
        this.dbType = DBType.DB_DATE;
    }

    @Override
    public Date toDate() throws TypeMismatchException {
        return date;
    }

    @Override
    public Timestamp toTimestamp() throws TypeMismatchException {
        return SpLib.convDateToTimestamp(date);
    }

    @Override
    public Timestamp toDatetime() throws TypeMismatchException {
        return SpLib.convDateToDatetime(date);
    }

    @Override
    public Object toObject() throws TypeMismatchException {
        return toDate();
    }

    @Override
    public String toString() {
        return SpLib.convDateToString(date);
    }
}
