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

import com.cubrid.jsp.data.DBType;
import com.cubrid.jsp.exception.ExecuteException;
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

    private Value[] values;
    private Object[] objects;

    public SetValue(Value[] args) throws TypeMismatchException {
        super();
        this.values = args;
        this.objects = toJavaObjectArray(args);
        this.dbType = DBType.DB_SET;
    }

    public SetValue(Object[] objects) throws TypeMismatchException {
        this.objects = objects;
        setValuesFromObjects();
    }

    private void setValuesFromObjects() throws TypeMismatchException {
        if (objects != null) {
            this.values = new Value[objects.length];
            for (int i = 0; i < objects.length; i++) {
                try {
                    this.values[i] = ValueUtilities.createValueFrom(objects[i]);
                } catch (ExecuteException e) {
                    this.values[i] = new NullValue();
                    assert false; // This should never happen
                }
            }
        }
        this.dbType = DBType.DB_SET;
    }

    public SetValue(byte[] objects) throws TypeMismatchException {
        Object[] array = new Object[objects.length];
        for (int i = 0; i < objects.length; i++) {
            array[i] = new Byte(objects[i]);
        }
        this.objects = array;
        this.dbType = DBType.DB_SET;
        setValuesFromObjects();
    }

    public SetValue(short[] objects) throws TypeMismatchException {
        Object[] array = new Object[objects.length];
        for (int i = 0; i < objects.length; i++) {
            array[i] = new Short(objects[i]);
        }
        this.objects = array;
        setValuesFromObjects();
    }

    public SetValue(int[] objects) throws TypeMismatchException {
        Object[] array = new Object[objects.length];
        for (int i = 0; i < objects.length; i++) {
            array[i] = new Integer(objects[i]);
        }
        this.objects = array;
        setValuesFromObjects();
    }

    public SetValue(long[] objects) throws TypeMismatchException {
        Object[] array = new Object[objects.length];
        for (int i = 0; i < objects.length; i++) {
            array[i] = new Long(objects[i]);
        }
        this.objects = array;
        setValuesFromObjects();
    }

    public SetValue(float[] objects) throws TypeMismatchException {
        Object[] array = new Object[objects.length];
        for (int i = 0; i < objects.length; i++) {
            array[i] = new Float(objects[i]);
        }
        this.objects = array;
        setValuesFromObjects();
    }

    public SetValue(double[] objects) throws TypeMismatchException {
        Object[] array = new Object[objects.length];
        for (int i = 0; i < objects.length; i++) {
            array[i] = new Double(objects[i]);
        }
        this.objects = array;
        setValuesFromObjects();
    }

    private Object[] toJavaObjectArray(Value[] args) throws TypeMismatchException {
        Object[] array = new Object[args.length];

        for (int i = 0; i < args.length; i++) {
            array[i] = args[i].toObject();
        }
        return array;
    }

    public Value[] toValueArray() throws TypeMismatchException {
        return values;
    }

    @Override
    public Object[] toObjectArray() throws TypeMismatchException {
        return objects;
    }

    @Override
    public Object toObject() throws TypeMismatchException {
        return objects;
    }

    @Override
    public String toString() {
        StringBuffer buf = new StringBuffer();

        buf.append("{");
        for (int i = 0; i < objects.length; i++) {
            if (objects[i] == null) {
                buf.append("null");
            } else {
                buf.append(objects[i].toString());
            }
            if (i < objects.length - 1) {
                buf.append(", ");
            }
        }
        buf.append("}");
        return buf.toString();
    }

    @Override
    public byte[] toByteArray() throws TypeMismatchException {
        byte[] array = new byte[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = ((Byte) objects[i]).byteValue();
        }
        return array;
    }

    @Override
    public short[] toShortArray() throws TypeMismatchException {
        short[] array = new short[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = ((Short) objects[i]).shortValue();
        }
        return array;
    }

    @Override
    public int[] toIntegerArray() throws TypeMismatchException {
        int[] array = new int[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = ((Integer) objects[i]).intValue();
        }
        return array;
    }

    @Override
    public long[] toLongArray() throws TypeMismatchException {
        long[] array = new long[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = ((Long) objects[i]).longValue();
        }
        return array;
    }

    @Override
    public float[] toFloatArray() throws TypeMismatchException {
        float[] array = new float[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = ((Float) objects[i]).floatValue();
        }
        return array;
    }

    @Override
    public double[] toDoubleArray() throws TypeMismatchException {
        double[] array = new double[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = ((Double) objects[i]).doubleValue();
        }
        return array;
    }

    @Override
    public BigDecimal[] toBigDecimalArray() throws TypeMismatchException {
        BigDecimal[] array = new BigDecimal[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = (BigDecimal) objects[i];
        }
        return array;
    }

    @Override
    public Date[] toDateArray() throws TypeMismatchException {
        Date[] array = new Date[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = (Date) objects[i];
        }
        return array;
    }

    @Override
    public Time[] toTimeArray() throws TypeMismatchException {
        Time[] array = new Time[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = (Time) objects[i];
        }
        return array;
    }

    @Override
    public Timestamp[] toTimestampArray() throws TypeMismatchException {
        Timestamp[] array = new Timestamp[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = (Timestamp) objects[i];
        }
        return array;
    }

    @Override
    public Timestamp[] toDatetimeArray() throws TypeMismatchException {
        Timestamp[] array = new Timestamp[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = (Timestamp) objects[i];
        }
        return array;
    }

    @Override
    public String[] toStringArray() throws TypeMismatchException {
        String[] array = new String[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = (String) objects[i];
        }
        return array;
    }

    @Override
    public Byte[] toByteObjArray() throws TypeMismatchException {
        Byte[] array = new Byte[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = (Byte) objects[i];
        }
        return array;
    }

    @Override
    public Double[] toDoubleObjArray() throws TypeMismatchException {
        Double[] array = new Double[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = (Double) objects[i];
        }
        return array;
    }

    @Override
    public Float[] toFloatObjArray() throws TypeMismatchException {
        Float[] array = new Float[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = (Float) objects[i];
        }
        return array;
    }

    @Override
    public Integer[] toIntegerObjArray() throws TypeMismatchException {
        Integer[] array = new Integer[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = (Integer) objects[i];
        }
        return array;
    }

    @Override
    public Long[] toLongObjArray() throws TypeMismatchException {
        Long[] array = new Long[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = (Long) objects[i];
        }
        return array;
    }

    @Override
    public Short[] toShortObjArray() throws TypeMismatchException {
        Short[] array = new Short[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = (Short) objects[i];
        }
        return array;
    }

    @Override
    public CUBRIDOID[] toOidArray() throws TypeMismatchException {
        CUBRIDOID[] array = new CUBRIDOID[objects.length];

        for (int i = 0; i < objects.length; i++) {
            array[i] = (CUBRIDOID) objects[i];
        }
        return array;
    }
}
