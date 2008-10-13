package com.cubrid.jsp.value;

import java.math.BigDecimal;
import java.sql.Date;
import java.sql.ResultSet;
import java.sql.Time;
import java.sql.Timestamp;

import com.cubrid.jsp.exception.TypeMismatchException;

import cubrid.sql.CUBRIDOID;


public class NullValue extends Value {

    public NullValue() {
        super();
    }

    public NullValue(int mode, int dbType) {
        super(mode);
        this.dbType = dbType;
    }

    public Byte toByteObject() throws TypeMismatchException {
        return null;
    }

    public byte[] toByteArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new byte[1];
    }

    public Short toShortObject() throws TypeMismatchException {
        return null;
    }

    public short[] toShortArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new short[1];
    }

    public Integer toIntegerObject() throws TypeMismatchException {
        return null;
    }

    public int[] toIntegerArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new int[1];
    }

    public Long toLongObject() throws TypeMismatchException {
        return null;
    }

    public long[] toLongArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new long[1];
    }

    public Float toFloatObject() throws TypeMismatchException {
        return null;
    }

    public float[] toFloatArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new float[1];
    }

    public Double toDoubleObject() throws TypeMismatchException {
        return null;
    }

    public double[] toDoubleArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new double[1];
    }

    public Object toObject() throws TypeMismatchException {
        return null;
    }

    public Object[] toObjectArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new Object[1];
    }

    public Date toDate() throws TypeMismatchException {
        return null;
    }

    public Date[] toDateArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new Date[1];
    }

    public Time toTime() throws TypeMismatchException {
        return null;
    }

    public Time[] toTimeArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new Time[1];
    }

    public Timestamp toTimestamp() throws TypeMismatchException {
        return null;
    }

    public Timestamp[] toTimestampArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new Timestamp[1];
    }

    public BigDecimal toBigDecimal() throws TypeMismatchException {
        return null;
    }

    public BigDecimal[] toBigDecimalArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new BigDecimal[1];
    }
    
    public String[] toStringArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new String[1];
    }    

    public String toString(){
        return null;
    }

    public Byte[] toByteObjArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new Byte[1];
    }

    public Double[] toDoubleObjArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new Double[1];
    }

    public Float[] toFloatObjArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new Float[1];
    }

    public Integer[] toIntegerObjArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new Integer[1];
    }

    public Long[] toLongObjArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new Long[1];
    }

    public Short[] toShortObjArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new Short[1];
    }

    public CUBRIDOID toOid() throws TypeMismatchException {
        return null;
    }

    public CUBRIDOID[] toOidArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new CUBRIDOID[1];
    }

    public ResultSet toResultSet() throws TypeMismatchException {
        return null;
    }

    public ResultSet[] toResultSetArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        return new ResultSet[1];
    }

    public BigDecimal[][] toBigDecimalArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new BigDecimal[1][];
    }

    public byte[][] toByteArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new byte[1][];
    }

    public Byte[][] toByteObjArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new Byte[1][];
    }

    public Date[][] toDateArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new Date[1][];
    }

    public double[][] toDoubleArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new double[1][];
    }

    public Double[][] toDoubleObjArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new Double[1][];
    }

    public float[][] toFloatArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new float[1][];
    }

    public Float[][] toFloatObjArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new Float[1][];
    }

    public int[][] toIntegerArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new int[1][];
    }

    public Integer[][] toIntegerObjArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new Integer[1][];
    }

    public long[][] toLongArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new long[1][];
    }

    public Long[][] toLongObjArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new Long[1][];
    }

    public Object[][] toObjectArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new Long[1][];
    }

    public CUBRIDOID[][] toOidArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new CUBRIDOID[1][];
    }

    public short[][] toShortArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new short[1][];
    }

    public Short[][] toShortObjArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new Short[1][];
    }

    public String[][] toStringArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new String[1][];
    }

    public Time[][] toTimeArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new Time[1][];
    }

    public Timestamp[][] toTimestampArrayArray() throws TypeMismatchException {
        if (mode == Value.IN)
            return null;
        
        return new Timestamp[1][];
    }
    
    
}
