package com.cubrid.jsp.value;

import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;
import java.util.Calendar;

import com.cubrid.jsp.exception.TypeMismatchException;

public class DateValue extends Value {

    private Date date;

    public DateValue(int year, int mon, int day) {
        super();
        Calendar cal = Calendar.getInstance();
        cal.set(year, mon, day);
        
        date = new Date(cal.getTimeInMillis());
    }

    public DateValue(int year, int mon, int day, int mode, int dbType) {
        super(mode);
        Calendar cal = Calendar.getInstance();
        cal.set(year, mon, day);
        
        date = new Date(cal.getTimeInMillis());
        this.dbType = dbType;
    }

    public DateValue(Date date) {
        this.date = date;
    }

    public Date toDate() throws TypeMismatchException {
        return date;
    }

    public Time toTime() throws TypeMismatchException {
        return new Time(date.getTime());
    }

    public Timestamp toTimestamp() throws TypeMismatchException {
        return new Timestamp(date.getTime());
    }

    public Object toObject() throws TypeMismatchException {
        return toDate();
    }
    
    public String toString() {
        return date.toString();
    }

    public Date[] toDateArray() throws TypeMismatchException {
        return new Date[] {date};
    }

    public Time[] toTimeArray() throws TypeMismatchException {
        return new Time[] {toTime()};
    }

    public Timestamp[] toTimestampArray() throws TypeMismatchException {
        return new Timestamp[] {toTimestamp()};
    }

    public Object[] toObjectArray() throws TypeMismatchException {
        return new Object[] {toDate()};
    }
    
    public String[] toStringArray() throws TypeMismatchException {
        return new String[] {toString()};
    }
}

