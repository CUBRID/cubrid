package com.cubrid.jsp.value;

import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;
import java.util.Calendar;

import com.cubrid.jsp.exception.TypeMismatchException;

public class TimestampValue extends Value {

    private Timestamp timestamp;

    public TimestampValue(int year, int mon, int day, int hour, int min,
            int sec) {
        super();
        Calendar cal = Calendar.getInstance();
        cal.set(year, mon, day, hour, min, sec);

        this.timestamp = new Timestamp(cal.getTimeInMillis());
    }

    public TimestampValue(int year, int mon, int day, int hour, int min,
            int sec, int mode, int dbType) {
        super(mode);
        Calendar cal = Calendar.getInstance();
        cal.set(year, mon, day, hour, min, sec);

        this.timestamp = new Timestamp(cal.getTimeInMillis());
        this.dbType = dbType;
    }

    public TimestampValue(Timestamp timestamp) {
        this.timestamp = timestamp;
    }

    public Date toDate() throws TypeMismatchException {
        return new Date(timestamp.getTime());
    }

    public Time toTime() throws TypeMismatchException {
        return new Time(timestamp.getTime());
    }

    public Timestamp toTimestamp() throws TypeMismatchException {
        return timestamp;
    }

    public Object toDefault() throws TypeMismatchException {
        return toTimestamp();
    }

    public String toString() {
        return timestamp.toString();
    }
    
    public Date[] toDateArray() throws TypeMismatchException {
        return new Date[] {toDate()};
    }

    public Time[] toTimeArray() throws TypeMismatchException {
        return new Time[] {toTime()};
    }

    public Timestamp[] toTimestampArray() throws TypeMismatchException {
        return new Timestamp[] {toTimestamp()};
    }

    public Object[] toObjectArray() throws TypeMismatchException {
        return new Object[] {toObject()};
    }
    
    public String[] toStringArray() throws TypeMismatchException {
        return new String[] {toString()};
    }
}

