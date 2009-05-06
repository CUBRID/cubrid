/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
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

import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;
import java.util.Calendar;

import com.cubrid.jsp.exception.TypeMismatchException;

public class DateValue extends Value
{
  private Date date;

  public DateValue(int year, int mon, int day)
  {
    super();
    Calendar cal = Calendar.getInstance();
    cal.set(year, mon, day);

    date = new Date(cal.getTimeInMillis());
  }

  public DateValue(int year, int mon, int day, int mode, int dbType)
  {
    super(mode);
    Calendar cal = Calendar.getInstance();
    cal.set(year, mon, day);

    date = new Date(cal.getTimeInMillis());
    this.dbType = dbType;
  }

  public DateValue(Date date)
  {
    this.date = date;
  }

  public Date toDate() throws TypeMismatchException
  {
    return date;
  }

  public Time toTime() throws TypeMismatchException
  {
    return new Time(date.getTime());
  }

  public Timestamp toTimestamp() throws TypeMismatchException
  {
    return new Timestamp(date.getTime());
  }

  public Timestamp toDatetime() throws TypeMismatchException
  {
    return new Timestamp(date.getTime());
  }

  public Object toObject() throws TypeMismatchException
  {
    return toDate();
  }

  public String toString()
  {
    return date.toString();
  }

  public Date[] toDateArray() throws TypeMismatchException
  {
    return new Date[] { date };
  }

  public Time[] toTimeArray() throws TypeMismatchException
  {
    return new Time[] { toTime() };
  }

  public Timestamp[] toTimestampArray() throws TypeMismatchException
  {
    return new Timestamp[] { toTimestamp() };
  }

  public Timestamp[] toDatetimeArray() throws TypeMismatchException
  {
    return new Timestamp[] { toTimestamp() };
  }

  public Object[] toObjectArray() throws TypeMismatchException
  {
    return new Object[] { toDate() };
  }

  public String[] toStringArray() throws TypeMismatchException
  {
    return new String[] { toString() };
  }
}
