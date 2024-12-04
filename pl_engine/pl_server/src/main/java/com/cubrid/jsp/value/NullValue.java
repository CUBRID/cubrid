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

public class NullValue extends Value {

    protected String getTypeName() {
        return TYPE_NAME_NULL;
    }

    public NullValue() {
        super();
    }

    public NullValue(int mode, int dbType) {
        super(mode);
        this.dbType = dbType;
    }

    @Override
    public Byte toByteObject() throws TypeMismatchException {
        return null;
    }

    @Override
    public byte[] toByteArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new byte[1];
    }

    @Override
    public Short toShortObject() throws TypeMismatchException {
        return null;
    }

    @Override
    public short[] toShortArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new short[1];
    }

    @Override
    public Integer toIntegerObject() throws TypeMismatchException {
        return null;
    }

    @Override
    public int[] toIntegerArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new int[1];
    }

    @Override
    public Long toLongObject() throws TypeMismatchException {
        return null;
    }

    @Override
    public long[] toLongArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new long[1];
    }

    @Override
    public Float toFloatObject() throws TypeMismatchException {
        return null;
    }

    @Override
    public float[] toFloatArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new float[1];
    }

    @Override
    public Double toDoubleObject() throws TypeMismatchException {
        return null;
    }

    @Override
    public double[] toDoubleArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new double[1];
    }

    @Override
    public Object toObject() throws TypeMismatchException {
        return null;
    }

    @Override
    public Object[] toObjectArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new Object[1];
    }

    @Override
    public Date toDate() throws TypeMismatchException {
        return null;
    }

    @Override
    public Date[] toDateArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new Date[1];
    }

    @Override
    public Time toTime() throws TypeMismatchException {
        return null;
    }

    @Override
    public Time[] toTimeArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new Time[1];
    }

    @Override
    public Timestamp toTimestamp() throws TypeMismatchException {
        return null;
    }

    @Override
    public Timestamp[] toTimestampArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new Timestamp[1];
    }

    @Override
    public Timestamp toDatetime() throws TypeMismatchException {
        return null;
    }

    @Override
    public Timestamp[] toDatetimeArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new Timestamp[1];
    }

    @Override
    public BigDecimal toBigDecimal() throws TypeMismatchException {
        return null;
    }

    @Override
    public BigDecimal[] toBigDecimalArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new BigDecimal[1];
    }

    @Override
    public String[] toStringArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new String[1];
    }

    @Override
    public String toString() {
        return null;
    }

    @Override
    public Byte[] toByteObjArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new Byte[1];
    }

    @Override
    public Double[] toDoubleObjArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new Double[1];
    }

    @Override
    public Float[] toFloatObjArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new Float[1];
    }

    @Override
    public Integer[] toIntegerObjArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new Integer[1];
    }

    @Override
    public Long[] toLongObjArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new Long[1];
    }

    @Override
    public Short[] toShortObjArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new Short[1];
    }

    @Override
    public CUBRIDOID toOid() throws TypeMismatchException {
        return null;
    }

    @Override
    public CUBRIDOID[] toOidArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new CUBRIDOID[1];
    }

    @Override
    public ResultSet toResultSet(SUConnection ucon) throws TypeMismatchException {
        return null;
    }

    @Override
    public ResultSet[] toResultSetArray(SUConnection ucon) throws TypeMismatchException {
        if (mode == Value.IN) return null;
        return new ResultSet[1];
    }

    @Override
    public BigDecimal[][] toBigDecimalArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new BigDecimal[1][];
    }

    @Override
    public byte[][] toByteArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new byte[1][];
    }

    @Override
    public Byte[][] toByteObjArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new Byte[1][];
    }

    @Override
    public Date[][] toDateArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new Date[1][];
    }

    @Override
    public double[][] toDoubleArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new double[1][];
    }

    @Override
    public Double[][] toDoubleObjArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new Double[1][];
    }

    @Override
    public float[][] toFloatArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new float[1][];
    }

    @Override
    public Float[][] toFloatObjArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new Float[1][];
    }

    @Override
    public int[][] toIntegerArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new int[1][];
    }

    @Override
    public Integer[][] toIntegerObjArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new Integer[1][];
    }

    @Override
    public long[][] toLongArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new long[1][];
    }

    @Override
    public Long[][] toLongObjArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new Long[1][];
    }

    @Override
    public Object[][] toObjectArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new Long[1][];
    }

    @Override
    public CUBRIDOID[][] toOidArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new CUBRIDOID[1][];
    }

    @Override
    public short[][] toShortArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new short[1][];
    }

    @Override
    public Short[][] toShortObjArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new Short[1][];
    }

    @Override
    public String[][] toStringArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new String[1][];
    }

    @Override
    public Time[][] toTimeArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new Time[1][];
    }

    @Override
    public Timestamp[][] toTimestampArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new Timestamp[1][];
    }

    @Override
    public Timestamp[][] toDatetimeArrayArray() throws TypeMismatchException {
        if (mode == Value.IN) return null;

        return new Timestamp[1][];
    }
}
