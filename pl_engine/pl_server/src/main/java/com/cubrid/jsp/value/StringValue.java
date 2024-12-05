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

import com.cubrid.jsp.Server;
import com.cubrid.jsp.SysParam;
import com.cubrid.jsp.data.DBType;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.plcsql.predefined.sp.SpLib;
import java.io.UnsupportedEncodingException;
import java.math.BigDecimal;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;

public class StringValue extends Value {

    protected String getTypeName() {
        return TYPE_NAME_STRING;
    }

    private byte[] primitiveValue;

    public StringValue(byte[] value, int codeset) {
        super();
        this.primitiveValue = value;
        this.codeset = codeset;
        this.resolved = null;
        this.dbType = DBType.DB_STRING;
    }

    public StringValue(byte[] value) {
        this(value, Server.getConfig().getServerCodesetId());
    }

    public StringValue(String value) {
        this(value.getBytes(Server.getConfig().getServerCharset()));
    }

    @Override
    public byte toByte() throws TypeMismatchException {
        return SpLib.convStringToByte(toString());
    }

    @Override
    public byte[] toByteArray() throws TypeMismatchException {
        return primitiveValue;
    }

    @Override
    public short toShort() throws TypeMismatchException {
        return SpLib.convStringToShort(toString());
    }

    @Override
    public int toInt() throws TypeMismatchException {
        return SpLib.convStringToInt(toString());
    }

    @Override
    public long toLong() throws TypeMismatchException {
        return SpLib.convStringToBigint(toString());
    }

    @Override
    public float toFloat() throws TypeMismatchException {
        return SpLib.convStringToFloat(toString());
    }

    @Override
    public double toDouble() throws TypeMismatchException {
        return SpLib.convStringToDouble(toString());
    }

    @Override
    public Byte toByteObject() throws TypeMismatchException {
        return SpLib.convStringToByte(toString());
    }

    @Override
    public Short toShortObject() throws TypeMismatchException {
        return SpLib.convStringToShort(toString());
    }

    @Override
    public Integer toIntegerObject() throws TypeMismatchException {
        return SpLib.convStringToInt(toString());
    }

    @Override
    public Long toLongObject() throws TypeMismatchException {
        return SpLib.convStringToBigint(toString());
    }

    @Override
    public Float toFloatObject() throws TypeMismatchException {
        return SpLib.convStringToFloat(toString());
    }

    @Override
    public Double toDoubleObject() throws TypeMismatchException {
        return SpLib.convStringToDouble(toString());
    }

    @Override
    public Date toDate() throws TypeMismatchException {
        return SpLib.convStringToDate(toString());
    }

    @Override
    public Time toTime() throws TypeMismatchException {
        return SpLib.convStringToTime(toString());
    }

    @Override
    public Timestamp toTimestamp() throws TypeMismatchException {
        return SpLib.convStringToTimestamp(toString());
    }

    @Override
    public Timestamp toDatetime() throws TypeMismatchException {
        return SpLib.convStringToDatetime(toString());
    }

    @Override
    public BigDecimal toBigDecimal() throws TypeMismatchException {
        return SpLib.convStringToNumeric(toString());
    }

    @Override
    public Object toObject() throws TypeMismatchException {
        return toString();
    }

    @Override
    public String toString() {
        if (resolved == null) {
            try {
                resolved = new String(primitiveValue, SysParam.getCodesetString(this.codeset));
            } catch (UnsupportedEncodingException e) {
                // just return null
                Server.log(e);
            }
        }
        return (String) resolved;
    }
}
