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

import com.cubrid.jsp.exception.TypeMismatchException;
import java.math.BigDecimal;

public class ByteValue extends Value {
    private byte value;

    public ByteValue(byte value) {
        super();
        this.value = value;
    }

    public ByteValue(byte value, int mode) {
        super(mode);
        this.value = value;
    }

    @Override
    public byte toByte() throws TypeMismatchException {
        return value;
    }

    @Override
    public short toShort() throws TypeMismatchException {
        return value;
    }

    @Override
    public int toInt() throws TypeMismatchException {
        return value;
    }

    @Override
    public long toLong() throws TypeMismatchException {
        return value;
    }

    @Override
    public float toFloat() throws TypeMismatchException {
        return value;
    }

    @Override
    public double toDouble() throws TypeMismatchException {
        return value;
    }

    @Override
    public Byte toByteObject() throws TypeMismatchException {
        return new Byte(value);
    }

    @Override
    public Short toShortObject() throws TypeMismatchException {
        return new Short(value);
    }

    @Override
    public Integer toIntegerObject() throws TypeMismatchException {
        return new Integer(value);
    }

    @Override
    public Long toLongObject() throws TypeMismatchException {
        return new Long(value);
    }

    @Override
    public Float toFloatObject() throws TypeMismatchException {
        return new Float(value);
    }

    @Override
    public Double toDoubleObject() throws TypeMismatchException {
        return new Double(value);
    }

    @Override
    public BigDecimal toBigDecimal() throws TypeMismatchException {
        return new BigDecimal(value);
    }

    @Override
    public Object toObject() throws TypeMismatchException {
        return toByteObject();
    }

    @Override
    public String toString() {
        return "" + value;
    }
}
