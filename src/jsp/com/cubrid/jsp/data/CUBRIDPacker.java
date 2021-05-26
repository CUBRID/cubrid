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

import cubrid.jdbc.driver.CUBRIDResultSet;
import cubrid.sql.CUBRIDOID;
import java.io.UnsupportedEncodingException;
import java.math.BigDecimal;
import java.nio.ByteBuffer;
import java.sql.ResultSet;
import java.text.SimpleDateFormat;

public class CUBRIDPacker {
    private ByteBuffer buffer;

    public CUBRIDPacker(ByteBuffer buffer) {
        this.buffer = buffer;
    }

    public CUBRIDPacker(byte[] byteArray) {
        this(ByteBuffer.wrap(byteArray));
    }

    public void packInt(int value) {
        align(DataUtilities.INT_ALIGNMENT);
        buffer.putInt(value);
    }

    public void packBool(boolean value) {
        packInt(value ? 1 : 0);
    }

    public void packShort(short value) {
        align(DataUtilities.SHORT_ALIGNMENT);
        buffer.putShort(value);
    }

    public void packBigInt(long value) {
        align(DataUtilities.MAX_ALIGNMENT);
        buffer.putLong(value);
    }

    public void packString(String value) {
        packCString(value.getBytes());
    }

    public void packCString(byte[] value) {
        int len = value.length;
        if (len < DataUtilities.MAX_SMALL_STRING_SIZE) {
            buffer.put((byte) len);
            buffer.put(value);
        } else {
            buffer.put((byte) DataUtilities.LARGE_STRING_CODE);

            align(DataUtilities.INT_ALIGNMENT);
            buffer.putInt(len);
            buffer.put(value);
        }

        align(DataUtilities.INT_ALIGNMENT);
    }

    // TODO: legacy implementation, this function will be modified
    public void packValue(Object result, int ret_type, String charset)
            throws UnsupportedEncodingException {
        if (result == null) {
            buffer.putInt(DBType.DB_NULL);
        } else if (result instanceof Short) {
            buffer.putInt(DBType.DB_SHORT);
            buffer.putInt(((Short) result).intValue());
        } else if (result instanceof Integer) {
            buffer.putInt(DBType.DB_INT);
            buffer.putInt(((Integer) result).intValue());
        } else if (result instanceof Long) {
            buffer.putInt(DBType.DB_BIGINT);
            buffer.putLong(((Long) result).longValue());
        } else if (result instanceof Float) {
            buffer.putInt(DBType.DB_FLOAT);
            buffer.putFloat(((Float) result).floatValue());
        } else if (result instanceof Double) {
            buffer.putInt(ret_type);
            buffer.putDouble(((Double) result).doubleValue());
        } else if (result instanceof BigDecimal) {
            buffer.putInt(DBType.DB_NUMERIC);
            DataUtilities.packAndSendString(((BigDecimal) result).toString(), buffer, charset);
        } else if (result instanceof String) {
            buffer.putInt(DBType.DB_STRING);
            DataUtilities.packAndSendString((String) result, buffer, charset);
        } else if (result instanceof java.sql.Date) {
            buffer.putInt(DBType.DB_DATE);
            DataUtilities.packAndSendString(result.toString(), buffer, charset);
        } else if (result instanceof java.sql.Time) {
            buffer.putInt(DBType.DB_TIME);
            DataUtilities.packAndSendString(result.toString(), buffer, charset);
        } else if (result instanceof java.sql.Timestamp) {
            buffer.putInt(ret_type);

            if (ret_type == DBType.DB_DATETIME) {
                DataUtilities.packAndSendString(result.toString(), buffer, charset);
            } else {
                SimpleDateFormat formatter = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
                DataUtilities.packAndSendString(formatter.format(result), buffer, charset);
            }
        } else if (result instanceof CUBRIDOID) {
            // TODO: implement CUBRIDOID type later
            /*
            buffer.putInt(DBType.DB_OBJECT);
            byte[] oid = ((CUBRIDOID) result).getOID();
            buffer.putInt(UJCIUtil.bytes2int(oid, 0));
            buffer.putInt(UJCIUtil.bytes2short(oid, 4));
            buffer.putInt(UJCIUtil.bytes2short(oid, 6));
            */
        } else if (result instanceof ResultSet) {
            buffer.putInt(DBType.DB_RESULTSET);
            buffer.putInt(((CUBRIDResultSet) result).getServerHandle());
        } else if (result instanceof int[]) {
            int length = ((int[]) result).length;
            Integer[] array = new Integer[length];
            for (int i = 0; i < array.length; i++) {
                array[i] = new Integer(((int[]) result)[i]);
            }
            packValue(result, ret_type, charset);
        } else if (result instanceof short[]) {
            int length = ((short[]) result).length;
            Short[] array = new Short[length];
            for (int i = 0; i < array.length; i++) {
                array[i] = new Short(((short[]) result)[i]);
            }
            packValue(result, ret_type, charset);
        } else if (result instanceof float[]) {
            int length = ((float[]) result).length;
            Float[] array = new Float[length];
            for (int i = 0; i < array.length; i++) {
                array[i] = new Float(((float[]) result)[i]);
            }
            packValue(result, ret_type, charset);
        } else if (result instanceof double[]) {
            int length = ((double[]) result).length;
            Double[] array = new Double[length];
            for (int i = 0; i < array.length; i++) {
                array[i] = new Double(((double[]) result)[i]);
            }
            packValue(result, ret_type, charset);
        } else if (result instanceof Object[]) {
            buffer.putInt(ret_type);
            Object[] arr = (Object[]) result;

            buffer.putInt(arr.length);
            for (int i = 0; i < arr.length; i++) {
                packValue(result, ret_type, charset);
            }
        } else ;
    }

    private void align(int size) {
        int currentPosition = buffer.position();
        int newPosition = DataUtilities.alignedPosition(buffer, size);
        if (newPosition - currentPosition > 0) {
            buffer.limit(newPosition);
            buffer.position(newPosition);
        }
    }
}
