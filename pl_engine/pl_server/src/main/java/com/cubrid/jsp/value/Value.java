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
import com.cubrid.jsp.impl.SUConnection;
import cubrid.sql.CUBRIDOID;
import java.math.BigDecimal;
import java.sql.Date;
import java.sql.ResultSet;
import java.sql.Time;
import java.sql.Timestamp;

public abstract class Value {
    public static final int IN = 1;
    public static final int OUT = 2;
    public static final int INOUT = 3;

    protected int mode;
    protected Object resolved;
    protected int dbType;

    public Value() {
        this.mode = IN;
    }

    public Value(int mode) {
        this.mode = mode;
    }

    public void setMode(int mode) {
        this.mode = mode;
    }

    public int getMode() {
        return mode;
    }

    public byte toByte() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public short toShort() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public int toInt() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public long toLong() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public float toFloat() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public double toDouble() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Byte toByteObject() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public byte[] toByteArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public byte[][] toByteArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Short toShortObject() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public short[] toShortArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public short[][] toShortArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Integer toIntegerObject() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public int[] toIntegerArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public int[][] toIntegerArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Long toLongObject() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public long[] toLongArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public long[][] toLongArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Float toFloatObject() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public float[] toFloatArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public float[][] toFloatArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Double toDoubleObject() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public double[] toDoubleArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public double[][] toDoubleArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Object toObject() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Object[] toObjectArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Object[][] toObjectArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Date toDate() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Date[] toDateArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Date[][] toDateArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Time toTime() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Time[] toTimeArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Time[][] toTimeArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Timestamp toTimestamp() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Timestamp[] toTimestampArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Timestamp[][] toTimestampArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Timestamp toDatetime() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Timestamp[] toDatetimeArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Timestamp[][] toDatetimeArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public BigDecimal toBigDecimal() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public BigDecimal[] toBigDecimalArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public BigDecimal[][] toBigDecimalArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public String[] toStringArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public String[][] toStringArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Byte[] toByteObjArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Byte[][] toByteObjArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Short[] toShortObjArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Short[][] toShortObjArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Integer[] toIntegerObjArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Integer[][] toIntegerObjArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Long[] toLongObjArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Long[][] toLongObjArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Float[] toFloatObjArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Float[][] toFloatObjArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Double[] toDoubleObjArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Double[][] toDoubleObjArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public CUBRIDOID toOid() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public CUBRIDOID[] toOidArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public CUBRIDOID[][] toOidArrayArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public ResultSet toResultSet(SUConnection ucon) throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public ResultSet[] toResultSetArray(SUConnection ucon) throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public ResultSet[][] toResultSetArrayArray(SUConnection ucon) throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public void setResolved(Object val) {
        resolved = val;
    }

    public Object getResolved() {
        return resolved;
    }

    public int getDbType() {
        return dbType;
    }

    public void setDbType(int type) {
        dbType = type;
    }
}
