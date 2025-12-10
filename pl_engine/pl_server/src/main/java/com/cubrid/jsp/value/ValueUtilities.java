/*
 *
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
import java.sql.ResultSet;

public class ValueUtilities {
    public static Object resolveValue(int dbType, Value value) throws TypeMismatchException {
        Object resolvedResult = null;

        if (value == null) return null;
        switch (dbType) {
            case DBType.DB_INT:
                resolvedResult = value.toIntegerObject();
                break;
            case DBType.DB_BIGINT:
                resolvedResult = value.toLongObject();
                break;
            case DBType.DB_FLOAT:
                resolvedResult = value.toFloatObject();
                break;
            case DBType.DB_DOUBLE:
            case DBType.DB_MONETARY:
                resolvedResult = value.toDoubleObject();
                break;
            case DBType.DB_CHAR:
            case DBType.DB_STRING:
                resolvedResult = value.toString();
                break;
            case DBType.DB_SET:
            case DBType.DB_MULTISET:
            case DBType.DB_SEQUENCE:
                resolvedResult = value.toObjectArray();
                break;
            case DBType.DB_TIME:
                resolvedResult = value.toTime();
                break;
            case DBType.DB_DATE:
                resolvedResult = value.toDate();
                break;
            case DBType.DB_TIMESTAMP:
                resolvedResult = value.toTimestamp();
                break;
            case DBType.DB_DATETIME:
                resolvedResult = value.toDatetime();
                break;
            case DBType.DB_SHORT:
                resolvedResult = value.toShortObject();
                break;
            case DBType.DB_NUMERIC:
                resolvedResult = value.toBigDecimal();
                break;
            case DBType.DB_OID:
            case DBType.DB_OBJECT:
                resolvedResult = value.toOid();
                break;
            case DBType.DB_RESULTSET:
                resolvedResult = value.toResultSet(null);
                break;
            default:
                break;
        }
        return resolvedResult;
    }

    public static Value createValueFrom(Object o) throws TypeMismatchException, ExecuteException {
        Value val = null;

        if (o == null) {
            val = new NullValue();
        } else if (o instanceof Boolean) {
            val = new BooleanValue(((Boolean) o).booleanValue());
        } else if (o instanceof Byte) {
            val = new ByteValue(((Byte) o).byteValue());
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
            val = new NumericValue(((BigDecimal) o));
        } else if (o instanceof String) {
            val = new StringValue((String) o);
        } else if (o instanceof java.sql.Date) {
            val = new DateValue((java.sql.Date) o);
        } else if (o instanceof java.sql.Time) {
            val = new TimeValue((java.sql.Time) o);
        } else if (o instanceof java.sql.Timestamp) {
            val =
                    new DatetimeValue(
                            (java.sql.Timestamp)
                                    o); // DatetimeValue allows more values than TimestampValue
        } else if (o instanceof CUBRIDOID) {
            val = new OidValue((CUBRIDOID) o);
        } else if (o instanceof ResultSet) {
            val = new ResultSetValue((ResultSet) o);
        } else if (o instanceof byte[]) {
            val = new StringValue((byte[]) o);
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
}
