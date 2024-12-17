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

package com.cubrid.jsp.data;

import com.cubrid.jsp.Server;
import com.cubrid.jsp.SysParam;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.jdbc.CUBRIDServerSideResultSet;
import com.cubrid.jsp.protocol.PackableObject;
import com.cubrid.jsp.value.NullValue;
import com.cubrid.jsp.value.SetValue;
import com.cubrid.jsp.value.StringValue;
import com.cubrid.jsp.value.Value;
import com.cubrid.plcsql.predefined.sp.SpLib;
import cubrid.sql.CUBRIDOID;
import java.io.UnsupportedEncodingException;
import java.math.BigDecimal;
import java.nio.ByteBuffer;
import java.nio.charset.Charset;
import java.sql.Date;
import java.sql.ResultSet;
import java.sql.Timestamp;
import java.text.SimpleDateFormat;

public class CUBRIDPacker {
    private ByteBuffer buffer;

    public CUBRIDPacker(ByteBuffer buffer) {
        buffer.clear();
        this.buffer = buffer;
    }

    public CUBRIDPacker(byte[] byteArray) {
        this(ByteBuffer.wrap(byteArray));
    }

    public void setBuffer(ByteBuffer buffer) {
        this.buffer = buffer;
    }

    public ByteBuffer getBuffer() {
        return this.buffer;
    }

    public void packInt(int value) {
        ensureSpace(DataUtilities.INT_BYTES * 2);
        align(DataUtilities.INT_ALIGNMENT);
        buffer.putInt(value);
    }

    public void packBool(boolean value) {
        packInt(value ? 1 : 0);
    }

    public void packShort(short value) {
        ensureSpace(DataUtilities.SHORT_BYTES * 2);
        align(DataUtilities.SHORT_ALIGNMENT);
        buffer.putShort(value);
    }

    public void packBigInt(long value) {
        ensureSpace(DataUtilities.MAX_ALIGNMENT + DataUtilities.LONG_BYTES);
        align(DataUtilities.MAX_ALIGNMENT);
        buffer.putLong(value);
    }

    public void packFloat(float value) {
        // TODO: alignment is not considered yet in cubpacking::packer
        // align(DataUtilities.FLOAT_ALIGNMENT);
        ensureSpace(DataUtilities.FLOAT_BYTES);
        buffer.putFloat(value);
    }

    public void packDouble(double value) {
        // TODO: alignment is not considered yet in cubpacking::packer
        // align(DataUtilities.DOUBLE_ALIGNMENT);
        ensureSpace(DataUtilities.DOUBLE_BYTES);
        buffer.putDouble(value);
    }

    public void packString(String value) {
        Charset charset = Server.getConfig().getServerCharset();
        packCString(value.getBytes(charset));
    }

    public void packString(String value, int codeset) throws UnsupportedEncodingException {
        packCString(value.getBytes(SysParam.getCodesetString(codeset)));
    }

    public void packOID(SOID oid) {
        ensureSpace(
                DataUtilities.INT_BYTES
                        + DataUtilities.SHORT_BYTES
                        + DataUtilities.SHORT_BYTES
                        + DataUtilities.INT_ALIGNMENT);
        align(DataUtilities.INT_ALIGNMENT);
        packInt(oid.pageId);
        packShort(oid.slotId);
        packShort(oid.volId);
    }

    public void packCString(byte[] value) {
        int len = value.length;
        if (len < DataUtilities.MAX_SMALL_STRING_SIZE) {
            ensureSpace(value.length + 1 + DataUtilities.INT_ALIGNMENT); // str + len + align
            buffer.put((byte) len);
            buffer.put(value);
            align(DataUtilities.INT_ALIGNMENT);
        } else {
            ensureSpace(
                    value.length
                            + 1
                            + DataUtilities.INT_BYTES
                            + DataUtilities.INT_ALIGNMENT
                                    * 2); // str + LARGE_STRING_CODE + len + align
            buffer.put((byte) DataUtilities.LARGE_STRING_CODE);

            align(DataUtilities.INT_ALIGNMENT);
            buffer.putInt(len);
            buffer.put(value);
            align(DataUtilities.INT_ALIGNMENT);
        }
    }

    public void packPackableObject(PackableObject o) {
        o.pack(this);
    }

    public void packPrimitiveBytes(ByteBuffer b) {
        ensureSpace(b.position());
        buffer.put(b.array(), 0, b.position());
    }

    public void packValue(Value value, int dbType)
            throws UnsupportedEncodingException, TypeMismatchException {
        if (value == null || value instanceof NullValue) {
            dbType = DBType.DB_NULL;
        }

        switch (dbType) {
            case DBType.DB_INT:
                packInt(dbType);
                packInt(value.toInt());
                break;
            case DBType.DB_SHORT:
                packInt(dbType);
                packShort(value.toShort());
                break;
            case DBType.DB_BIGINT:
                packInt(dbType);
                packBigInt(value.toLong());
                break;
            case DBType.DB_FLOAT:
                packInt(dbType);
                packFloat(value.toFloat());
                break;
            case DBType.DB_DOUBLE:
            case DBType.DB_MONETARY:
                packInt(dbType);
                packDouble(value.toDouble());
                break;

            case DBType.DB_CHAR:
            case DBType.DB_STRING:
                packInt(dbType);
                if (value instanceof StringValue) {
                    packInt(value.getCodeSet());
                    packCString(value.toByteArray());
                } else {
                    packInt(value.getCodeSet());
                    packString(value.toString(), value.getCodeSet());
                }
                break;

            case DBType.DB_NUMERIC:
                packInt(dbType);
                packString(value.toBigDecimal().toPlainString());
                break;

            case DBType.DB_DATE:
                packInt(dbType);
                Date d = value.toDate();
                if (d.equals(SpLib.ZERO_DATE)) {
                    packString("0000-00-00");
                } else {
                    packString(d.toString());
                }
                break;
            case DBType.DB_TIME:
                packInt(dbType);
                packString(value.toTime().toString());
                break;

            case DBType.DB_TIMESTAMP:
            case DBType.DB_DATETIME:
                packInt(dbType);

                Timestamp ts = null;
                if (dbType == DBType.DB_DATETIME) {
                    ts = value.toDatetime();
                    if (ts.equals(SpLib.ZERO_DATETIME)) {
                        packString("0000-00-00 00:00:00.000");
                    } else {
                        packString(ts.toString());
                    }
                } else {
                    ts = value.toTimestamp();
                    if (SpLib.isZeroTimestamp((java.sql.Timestamp) ts)) {
                        packString("0000-00-00 00:00:00");
                    } else {
                        SimpleDateFormat formatter = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
                        packString(formatter.format(ts));
                    }
                }
                break;

            case DBType.DB_OID:
            case DBType.DB_OBJECT:
                packInt(dbType);
                byte[] oid = value.toOid().getOID();
                packOID(new SOID(oid));
                break;

            case DBType.DB_SET:
            case DBType.DB_MULTISET:
            case DBType.DB_SEQUENCE:
                packInt(dbType);

                Object[] values = null;
                if (value instanceof SetValue) {
                    values = (Value[]) ((SetValue) value).toValueArray();
                    if (values != null) {
                        packInt(values.length);
                        for (int i = 0; i < values.length; i++) {
                            int dt = DBType.DB_NULL;
                            if (values[i] != null) {
                                dt = ((Value) values[i]).getDbType();
                            }
                            packValue((Value) values[i], dt);
                        }
                    }
                }

                if (values == null) {
                    values = value.toObjectArray();
                    packObject(values, dbType, value.getCodeSet());
                }
                break;

            case DBType.DB_RESULTSET:
                packInt(dbType);
                packBigInt(value.toLong());
                break;

            default:
                packInt(DBType.DB_NULL);
                break;
        }
    }

    public void packObject(Object result, int ret_type, int codeset)
            throws UnsupportedEncodingException {
        if (result == null) {
            packInt(DBType.DB_NULL);
        } else if (result instanceof Short) {
            packInt(DBType.DB_SHORT);
            packShort((Short) result);
        } else if (result instanceof Integer) {
            packInt(DBType.DB_INT);
            packInt(((Integer) result).intValue());
        } else if (result instanceof Long) {
            packInt(DBType.DB_BIGINT);
            packBigInt(((Long) result).longValue());
        } else if (result instanceof Float) {
            packInt(DBType.DB_FLOAT);
            packFloat(((Float) result).floatValue());
        } else if (result instanceof Double) {
            packInt(DBType.DB_DOUBLE);
            packDouble(((Double) result).doubleValue());
        } else if (result instanceof BigDecimal) {
            packInt(DBType.DB_NUMERIC);
            packString(((BigDecimal) result).toPlainString(), codeset);
        } else if (result instanceof String) {
            packInt(DBType.DB_STRING);
            packInt(codeset);
            packString((String) result, codeset);
        } else if (result instanceof java.sql.Date) {
            packInt(DBType.DB_DATE);
            if (result.equals(SpLib.ZERO_DATE)) {
                packString("0000-00-00", codeset);
            } else {
                packString(result.toString(), codeset);
            }
        } else if (result instanceof java.sql.Time) {
            packInt(DBType.DB_TIME);
            packString(result.toString(), codeset);
        } else if (result instanceof java.sql.Timestamp) {
            packInt(ret_type);
            if (ret_type == DBType.DB_DATETIME) {
                if (result.equals(SpLib.ZERO_DATETIME)) {
                    packString("0000-00-00 00:00:00.000");
                } else {
                    packString(result.toString());
                }
            } else {
                if (SpLib.isZeroTimestamp((java.sql.Timestamp) result)) {
                    packString("0000-00-00 00:00:00");
                } else {
                    SimpleDateFormat formatter = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
                    packString(formatter.format(result));
                }
            }
        } else if (result instanceof CUBRIDOID) {
            packInt(DBType.DB_OBJECT);
            byte[] oid = ((CUBRIDOID) result).getOID();
            packInt(DataUtilities.bytes2int(oid, 0));
            packShort(DataUtilities.bytes2short(oid, 4));
            packShort(DataUtilities.bytes2short(oid, 6));
        } else if (result instanceof ResultSet) {
            packInt(DBType.DB_RESULTSET);
            packBigInt(((CUBRIDServerSideResultSet) result).getQueryId());
        } else if (result instanceof int[]) {
            int length = ((int[]) result).length;
            Integer[] array = new Integer[length];
            packInt(array.length);
            for (int i = 0; i < array.length; i++) {
                array[i] = new Integer(((int[]) result)[i]);
                packObject(array[i], DBType.DB_INT, codeset);
            }
        } else if (result instanceof short[]) {
            int length = ((short[]) result).length;
            Short[] array = new Short[length];
            packInt(array.length);
            for (int i = 0; i < array.length; i++) {
                array[i] = new Short(((short[]) result)[i]);
                packObject(array, DBType.DB_SHORT, codeset);
            }
        } else if (result instanceof float[]) {
            int length = ((float[]) result).length;
            Float[] array = new Float[length];
            packInt(array.length);
            for (int i = 0; i < array.length; i++) {
                array[i] = new Float(((float[]) result)[i]);
                packObject(array[i], DBType.DB_FLOAT, codeset);
            }
        } else if (result instanceof double[]) {
            int length = ((double[]) result).length;
            Double[] array = new Double[length];
            packInt(array.length);
            for (int i = 0; i < array.length; i++) {
                array[i] = new Double(((double[]) result)[i]);
                packObject(array[i], DBType.DB_DOUBLE, codeset);
            }
        } else if (result instanceof Object[]) {
            packInt(ret_type);
            Object[] arr = (Object[]) result;

            packInt(arr.length);
            for (int i = 0; i < arr.length; i++) {
                packObject(arr[i], ret_type, codeset);
            }
        } else {
            // FIXME: treat as NULL
            packInt(DBType.DB_NULL);
        }
    }

    public void align(int size) {
        int currentPosition = buffer.position();
        int newPosition = DataUtilities.alignedPosition(currentPosition, size);

        ensureSpace(newPosition - currentPosition);
        if (newPosition - currentPosition > 0) {
            buffer.position(newPosition);
        }
    }

    private static final int EXPAND_FACTOR = 2;

    private void ensureSpace(int size) {
        if (buffer.remaining() > size) {
            return;
        }
        int newCapacity = (buffer.capacity() * EXPAND_FACTOR);
        while (newCapacity < (buffer.capacity() + size)) {
            newCapacity *= EXPAND_FACTOR;
        }
        ByteBuffer expanded = ByteBuffer.allocate(newCapacity);

        expanded.clear();
        expanded.order(buffer.order());
        expanded.put(buffer.array(), 0, buffer.position());
        buffer = expanded;
    }
}
