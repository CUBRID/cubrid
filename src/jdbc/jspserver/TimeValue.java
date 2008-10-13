package com.cubrid.jsp.value;

import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;
import java.util.Calendar;

import com.cubrid.jsp.exception.TypeMismatchException;

public class TimeValue extends Value {

    private Time time;

    public TimeValue(int hour, int min, int sec) {
        super();
        Calendar cal = Calendar.getInstance();
        cal.set(0, 0, 0, hour, min, sec);
        
        this.time = new Time(cal.getTimeInMillis());
    }

    public TimeValue(int hour, int min, int sec, int mode, int dbType) {
        super(mode);
        Calendar cal = Calendar.getInstance();
        cal.set(0, 0, 0, hour, min, sec);
        
        this.time = new Time(cal.getTimeInMillis());
        this.dbType = dbType;
    }

    public TimeValue(Time time) {
        this.time = time;
    }

    public Date toDate() throws TypeMismatchException {
        return new Date(time.getTime());
    }

    public Time toTime() throws TypeMismatchException {
        return time;
    }

    public Timestamp toTimestamp() throws TypeMismatchException {
        return new Timestamp(time.getTime());
    }

    public Object toObject() throws TypeMismatchException {
        return toTime();
    }
    
    public String toString() {
        return time.toString();
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

