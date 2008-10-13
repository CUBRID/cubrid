package com.cubrid.jsp;

import java.lang.reflect.Method;
import java.math.BigDecimal;
import java.sql.Date;
import java.sql.ResultSet;
import java.sql.Time;
import java.sql.Timestamp;

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

public class StoredProcedure {
    private Value[] args;

    private int returnType;

    private TargetMethod target;

    public StoredProcedure(String signature, Value[] args, int returnType)
            throws Exception {
        this.args = args;
        this.returnType = returnType;
        this.target = TargetMethodCache.getInstance().get(signature);
    }

    public Object[] checkArgs(Value[] args) throws TypeMismatchException {
        Object[] resolved = new Object[args.length];
        Class[] argsTypes = target.getArgsTypes();

        if (argsTypes.length != args.length)
            throw new TypeMismatchException(
                    "Argument count mismatch: expected " + argsTypes.length
                            + ", but " + args.length);

        for (int i = 0; i < argsTypes.length; i++) {
            if (args[i] == null) {
                resolved[i] = null;
            } else if (argsTypes[i] == byte.class || argsTypes[i] == Byte.class) {
                resolved[i] = args[i].toByteObject();
            } else if (argsTypes[i] == short.class
                    || argsTypes[i] == Short.class) {
                resolved[i] = args[i].toShortObject();

            } else if (argsTypes[i] == int.class
                    || argsTypes[i] == Integer.class) {
                resolved[i] = args[i].toIntegerObject();

            } else if (argsTypes[i] == long.class || argsTypes[i] == Long.class) {
                resolved[i] = args[i].toLongObject();

            } else if (argsTypes[i] == float.class
                    || argsTypes[i] == Float.class) {
                resolved[i] = args[i].toFloatObject();

            } else if (argsTypes[i] == double.class
                    || argsTypes[i] == Double.class) {
                resolved[i] = args[i].toDoubleObject();

            } else if (argsTypes[i] == String.class) {
                resolved[i] = args[i].toString();

            } else if (argsTypes[i] == Date.class) {
                resolved[i] = args[i].toDate();

            } else if (argsTypes[i] == Time.class) {
                resolved[i] = args[i].toTime();

            } else if (argsTypes[i] == Timestamp.class) {
                resolved[i] = args[i].toTimestamp();

            } else if (argsTypes[i] == BigDecimal.class) {
                resolved[i] = args[i].toBigDecimal();

            } else if (argsTypes[i] == CUBRIDOID.class) {
                resolved[i] = args[i].toOid();

            } else if (argsTypes[i] == Object.class) {
                resolved[i] = args[i].toObject();

            } else if (argsTypes[i] == byte[].class) {
                resolved[i] = args[i].toByteArray();

            } else if (argsTypes[i] == short[].class) {
                resolved[i] = args[i].toShortArray();

            } else if (argsTypes[i] == int[].class) {
                resolved[i] = args[i].toIntegerArray();

            } else if (argsTypes[i] == long[].class) {
                resolved[i] = args[i].toLongArray();

            } else if (argsTypes[i] == float[].class) {
                resolved[i] = args[i].toFloatArray();

            } else if (argsTypes[i] == double[].class) {
                resolved[i] = args[i].toDoubleArray();

            } else if (argsTypes[i] == String[].class) {
                resolved[i] = args[i].toStringArray();

            } else if (argsTypes[i] == Byte[].class) {
                resolved[i] = args[i].toByteObjArray();

            } else if (argsTypes[i] == Short[].class) {
                resolved[i] = args[i].toShortObjArray();

            } else if (argsTypes[i] == Integer[].class) {
                resolved[i] = args[i].toIntegerObjArray();

            } else if (argsTypes[i] == Long[].class) {
                resolved[i] = args[i].toLongObjArray();

            } else if (argsTypes[i] == Float[].class) {
                resolved[i] = args[i].toFloatObjArray();

            } else if (argsTypes[i] == Double[].class) {
                resolved[i] = args[i].toDoubleObjArray();

            } else if (argsTypes[i] == Date[].class) {
                resolved[i] = args[i].toDateArray();

            } else if (argsTypes[i] == Time[].class) {
                resolved[i] = args[i].toTimeArray();

            } else if (argsTypes[i] == Timestamp[].class) {
                resolved[i] = args[i].toTimestampArray();

            } else if (argsTypes[i] == BigDecimal[].class) {
                resolved[i] = args[i].toBigDecimalArray();

            } else if (argsTypes[i] == CUBRIDOID[].class) {
                resolved[i] = args[i].toOidArray();

            } else if (argsTypes[i] == ResultSet[].class) {
                resolved[i] = args[i].toResultSetArray();

            } else if (argsTypes[i] == Object[].class) {
                resolved[i] = args[i].toObjectArray();

            } else if (argsTypes[i] == byte[][].class) {
                resolved[i] = args[i].toByteArrayArray();

            } else if (argsTypes[i] == short[][].class) {
                resolved[i] = args[i].toShortArrayArray();

            } else if (argsTypes[i] == int[][].class) {
                resolved[i] = args[i].toIntegerArrayArray();

            } else if (argsTypes[i] == long[][].class) {
                resolved[i] = args[i].toLongArrayArray();

            } else if (argsTypes[i] == float[][].class) {
                resolved[i] = args[i].toFloatArrayArray();

            } else if (argsTypes[i] == double[].class) {
                resolved[i] = args[i].toDoubleArrayArray();

            } else if (argsTypes[i] == String[][].class) {
                resolved[i] = args[i].toStringArrayArray();

            } else if (argsTypes[i] == Byte[][].class) {
                resolved[i] = args[i].toByteObjArrayArray();

            } else if (argsTypes[i] == Short[][].class) {
                resolved[i] = args[i].toShortObjArrayArray();

            } else if (argsTypes[i] == Integer[][].class) {
                resolved[i] = args[i].toIntegerObjArrayArray();

            } else if (argsTypes[i] == Long[][].class) {
                resolved[i] = args[i].toLongObjArrayArray();

            } else if (argsTypes[i] == Float[][].class) {
                resolved[i] = args[i].toFloatObjArrayArray();

            } else if (argsTypes[i] == Double[][].class) {
                resolved[i] = args[i].toDoubleObjArrayArray();

            } else if (argsTypes[i] == Date[][].class) {
                resolved[i] = args[i].toDateArrayArray();

            } else if (argsTypes[i] == Time[][].class) {
                resolved[i] = args[i].toTimeArrayArray();

            } else if (argsTypes[i] == Timestamp[][].class) {
                resolved[i] = args[i].toTimestampArrayArray();

            } else if (argsTypes[i] == BigDecimal[][].class) {
                resolved[i] = args[i].toBigDecimalArrayArray();

            } else if (argsTypes[i] == CUBRIDOID[][].class) {
                resolved[i] = args[i].toOidArrayArray();

            } else if (argsTypes[i] == ResultSet[][].class) {
                resolved[i] = args[i].toResultSetArrayArray();

            } else if (argsTypes[i] == Object[][].class) {
                resolved[i] = args[i].toObjectArrayArray();
                
            } else {
                throw new TypeMismatchException("Not supported data type: '"
                        + argsTypes[i].getName() + "'");
            }

            args[i].setResolved(resolved[i]);
        }

        return resolved;
    }

    public Value invoke() throws Exception {
        Method m = target.getMethod();
        Object[] resolved = checkArgs(args);
        return makeReturnValue(m.invoke(null, resolved));
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
        } else
            throw new ExecuteException("Not supported data type: '"
                    + o.getClass().getName() + "'");

        return val;
    }

    public int getReturnType() {
        return returnType;
    }

    public Value[] getArgs() {
        return args;
    }
}
