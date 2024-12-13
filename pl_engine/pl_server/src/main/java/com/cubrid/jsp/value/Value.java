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

import com.cubrid.jsp.Server;
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
    protected int codeset;

    public Value() {
        this.mode = IN;
        this.codeset = Server.getConfig().getServerCodesetId();
    }

    public void setMode(int mode) {
        this.mode = mode;
    }

    public int getMode() {
        return mode;
    }

    public byte toByte() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_BYTE));
    }

    public short toShort() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_SHORT));
    }

    public int toInt() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_INT));
    }

    public long toLong() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_BIGINT));
    }

    public float toFloat() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_FLOAT));
    }

    public double toDouble() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_DOUBLE));
    }

    public Byte toByteObject() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_BYTE));
    }

    public byte[] toByteArray() throws TypeMismatchException {
        return new byte[] {toByte()};
    }

    public byte[][] toByteArrayArray() throws TypeMismatchException {
        return new byte[][] {toByteArray()};
    }

    public Short toShortObject() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_SHORT));
    }

    public short[] toShortArray() throws TypeMismatchException {
        return new short[] {toShort()};
    }

    public short[][] toShortArrayArray() throws TypeMismatchException {
        return new short[][] {toShortArray()};
    }

    public Integer toIntegerObject() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_INT));
    }

    public int[] toIntegerArray() throws TypeMismatchException {
        return new int[] {toInt()};
    }

    public int[][] toIntegerArrayArray() throws TypeMismatchException {
        return new int[][] {toIntegerArray()};
    }

    public Long toLongObject() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_BIGINT));
    }

    public long[] toLongArray() throws TypeMismatchException {
        return new long[] {toLong()};
    }

    public long[][] toLongArrayArray() throws TypeMismatchException {
        return new long[][] {toLongArray()};
    }

    public Float toFloatObject() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_FLOAT));
    }

    public float[] toFloatArray() throws TypeMismatchException {
        return new float[] {toFloat()};
    }

    public float[][] toFloatArrayArray() throws TypeMismatchException {
        return new float[][] {toFloatArray()};
    }

    public Double toDoubleObject() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_DOUBLE));
    }

    public double[] toDoubleArray() throws TypeMismatchException {
        return new double[] {toDouble()};
    }

    public double[][] toDoubleArrayArray() throws TypeMismatchException {
        return new double[][] {toDoubleArray()};
    }

    public Object toObject() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_OBJECT));
    }

    public Object[] toObjectArray() throws TypeMismatchException {
        return new Object[] {toObject()};
    }

    public Object[][] toObjectArrayArray() throws TypeMismatchException {
        return new Object[][] {toObjectArray()};
    }

    public Date toDate() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_DATE));
    }

    public Date[] toDateArray() throws TypeMismatchException {
        return new Date[] {toDate()};
    }

    public Date[][] toDateArrayArray() throws TypeMismatchException {
        return new Date[][] {toDateArray()};
    }

    public Time toTime() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_TIME));
    }

    public Time[] toTimeArray() throws TypeMismatchException {
        return new Time[] {toTime()};
    }

    public Time[][] toTimeArrayArray() throws TypeMismatchException {
        return new Time[][] {toTimeArray()};
    }

    public Timestamp toTimestamp() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_TIMESTAMP));
    }

    public Timestamp[] toTimestampArray() throws TypeMismatchException {
        return new Timestamp[] {toTimestamp()};
    }

    public Timestamp[][] toTimestampArrayArray() throws TypeMismatchException {
        return new Timestamp[][] {toTimestampArray()};
    }

    public Timestamp toDatetime() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_DATETIME));
    }

    public Timestamp[] toDatetimeArray() throws TypeMismatchException {
        return new Timestamp[] {toDatetime()};
    }

    public Timestamp[][] toDatetimeArrayArray() throws TypeMismatchException {
        return new Timestamp[][] {toDatetimeArray()};
    }

    public BigDecimal toBigDecimal() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_NUMERIC));
    }

    public BigDecimal[] toBigDecimalArray() throws TypeMismatchException {
        return new BigDecimal[] {toBigDecimal()};
    }

    public BigDecimal[][] toBigDecimalArrayArray() throws TypeMismatchException {
        return new BigDecimal[][] {toBigDecimalArray()};
    }

    public String[] toStringArray() throws TypeMismatchException {
        return new String[] {toString()};
    }

    public String[][] toStringArrayArray() throws TypeMismatchException {
        return new String[][] {toStringArray()};
    }

    public Byte[] toByteObjArray() throws TypeMismatchException {
        return new Byte[] {toByteObject()};
    }

    public Byte[][] toByteObjArrayArray() throws TypeMismatchException {
        return new Byte[][] {toByteObjArray()};
    }

    public Short[] toShortObjArray() throws TypeMismatchException {
        return new Short[] {toShortObject()};
    }

    public Short[][] toShortObjArrayArray() throws TypeMismatchException {
        return new Short[][] {toShortObjArray()};
    }

    public Integer[] toIntegerObjArray() throws TypeMismatchException {
        return new Integer[] {toIntegerObject()};
    }

    public Integer[][] toIntegerObjArrayArray() throws TypeMismatchException {
        return new Integer[][] {toIntegerObjArray()};
    }

    public Long[] toLongObjArray() throws TypeMismatchException {
        return new Long[] {toLongObject()};
    }

    public Long[][] toLongObjArrayArray() throws TypeMismatchException {
        return new Long[][] {toLongObjArray()};
    }

    public Float[] toFloatObjArray() throws TypeMismatchException {
        return new Float[] {toFloatObject()};
    }

    public Float[][] toFloatObjArrayArray() throws TypeMismatchException {
        return new Float[][] {toFloatObjArray()};
    }

    public Double[] toDoubleObjArray() throws TypeMismatchException {
        return new Double[] {toDoubleObject()};
    }

    public Double[][] toDoubleObjArrayArray() throws TypeMismatchException {
        return new Double[][] {toDoubleObjArray()};
    }

    public CUBRIDOID toOid() throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_OID));
    }

    public CUBRIDOID[] toOidArray() throws TypeMismatchException {
        return new CUBRIDOID[] {toOid()};
    }

    public CUBRIDOID[][] toOidArrayArray() throws TypeMismatchException {
        return new CUBRIDOID[][] {toOidArray()};
    }

    public ResultSet toResultSet(SUConnection ucon) throws TypeMismatchException {
        throw new TypeMismatchException(getCastErrMsg(TYPE_NAME_RESULTSET));
    }

    public ResultSet[] toResultSetArray(SUConnection ucon) throws TypeMismatchException {
        return new ResultSet[] {toResultSet(ucon)};
    }

    public ResultSet[][] toResultSetArrayArray(SUConnection ucon) throws TypeMismatchException {
        return new ResultSet[][] {toResultSetArray(ucon)};
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

    public int getCodeSet() {
        return codeset;
    }

    // -----------------------------------------------------

    private String getCastErrMsg(String targetType) {
        return String.format("cannot convert %s to %s", getTypeName(), targetType);
    }

    protected abstract String getTypeName();

    protected static final String TYPE_NAME_NULL = "NULL";
    protected static final String TYPE_NAME_OBJECT = "OBJECT";

    protected static final String TYPE_NAME_BOOLEAN = "BOOLEAN";
    protected static final String TYPE_NAME_STRING = "STRING";

    protected static final String TYPE_NAME_BYTE = "BYTE";
    protected static final String TYPE_NAME_SHORT = "SHORT";
    protected static final String TYPE_NAME_INT = "INT";
    protected static final String TYPE_NAME_BIGINT = "BIGINT";
    protected static final String TYPE_NAME_FLOAT = "FLOAT";
    protected static final String TYPE_NAME_DOUBLE = "DOUBLE";
    protected static final String TYPE_NAME_NUMERIC = "NUMERIC";

    protected static final String TYPE_NAME_DATE = "DATE";
    protected static final String TYPE_NAME_TIME = "TIME";
    protected static final String TYPE_NAME_DATETIME = "DATETIME";
    protected static final String TYPE_NAME_TIMESTAMP = "TIMESTAMP";

    protected static final String TYPE_NAME_OID = "OID";
    protected static final String TYPE_NAME_RESULTSET = "RESULTSET";
    protected static final String TYPE_NAME_SET =
            "COLLECTION"; // actually, it is not only SET but also MULTISET and LIST
}
