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
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.plcsql.predefined.sp.SpLib;
import java.math.BigDecimal;
import java.sql.Time;
import java.sql.Timestamp;

public class ShortValue extends Value {

    protected String getTypeName() {
        return TYPE_NAME_SHORT;
    }

    private short value;

    public ShortValue(short value) {
        super();
        this.value = value;
        this.dbType = DBType.DB_SHORT;
    }

    @Override
    public byte toByte() throws TypeMismatchException {
        return SpLib.convShortToByte(value);
    }

    @Override
    public short toShort() throws TypeMismatchException {
        return value;
    }

    @Override
    public int toInt() throws TypeMismatchException {
        return SpLib.convShortToInt(value);
    }

    @Override
    public long toLong() throws TypeMismatchException {
        return SpLib.convShortToBigint(value);
    }

    @Override
    public float toFloat() throws TypeMismatchException {
        return SpLib.convShortToFloat(value);
    }

    @Override
    public double toDouble() throws TypeMismatchException {
        return SpLib.convShortToDouble(value);
    }

    @Override
    public Byte toByteObject() throws TypeMismatchException {
        return SpLib.convShortToByte(value);
    }

    @Override
    public Short toShortObject() throws TypeMismatchException {
        return new Short(value);
    }

    @Override
    public Integer toIntegerObject() throws TypeMismatchException {
        return SpLib.convShortToInt(value);
    }

    @Override
    public Long toLongObject() throws TypeMismatchException {
        return SpLib.convShortToBigint(value);
    }

    @Override
    public Float toFloatObject() throws TypeMismatchException {
        return SpLib.convShortToFloat(value);
    }

    @Override
    public Double toDoubleObject() throws TypeMismatchException {
        return SpLib.convShortToDouble(value);
    }

    @Override
    public BigDecimal toBigDecimal() throws TypeMismatchException {
        return SpLib.convShortToNumeric(value);
    }

    @Override
    public Object toObject() throws TypeMismatchException {
        return new Short(value);
    }

    @Override
    public Time toTime() throws TypeMismatchException {
        return SpLib.convShortToTime(value);
    }

    @Override
    public Timestamp toTimestamp() throws TypeMismatchException {
        return SpLib.convShortToTimestamp(value);
    }

    @Override
    public String toString() {
        return SpLib.convShortToString(value);
    }
}
