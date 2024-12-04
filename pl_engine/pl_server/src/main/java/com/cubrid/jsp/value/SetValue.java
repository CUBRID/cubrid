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
import cubrid.sql.CUBRIDOID;
import java.math.BigDecimal;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;

public class SetValue extends Value {

    protected String getTypeName() {
        return TYPE_NAME_SET;
    }

    private Object[] values;

    public SetValue(Value[] args) throws TypeMismatchException {
        super();
        values = toObjectArray(args);
    }

    public SetValue(Value[] args, int mode, int dbType) throws TypeMismatchException {
        super(mode);
        values = toObjectArray(args);
        this.dbType = dbType;
    }

    public SetValue(Object[] objects) {
        this.values = objects;
    }

    public SetValue(byte[] objects) {
        Object[] array = new Object[objects.length];
        for (int i = 0; i < objects.length; i++) {
            array[i] = new Byte(objects[i]);
        }
        this.values = array;
    }

    public SetValue(short[] objects) {
        Object[] array = new Object[objects.length];
        for (int i = 0; i < objects.length; i++) {
            array[i] = new Short(objects[i]);
        }
        this.values = array;
    }

    public SetValue(int[] objects) {
        Object[] array = new Object[objects.length];
        for (int i = 0; i < objects.length; i++) {
            array[i] = new Integer(objects[i]);
        }
        this.values = array;
    }

    public SetValue(long[] objects) {
        Object[] array = new Object[objects.length];
        for (int i = 0; i < objects.length; i++) {
            array[i] = new Long(objects[i]);
        }
        this.values = array;
    }

    public SetValue(float[] objects) {
        Object[] array = new Object[objects.length];
        for (int i = 0; i < objects.length; i++) {
            array[i] = new Float(objects[i]);
        }
        this.values = array;
    }

    public SetValue(double[] objects) {
        Object[] array = new Object[objects.length];
        for (int i = 0; i < objects.length; i++) {
            array[i] = new Double(objects[i]);
        }
        this.values = array;
    }

    private Object[] toObjectArray(Value[] args) throws TypeMismatchException {
        Object[] array = new Object[args.length];

        for (int i = 0; i < args.length; i++) {
            array[i] = args[i].toObject();
        }
        return array;
    }

    @Override
    public Object[] toObjectArray() throws TypeMismatchException {
        return values;
    }

    @Override
    public Object toObject() throws TypeMismatchException {
        return values;
    }

    @Override
    public String toString() {
        StringBuffer buf = new StringBuffer();

        buf.append("{");
        for (int i = 0; i < values.length; i++) {
            if (values[i] == null) {
                buf.append("null");
            } else {
                buf.append(values[i].toString());
            }
            if (i < values.length - 1) {
                buf.append(", ");
            }
        }
        buf.append("}");
        return buf.toString();
    }

    @Override
    public byte[] toByteArray() throws TypeMismatchException {
        byte[] array = new byte[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = ((Byte) values[i]).byteValue();
        }
        return array;
    }

    @Override
    public short[] toShortArray() throws TypeMismatchException {
        short[] array = new short[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = ((Short) values[i]).shortValue();
        }
        return array;
    }

    @Override
    public int[] toIntegerArray() throws TypeMismatchException {
        int[] array = new int[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = ((Integer) values[i]).intValue();
        }
        return array;
    }

    @Override
    public long[] toLongArray() throws TypeMismatchException {
        long[] array = new long[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = ((Long) values[i]).longValue();
        }
        return array;
    }

    @Override
    public float[] toFloatArray() throws TypeMismatchException {
        float[] array = new float[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = ((Float) values[i]).floatValue();
        }
        return array;
    }

    @Override
    public double[] toDoubleArray() throws TypeMismatchException {
        double[] array = new double[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = ((Double) values[i]).doubleValue();
        }
        return array;
    }

    @Override
    public BigDecimal[] toBigDecimalArray() throws TypeMismatchException {
        BigDecimal[] array = new BigDecimal[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = (BigDecimal) values[i];
        }
        return array;
    }

    @Override
    public Date[] toDateArray() throws TypeMismatchException {
        Date[] array = new Date[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = (Date) values[i];
        }
        return array;
    }

    @Override
    public Time[] toTimeArray() throws TypeMismatchException {
        Time[] array = new Time[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = (Time) values[i];
        }
        return array;
    }

    @Override
    public Timestamp[] toTimestampArray() throws TypeMismatchException {
        Timestamp[] array = new Timestamp[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = (Timestamp) values[i];
        }
        return array;
    }

    @Override
    public Timestamp[] toDatetimeArray() throws TypeMismatchException {
        Timestamp[] array = new Timestamp[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = (Timestamp) values[i];
        }
        return array;
    }

    @Override
    public String[] toStringArray() throws TypeMismatchException {
        String[] array = new String[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = (String) values[i];
        }
        return array;
    }

    @Override
    public Byte[] toByteObjArray() throws TypeMismatchException {
        Byte[] array = new Byte[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = (Byte) values[i];
        }
        return array;
    }

    @Override
    public Double[] toDoubleObjArray() throws TypeMismatchException {
        Double[] array = new Double[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = (Double) values[i];
        }
        return array;
    }

    @Override
    public Float[] toFloatObjArray() throws TypeMismatchException {
        Float[] array = new Float[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = (Float) values[i];
        }
        return array;
    }

    @Override
    public Integer[] toIntegerObjArray() throws TypeMismatchException {
        Integer[] array = new Integer[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = (Integer) values[i];
        }
        return array;
    }

    @Override
    public Long[] toLongObjArray() throws TypeMismatchException {
        Long[] array = new Long[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = (Long) values[i];
        }
        return array;
    }

    @Override
    public Short[] toShortObjArray() throws TypeMismatchException {
        Short[] array = new Short[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = (Short) values[i];
        }
        return array;
    }

    @Override
    public CUBRIDOID[] toOidArray() throws TypeMismatchException {
        CUBRIDOID[] array = new CUBRIDOID[values.length];

        for (int i = 0; i < values.length; i++) {
            array[i] = (CUBRIDOID) values[i];
        }
        return array;
    }
}
