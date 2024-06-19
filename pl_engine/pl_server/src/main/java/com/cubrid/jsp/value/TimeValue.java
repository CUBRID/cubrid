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
import java.sql.Time;
import java.util.Calendar;

public class TimeValue extends Value {
    private Time time;

    public TimeValue(int hour, int min, int sec) {
        super();
        Calendar cal = Calendar.getInstance();
        cal.set(0, 0, 0, hour, min, sec);
        cal.set(Calendar.MILLISECOND, 0);

        this.time = new Time(cal.getTimeInMillis());
    }

    public TimeValue(int hour, int min, int sec, int mode, int dbType) {
        super(mode);
        Calendar cal = Calendar.getInstance();
        cal.set(0, 0, 0, hour, min, sec);
        cal.set(Calendar.MILLISECOND, 0);

        this.time = new Time(cal.getTimeInMillis());
        this.dbType = dbType;
    }

    public TimeValue(Time time) {
        this.time = time;
        assert time.getTime() % 1000L == 0;
    }

    public Time toTime() throws TypeMismatchException {
        return time;
    }

    public Object toObject() throws TypeMismatchException {
        return toTime();
    }

    public String toString() {
        return time.toString();
    }

    public Time[] toTimeArray() throws TypeMismatchException {
        return new Time[] {toTime()};
    }

    public Object[] toObjectArray() throws TypeMismatchException {
        return new Object[] {toObject()};
    }

    public String[] toStringArray() throws TypeMismatchException {
        return new String[] {toString()};
    }
}
