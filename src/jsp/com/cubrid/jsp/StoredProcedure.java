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

package com.cubrid.jsp;

import com.cubrid.jsp.exception.ExecuteException;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.value.BooleanValue;
import com.cubrid.jsp.value.ByteValue;
import com.cubrid.jsp.value.DateValue;
import com.cubrid.jsp.value.DoubleValue;
import com.cubrid.jsp.value.FloatValue;
import com.cubrid.jsp.value.IntValue;
import com.cubrid.jsp.value.LongValue;
import com.cubrid.jsp.value.OidValue;
import com.cubrid.jsp.value.ResultSetValue;
import com.cubrid.jsp.value.SetValue;
import com.cubrid.jsp.value.ShortValue;
import com.cubrid.jsp.value.StringValue;
import com.cubrid.jsp.value.TimeValue;
import com.cubrid.jsp.value.TimestampValue;
import com.cubrid.jsp.value.Value;
import cubrid.sql.CUBRIDOID;
import java.lang.reflect.Method;
import java.math.BigDecimal;
import java.sql.Date;
import java.sql.ResultSet;
import java.sql.Time;
import java.sql.Timestamp;

public class StoredProcedure {
    private String signature;
    private Value[] args;
    private int returnType;
    private TargetMethod target;

    private Object[] cachedResolved;

    public StoredProcedure(String signature, Value[] args, int returnType) throws Exception {
        this.signature = signature;
        this.args = args;
        this.returnType = returnType;
        this.target = TargetMethodCache.getInstance().get(signature);
        this.cachedResolved = null;

        checkArgs();
    }

    public Object[] getResolved() {
        Object[] resolved = new Object[args.length];
        for (int i = 0; i < args.length; i++) {
            resolved[i] = args[i].getResolved();
        }
        return resolved;
    }

    public void setArgs() {}

    public void checkArgs() throws TypeMismatchException {
        Class<?>[] argsTypes = target.getArgsTypes();
        if (argsTypes.length != args.length) {
            throw new TypeMismatchException(
                    "Argument count mismatch: expected "
                            + argsTypes.length
                            + ", but "
                            + args.length);
        }

        for (int i = 0; i < argsTypes.length; i++) {
            Object resolved;
            if (args[i] == null) {
                resolved = null;
            } else if (argsTypes[i] == byte.class || argsTypes[i] == Byte.class) {
                resolved = args[i].toByteObject();
            } else if (argsTypes[i] == short.class || argsTypes[i] == Short.class) {
                resolved = args[i].toShortObject();

            } else if (argsTypes[i] == int.class || argsTypes[i] == Integer.class) {
                resolved = args[i].toIntegerObject();

            } else if (argsTypes[i] == long.class || argsTypes[i] == Long.class) {
                resolved = args[i].toLongObject();

            } else if (argsTypes[i] == float.class || argsTypes[i] == Float.class) {
                resolved = args[i].toFloatObject();

            } else if (argsTypes[i] == double.class || argsTypes[i] == Double.class) {
                resolved = args[i].toDoubleObject();

            } else if (argsTypes[i] == String.class) {
                resolved = args[i].toString();

            } else if (argsTypes[i] == Date.class) {
                resolved = args[i].toDate();

            } else if (argsTypes[i] == Time.class) {
                resolved = args[i].toTime();

            } else if (argsTypes[i] == Timestamp.class) {
                resolved = args[i].toTimestamp();

            } else if (argsTypes[i] == BigDecimal.class) {
                resolved = args[i].toBigDecimal();

            } else if (argsTypes[i] == CUBRIDOID.class) {
                resolved = args[i].toOid();

            } else if (argsTypes[i] == Object.class) {
                resolved = args[i].toObject();

            } else if (argsTypes[i] == byte[].class) {
                resolved = args[i].toByteArray();

            } else if (argsTypes[i] == short[].class) {
                resolved = args[i].toShortArray();

            } else if (argsTypes[i] == int[].class) {
                resolved = args[i].toIntegerArray();

            } else if (argsTypes[i] == long[].class) {
                resolved = args[i].toLongArray();

            } else if (argsTypes[i] == float[].class) {
                resolved = args[i].toFloatArray();

            } else if (argsTypes[i] == double[].class) {
                resolved = args[i].toDoubleArray();

            } else if (argsTypes[i] == String[].class) {
                resolved = args[i].toStringArray();

            } else if (argsTypes[i] == Byte[].class) {
                resolved = args[i].toByteObjArray();

            } else if (argsTypes[i] == Short[].class) {
                resolved = args[i].toShortObjArray();

            } else if (argsTypes[i] == Integer[].class) {
                resolved = args[i].toIntegerObjArray();

            } else if (argsTypes[i] == Long[].class) {
                resolved = args[i].toLongObjArray();

            } else if (argsTypes[i] == Float[].class) {
                resolved = args[i].toFloatObjArray();

            } else if (argsTypes[i] == Double[].class) {
                resolved = args[i].toDoubleObjArray();

            } else if (argsTypes[i] == Date[].class) {
                resolved = args[i].toDateArray();

            } else if (argsTypes[i] == Time[].class) {
                resolved = args[i].toTimeArray();

            } else if (argsTypes[i] == Timestamp[].class) {
                resolved = args[i].toTimestampArray();

            } else if (argsTypes[i] == BigDecimal[].class) {
                resolved = args[i].toBigDecimalArray();

            } else if (argsTypes[i] == CUBRIDOID[].class) {
                resolved = args[i].toOidArray();

            } else if (argsTypes[i] == ResultSet[].class) {
                resolved = args[i].toResultSetArray(null);

            } else if (argsTypes[i] == Object[].class) {
                resolved = args[i].toObjectArray();

            } else if (argsTypes[i] == byte[][].class) {
                resolved = args[i].toByteArrayArray();

            } else if (argsTypes[i] == short[][].class) {
                resolved = args[i].toShortArrayArray();

            } else if (argsTypes[i] == int[][].class) {
                resolved = args[i].toIntegerArrayArray();

            } else if (argsTypes[i] == long[][].class) {
                resolved = args[i].toLongArrayArray();

            } else if (argsTypes[i] == float[][].class) {
                resolved = args[i].toFloatArrayArray();

            } else if (argsTypes[i] == double[].class) {
                resolved = args[i].toDoubleArrayArray();

            } else if (argsTypes[i] == String[][].class) {
                resolved = args[i].toStringArrayArray();

            } else if (argsTypes[i] == Byte[][].class) {
                resolved = args[i].toByteObjArrayArray();

            } else if (argsTypes[i] == Short[][].class) {
                resolved = args[i].toShortObjArrayArray();

            } else if (argsTypes[i] == Integer[][].class) {
                resolved = args[i].toIntegerObjArrayArray();

            } else if (argsTypes[i] == Long[][].class) {
                resolved = args[i].toLongObjArrayArray();

            } else if (argsTypes[i] == Float[][].class) {
                resolved = args[i].toFloatObjArrayArray();

            } else if (argsTypes[i] == Double[][].class) {
                resolved = args[i].toDoubleObjArrayArray();

            } else if (argsTypes[i] == Date[][].class) {
                resolved = args[i].toDateArrayArray();

            } else if (argsTypes[i] == Time[][].class) {
                resolved = args[i].toTimeArrayArray();

            } else if (argsTypes[i] == Timestamp[][].class) {
                resolved = args[i].toTimestampArrayArray();

            } else if (argsTypes[i] == BigDecimal[][].class) {
                resolved = args[i].toBigDecimalArrayArray();

            } else if (argsTypes[i] == CUBRIDOID[][].class) {
                resolved = args[i].toOidArrayArray();

            } else if (argsTypes[i] == ResultSet[][].class) {
                resolved = args[i].toResultSetArrayArray(null);

            } else if (argsTypes[i] == Object[][].class) {
                resolved = args[i].toObjectArrayArray();

            } else {
                throw new TypeMismatchException(
                        "Not supported data type: '" + argsTypes[i].getName() + "'");
            }

            args[i].setResolved(resolved);
        }
    }

    public Value invoke() throws Exception {
        Method m = target.getMethod();
        if (cachedResolved == null) {
            cachedResolved = getResolved();
        }
        Object result = m.invoke(null, cachedResolved);
        return makeReturnValue(result);
    }

    public Value makeOutValue(Object object) throws ExecuteException {
        Object obj = null;
        if (object instanceof byte[]) {
            obj = new Byte(((byte[]) object)[0]);
        } else if (object instanceof short[]) {
            obj = new Short(((short[]) object)[0]);
        } else if (object instanceof int[]) {
            obj = new Integer(((int[]) object)[0]);
        } else if (object instanceof long[]) {
            obj = new Long(((long[]) object)[0]);
        } else if (object instanceof float[]) {
            obj = new Float(((float[]) object)[0]);
        } else if (object instanceof double[]) {
            obj = new Double(((double[]) object)[0]);
        } else if (object instanceof byte[][]) {
            obj = ((byte[][]) object)[0];
        } else if (object instanceof short[][]) {
            obj = ((short[][]) object)[0];
        } else if (object instanceof int[][]) {
            obj = ((int[][]) object)[0];
        } else if (object instanceof long[][]) {
            obj = ((long[][]) object)[0];
        } else if (object instanceof float[][]) {
            obj = ((float[][]) object)[0];
        } else if (object instanceof double[][]) {
            obj = ((double[][]) object)[0];
        } else if (object instanceof Object[]) {
            obj = ((Object[]) object)[0];
        }

        return makeReturnValue(obj);
    }

    public Value makeReturnValue(Object o) throws ExecuteException {
        Value val = null;

        if (o == null) {
            return null;
        } else if (o instanceof Boolean) {
            val = new BooleanValue(((Boolean) o).booleanValue());
        } else if (o instanceof Byte) {
            val = new ByteValue(((Byte) o).byteValue());
        } else if (o instanceof Character) {
            val = new StringValue(((Character) o).toString());
        } else if (o instanceof Short) {
            val = new ShortValue(((Short) o).shortValue());
        } else if (o instanceof Integer) {
            val = new IntValue(((Integer) o).intValue());
        } else if (o instanceof Long) {
            val = new LongValue(((Long) o).longValue());
        } else if (o instanceof Float) {
            val = new FloatValue(((Float) o).floatValue());
        } else if (o instanceof Double) {
            val = new DoubleValue(((Double) o).doubleValue());
        } else if (o instanceof BigDecimal) {
            val = new DoubleValue(((BigDecimal) o).doubleValue());
        } else if (o instanceof String) {
            val = new StringValue((String) o);
        } else if (o instanceof java.sql.Date) {
            val = new DateValue((java.sql.Date) o);
        } else if (o instanceof java.sql.Time) {
            val = new TimeValue((java.sql.Time) o);
        } else if (o instanceof java.sql.Timestamp) {
            val = new TimestampValue((java.sql.Timestamp) o);
        } else if (o instanceof CUBRIDOID) {
            val = new OidValue((CUBRIDOID) o);
        } else if (o instanceof ResultSet) {
            val = new ResultSetValue((ResultSet) o);
        } else if (o instanceof byte[]) {
            val = new SetValue((byte[]) o);
        } else if (o instanceof short[]) {
            val = new SetValue((short[]) o);
        } else if (o instanceof int[]) {
            val = new SetValue((int[]) o);
        } else if (o instanceof long[]) {
            val = new SetValue((long[]) o);
        } else if (o instanceof float[]) {
            val = new SetValue((float[]) o);
        } else if (o instanceof double[]) {
            val = new SetValue((double[]) o);
        } else if (o instanceof Object[]) {
            val = new SetValue((Object[]) o);
        } else {
            throw new ExecuteException("Not supported data type: '" + o.getClass().getName() + "'");
        }

        return val;
    }

    public int getReturnType() {
        return returnType;
    }

    public Value[] getArgs() {
        return args;
    }

    public void setArgs(Value[] args) {
        this.args = args;
    }

    public String getSignature() {
        return signature;
    }

    public TargetMethod getTarget() {
        return target;
    }
}
