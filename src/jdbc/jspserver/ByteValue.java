package com.cubrid.jsp.value;

import java.math.BigDecimal;

import com.cubrid.jsp.exception.TypeMismatchException;

public class ByteValue extends Value {

    private byte value;

    public ByteValue(byte value) {
        super();
        this.value = value;
    }

    public ByteValue(byte value, int mode) {
        super(mode);
        this.value = value;
    }

    public byte toByte() throws TypeMismatchException {
        return value;
    }

    public short toShort() throws TypeMismatchException {
        return value;
    }

    public int toInt() throws TypeMismatchException {
        return value;
    }

    public long toLong() throws TypeMismatchException {
        return value;
    }

    public float toFloat() throws TypeMismatchException {
        return value;
    }

    public double toDouble() throws TypeMismatchException {
        return value;
    }

    public Byte toByteObject() throws TypeMismatchException {
        return new Byte(value);
    }

    public Short toShortObject() throws TypeMismatchException {
        return new Short(value);
    }

    public Integer toIntegerObject() throws TypeMismatchException {
        return new Integer(value);
    }

    public Long toLongObject() throws TypeMismatchException {
        return new Long(value);
    }

    public Float toFloatObject() throws TypeMismatchException {
        return new Float(value);
    }

    public Double toDoubleObject() throws TypeMismatchException {
        return new Double(value);
    }

    public BigDecimal toBigDecimal() throws TypeMismatchException {
        return new BigDecimal(value);
    }

    public Object toObject() throws TypeMismatchException {
        return toByteObject();
    }

    public String toString() {
        return "" + value;
    }
    
    public byte[] toByteArray() throws TypeMismatchException {
        return new byte[] {value};
    }

    public short[] toShortArray() throws TypeMismatchException {
        return new short[] {value};
    }

    public int[] toIntegerArray() throws TypeMismatchException {
        return new int[] {value};
    }

    public long[] toLongArray() throws TypeMismatchException {
        return new long[] {value};
    }

    public float[] toFloatArray() throws TypeMismatchException {
        return new float[] {value};
    }

    public double[] toDoubleArray() throws TypeMismatchException {
        return new double[] {value};
    }

    public BigDecimal[] toBigDecimalArray() throws TypeMismatchException {
        return new BigDecimal[] {toBigDecimal()};
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

