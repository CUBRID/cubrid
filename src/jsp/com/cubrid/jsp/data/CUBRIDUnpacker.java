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

import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.value.*;
import java.nio.ByteBuffer;
import java.util.Calendar;

public class CUBRIDUnpacker {
    private ByteBuffer buffer;

    public CUBRIDUnpacker(ByteBuffer buffer) {
        this.buffer = buffer;
    }

    public CUBRIDUnpacker(byte[] byteArray) {
        this(ByteBuffer.wrap(byteArray));
    }

    public void setBuffer(ByteBuffer buffer) {
        this.buffer = buffer;
    }

    public int unpackInt() {
        align(DataUtilities.INT_ALIGNMENT);
        return buffer.getInt();
    }

    public int[] unpackIntArray() {
        align(DataUtilities.INT_ALIGNMENT);
        int count = buffer.getInt();

        int[] arr = new int[count];
        for (int i = 0; i < arr.length; i++) {
            arr[i] = buffer.getInt();
        }

        return arr;
    }

    public boolean unpackBool() {
        align(DataUtilities.INT_ALIGNMENT);
        return (buffer.getInt() != 0);
    }

    public short unpackShort() {
        align(DataUtilities.SHORT_ALIGNMENT);
        return buffer.getShort();
    }

    public float unpackFloat() {
        // TODO: alignment is not considered yet in cubpacking::packer
        // align(DataUtilities.FLOAT_ALIGNMENT);
        return buffer.getFloat();
    }

    public double unpackDouble() {
        // TODO: alignment is not considered yet in cubpacking::packer
        // align(DataUtilities.DOUBLE_ALIGNMENT);
        return buffer.getDouble();
    }

    public long unpackBigint() {
        align(DataUtilities.MAX_ALIGNMENT);
        return buffer.getLong();
    }

    public String unpackCString() {
        int len = unpackStringSize();

        byte[] str = new byte[len];
        buffer.get(str);
        return new String(str);
    }

    public byte[] unpackCStringByteArray() {
        int len = unpackStringSize();

        byte[] str = new byte[len];
        buffer.get(str);
        return str;
    }

    public int unpackStringSize() {
        int len = (int) buffer.get();
        if (len == 0xff) {
            /* LARGE_STRING_CODE */
            len = buffer.getInt();
        }
        return len;
    }

    public Value unpackValue(int paramSize, int paramType) throws TypeMismatchException {
        Value arg = null;
        switch (paramType) {
            case DBType.DB_SHORT:
                arg = new ShortValue(unpackShort());
                break;
            case DBType.DB_INT:
                arg = new IntValue(unpackInt());
                break;
            case DBType.DB_BIGINT:
                arg = new LongValue(unpackBigint());
                break;
            case DBType.DB_FLOAT:
                arg = new FloatValue(unpackFloat());
                break;
            case DBType.DB_DOUBLE:
            case DBType.DB_MONETARY:
                arg = new DoubleValue(unpackDouble());
                break;
            case DBType.DB_NUMERIC:
                arg = new NumericValue(unpackCString());
                break;
            case DBType.DB_CHAR:
            case DBType.DB_STRING:
                arg = new StringValue(unpackCString());
                break;
            case DBType.DB_DATE:
                {
                    int year = unpackInt();
                    int month = unpackInt();
                    int day = unpackInt();

                    arg = new DateValue(year, month, day);
                }
                break;
            case DBType.DB_TIME:
                {
                    int hour = unpackInt();
                    int min = unpackInt();
                    int sec = unpackInt();

                    Calendar cal = Calendar.getInstance();
                    cal.set(0, 0, 0, hour, min, sec);

                    arg = new TimeValue(hour, min, sec);
                }
                break;
            case DBType.DB_TIMESTAMP:
                {
                    int year = unpackInt();
                    int month = unpackInt();
                    int day = unpackInt();
                    int hour = unpackInt();
                    int min = unpackInt();
                    int sec = unpackInt();
                    arg = new TimestampValue(year, month, day, hour, min, sec);
                }
                break;
            case DBType.DB_DATETIME:
                {
                    int year = unpackInt();
                    int month = unpackInt();
                    int day = unpackInt();
                    int hour = unpackInt();
                    int min = unpackInt();
                    int sec = unpackInt();
                    int msec = unpackInt();
                    arg = new DatetimeValue(year, month, day, hour, min, sec, msec);
                }
                break;
            case DBType.DB_SET:
            case DBType.DB_MULTISET:
            case DBType.DB_SEQUENCE:
                {
                    int nCol = unpackInt();
                    arg = new SetValue(unpackSetValue(nCol));
                }
                break;

            case DBType.DB_OID:
            case DBType.DB_OBJECT:
                {
                    int page = unpackInt();
                    short slot = unpackShort();
                    short vol = unpackShort();

                    byte[] bOID = new byte[DataUtilities.OID_BYTE_SIZE];
                    bOID[0] = ((byte) ((page >>> 24) & 0xFF));
                    bOID[1] = ((byte) ((page >>> 16) & 0xFF));
                    bOID[2] = ((byte) ((page >>> 8) & 0xFF));
                    bOID[3] = ((byte) ((page >>> 0) & 0xFF));
                    bOID[4] = ((byte) ((slot >>> 8) & 0xFF));
                    bOID[5] = ((byte) ((slot >>> 0) & 0xFF));
                    bOID[6] = ((byte) ((vol >>> 8) & 0xFF));
                    bOID[7] = ((byte) ((vol >>> 0) & 0xFF));

                    arg = new OidValue(bOID);
                }
                break;
            case DBType.DB_NULL:
                arg = new NullValue();
                break;
            default:
                // unknown type
                break;
        }
        return arg;
    }

    public Value unpackValue(int paramSize, int paramType, int mode, int dbType)
            throws TypeMismatchException {
        Value arg = null;
        switch (paramType) {
            case DBType.DB_SHORT:
                arg = new ShortValue(unpackShort(), mode, dbType);
                break;
            case DBType.DB_INT:
                arg = new IntValue(unpackInt(), mode, dbType);
                break;
            case DBType.DB_BIGINT:
                arg = new LongValue(unpackBigint(), mode, dbType);
                break;
            case DBType.DB_FLOAT:
                arg = new FloatValue(unpackFloat(), mode, dbType);
                break;
            case DBType.DB_DOUBLE:
            case DBType.DB_MONETARY:
                arg = new DoubleValue(unpackDouble(), mode, dbType);
                break;
            case DBType.DB_NUMERIC:
                arg = new NumericValue(unpackCString(), mode, dbType);
                break;
            case DBType.DB_CHAR:
            case DBType.DB_STRING:
                arg = new StringValue(unpackCString(), mode, dbType);
                break;
            case DBType.DB_DATE:
                {
                    int year = unpackInt();
                    int month = unpackInt();
                    int day = unpackInt();

                    arg = new DateValue(year, month, day, mode, dbType);
                }
                break;
            case DBType.DB_TIME:
                {
                    int hour = unpackInt();
                    int min = unpackInt();
                    int sec = unpackInt();

                    Calendar cal = Calendar.getInstance();
                    cal.set(0, 0, 0, hour, min, sec);

                    arg = new TimeValue(hour, min, sec, mode, dbType);
                }
                break;
            case DBType.DB_TIMESTAMP:
                {
                    int year = unpackInt();
                    int month = unpackInt();
                    int day = unpackInt();
                    int hour = unpackInt();
                    int min = unpackInt();
                    int sec = unpackInt();
                    arg = new TimestampValue(year, month, day, hour, min, sec, mode, dbType);
                }
                break;
            case DBType.DB_DATETIME:
                {
                    int year = unpackInt();
                    int month = unpackInt();
                    int day = unpackInt();
                    int hour = unpackInt();
                    int min = unpackInt();
                    int sec = unpackInt();
                    int msec = unpackInt();
                    arg = new DatetimeValue(year, month, day, hour, min, sec, msec, mode, dbType);
                }
                break;
            case DBType.DB_SET:
            case DBType.DB_MULTISET:
            case DBType.DB_SEQUENCE:
                {
                    int nCol = unpackInt();
                    arg = new SetValue(unpackSetValue(nCol), mode, dbType);
                }
                break;
            case DBType.DB_OBJECT:
                {
                    int page = unpackInt();
                    short slot = unpackShort();
                    short vol = unpackShort();

                    byte[] bOID = new byte[DataUtilities.OID_BYTE_SIZE];
                    bOID[0] = ((byte) ((page >>> 24) & 0xFF));
                    bOID[1] = ((byte) ((page >>> 16) & 0xFF));
                    bOID[2] = ((byte) ((page >>> 8) & 0xFF));
                    bOID[3] = ((byte) ((page >>> 0) & 0xFF));
                    bOID[4] = ((byte) ((slot >>> 8) & 0xFF));
                    bOID[5] = ((byte) ((slot >>> 0) & 0xFF));
                    bOID[6] = ((byte) ((vol >>> 8) & 0xFF));
                    bOID[7] = ((byte) ((vol >>> 0) & 0xFF));

                    arg = new OidValue(bOID, mode, dbType);
                }
                break;
            case DBType.DB_NULL:
                arg = new NullValue(mode, dbType);
                break;
            default:
                // unknown type
                break;
        }
        return arg;
    }

    private Value[] unpackSetValue(int paramCount) throws TypeMismatchException {
        Value[] args = new Value[paramCount];
        for (int i = 0; i < paramCount; i++) {
            int paramType = unpackInt();
            int paramSize = unpackInt();
            // FIXME: dbType=0 is dummy, it is from legacy code. I'm not sure about it
            Value arg = unpackValue(paramSize, paramType, Value.IN, 0);
            args[i] = (arg);
        }
        return args;
    }

    private void align(int size) {
        DataUtilities.align(buffer, size);
    }
}
