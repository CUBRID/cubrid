package com.cubrid.jsp.value;

import java.math.BigDecimal;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;

import com.cubrid.jsp.exception.TypeMismatchException;

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

    public byte toByte() throws TypeMismatchException {
        try {
            return Byte.parseByte(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public short toShort() throws TypeMismatchException {
        try {
            return Short.parseShort(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public int toInt() throws TypeMismatchException {
        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public long toLong() throws TypeMismatchException {
        try {
            return Long.parseLong(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public float toFloat() throws TypeMismatchException {
        try {
            return Float.parseFloat(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public double toDouble() throws TypeMismatchException {
        try {
            return Double.parseDouble(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public Byte toByteObject() throws TypeMismatchException {
        try {
            return Byte.valueOf(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public Short toShortObject() throws TypeMismatchException {
        try {
            return Short.valueOf(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public Integer toIntegerObject() throws TypeMismatchException {
        try {
            return Integer.valueOf(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public Long toLongObject() throws TypeMismatchException {
        try {
            return Long.valueOf(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public Float toFloatObject() throws TypeMismatchException {
        try {
            return Float.valueOf(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public Double toDoubleObject() throws TypeMismatchException {
        try {
            return Double.valueOf(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public Date toDate() throws TypeMismatchException {
        try {
            return Date.valueOf(value);
        } catch (IllegalArgumentException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public Time toTime() throws TypeMismatchException {
        try {
            return Time.valueOf(value);
        } catch (IllegalArgumentException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public Timestamp toTimestamp() throws TypeMismatchException {
        try {
            return Timestamp.valueOf(value);
        } catch (IllegalArgumentException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public BigDecimal toBigDecimal() throws TypeMismatchException {
        try {
            return new BigDecimal(value);
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public Object toObject() throws TypeMismatchException {
        return toString();
    }

    public String toString() {
        return value;
    }

    public byte[] toByteArray() throws TypeMismatchException {
        return new byte[] {toByteObject().byteValue()};
    }

    public short[] toShortArray() throws TypeMismatchException {
        return new short[] {toShortObject().shortValue()};
    }

    public int[] toIntegerArray() throws TypeMismatchException {
        return new int[] {toIntegerObject().intValue()};
    }

    public long[] toLongArray() throws TypeMismatchException {
        return new long[] {toLongObject().longValue()};
    }

    public float[] toFloatArray() throws TypeMismatchException {
        return new float[] {toFloatObject().floatValue()};
    }

    public double[] toDoubleArray() throws TypeMismatchException {
        return new double[] {toDoubleObject().doubleValue()};
    }

    public BigDecimal[] toBigDecimalArray() throws TypeMismatchException {
        return new BigDecimal[] {toBigDecimal()};
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

    public Byte[] toByteObjArray() throws TypeMismatchException {
        return new Byte[] {toByteObject()};
    }

    public Double[] toDoubleObjArray() throws TypeMismatchException {
        return new Double[] {toDoubleObject()};
    }

    public Float[] toFloatObjArray() throws TypeMismatchException {
        return new Float[] {toFloatObject()};
    }

    public Integer[] toIntegerObjArray() throws TypeMismatchException {
        return new Integer[] {toIntegerObject()};
    }

    public Long[] toLongObjArray() throws TypeMismatchException {
        return new Long[] {toLongObject()};
    }

    public Short[] toShortObjArray() throws TypeMismatchException {
        return new Short[] {toShortObject()};
    }
}

