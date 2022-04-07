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

import com.cubrid.jsp.data.DBType;
import com.cubrid.jsp.data.SOID;
import com.cubrid.jsp.jdbc.CUBRIDServerSideResultSet;
import cubrid.sql.CUBRIDOID;
import java.io.UnsupportedEncodingException;
import java.math.BigDecimal;
import java.nio.ByteBuffer;
import java.sql.ResultSet;
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
        align(DataUtilities.INT_ALIGNMENT);
        ensureSpace(DataUtilities.INT_BYTES);
        buffer.putInt(value);
    }

    public void packBool(boolean value) {
        buffer.putInt(value ? 1 : 0);
    }

    public void packShort(short value) {
        align(DataUtilities.SHORT_ALIGNMENT);
        buffer.putShort(value);
    }

    public void packBigInt(long value) {
        align(DataUtilities.MAX_ALIGNMENT);
        buffer.putLong(value);
    }

    public void packFloat(float value) {
        // TODO: alignment is not considered yet in cubpacking::packer
        // align(DataUtilities.FLOAT_ALIGNMENT);
        buffer.putFloat(value);
    }

    public void packDouble(double value) {
        // TODO: alignment is not considered yet in cubpacking::packer
        // align(DataUtilities.DOUBLE_ALIGNMENT);
        buffer.putDouble(value);
    }

    public void packString(String value) {
        packCString(value.getBytes());
    }

    public void packString(String value, String charset) throws UnsupportedEncodingException {
        packCString(value.getBytes(charset));
    }

    public void packOID(SOID oid) {
        align(DataUtilities.INT_ALIGNMENT);
        ensureSpace(DataUtilities.INT_BYTES + DataUtilities.SHORT_BYTES + DataUtilities.SHORT_BYTES);
        packInt(oid.pageId);
        packShort(oid.slotId);
        packShort(oid.volId);
    }

    public void packCString(byte[] value) {
        int len = value.length;
        if (len < DataUtilities.MAX_SMALL_STRING_SIZE) {
            ensureSpace(value.length + 1); // str + len
            buffer.put((byte) len);
            buffer.put(value);
            align(DataUtilities.INT_ALIGNMENT);
        } else {
            ensureSpace(value.length + 1 + DataUtilities.INT_BYTES); // str + LARGE_STRING_CODE + len
            buffer.put((byte) DataUtilities.LARGE_STRING_CODE);

            align(DataUtilities.INT_ALIGNMENT);
            buffer.putInt(len);
            buffer.put(value);
            align(DataUtilities.INT_ALIGNMENT);
        }
    }

    // TODO: legacy implementation, this function will be modified
    public void packValue(Object result, int ret_type, String charset)
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
            packInt(ret_type);
            packDouble(((Double) result).doubleValue());
        } else if (result instanceof BigDecimal) {
            packInt(DBType.DB_NUMERIC);
            packString(((BigDecimal) result).toString(), charset);
        } else if (result instanceof String) {
            packInt(DBType.DB_STRING);
            packString((String) result, charset);
        } else if (result instanceof java.sql.Date) {
            packInt(DBType.DB_DATE);
            packString(result.toString(), charset);
        } else if (result instanceof java.sql.Time) {
            packInt(DBType.DB_TIME);
            packString(result.toString(), charset);
        } else if (result instanceof java.sql.Timestamp) {
            packInt(ret_type);
            if (ret_type == DBType.DB_DATETIME) {
                packString(result.toString());
            } else {
                SimpleDateFormat formatter = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
                packString(formatter.format(result));
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
                packValue(array[i], ret_type, charset);
            }
            packValue(array, ret_type, charset);
        } else if (result instanceof short[]) {
            int length = ((short[]) result).length;
            Short[] array = new Short[length];
            packInt(array.length);
            for (int i = 0; i < array.length; i++) {
                array[i] = new Short(((short[]) result)[i]);
                packValue(array, ret_type, charset);
            }
        } else if (result instanceof float[]) {
            int length = ((float[]) result).length;
            Float[] array = new Float[length];
            packInt(array.length);
            for (int i = 0; i < array.length; i++) {
                array[i] = new Float(((float[]) result)[i]);
                packValue(array[i], ret_type, charset);
            }
        } else if (result instanceof double[]) {
            int length = ((double[]) result).length;
            Double[] array = new Double[length];
            packInt(array.length);
            for (int i = 0; i < array.length; i++) {
                array[i] = new Double(((double[]) result)[i]);
                packValue(array[i], ret_type, charset);
            }
        } else if (result instanceof Object[]) {
            packInt(ret_type);
            Object[] arr = (Object[]) result;

            packInt(arr.length);
            for (int i = 0; i < arr.length; i++) {
                packValue(arr[i], ret_type, charset);
            }
        } else {
            // FIXME: treat as NULL
            packInt(DBType.DB_NULL);
        }
    }

    private void align(int size) {
        int currentPosition = buffer.position();
        int newPosition = DataUtilities.alignedPosition(buffer, size);

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
