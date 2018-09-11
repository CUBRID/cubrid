/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
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

import java.math.BigDecimal;

import com.cubrid.jsp.exception.TypeMismatchException;

public class NumericValue extends Value {
	private BigDecimal value;

	public NumericValue(String value) {
		super();
		this.value = new BigDecimal(value);
	}

	public NumericValue(String value, int mode, int dbType) {
		super(mode);
		this.value = new BigDecimal(value);
		this.dbType = dbType;
	}

	public byte toByte() throws TypeMismatchException {
		return value.byteValue();
	}

	public short toShort() throws TypeMismatchException {
		return value.shortValue();
	}

	public int toInt() throws TypeMismatchException {
		return value.intValue();
	}

	public long toLong() throws TypeMismatchException {
		return value.longValue();
	}

	public float toFloat() throws TypeMismatchException {
		return value.floatValue();
	}

	public double toDouble() throws TypeMismatchException {
		return value.doubleValue();
	}

	public Byte toByteObject() throws TypeMismatchException {
		return new Byte(value.byteValue());
	}

	public Short toShortObject() throws TypeMismatchException {
		return new Short(value.shortValue());
	}

	public Integer toIntegerObject() throws TypeMismatchException {
		return new Integer(value.intValue());
	}

	public Long toLongObject() throws TypeMismatchException {
		return new Long(value.longValue());
	}

	public Float toFloatObject() throws TypeMismatchException {
		return new Float(value.floatValue());
	}

	public Double toDoubleObject() throws TypeMismatchException {
		return new Double(value.doubleValue());
	}

	public BigDecimal toBigDecimal() throws TypeMismatchException {
		return value;
	}

	public Object toObject() throws TypeMismatchException {
		return toBigDecimal();
	}

	public String toString() {
		return value.toString(); // TODO: using NumberFormat class
	}

	public byte[] toByteArray() throws TypeMismatchException {
		return new byte[] { value.byteValue() };
	}

	public short[] toShortArray() throws TypeMismatchException {
		return new short[] { value.shortValue() };
	}

	public int[] toIntegerArray() throws TypeMismatchException {
		return new int[] { value.intValue() };
	}

	public long[] toLongArray() throws TypeMismatchException {
		return new long[] { value.longValue() };
	}

	public float[] toFloatArray() throws TypeMismatchException {
		return new float[] { value.floatValue() };
	}

	public double[] toDoubleArray() throws TypeMismatchException {
		return new double[] { value.doubleValue() };
	}

	public BigDecimal[] toBigDecimalArray() throws TypeMismatchException {
		return new BigDecimal[] { toBigDecimal() };
	}

	public Object[] toObjectArray() throws TypeMismatchException {
		return new Object[] { toObject() };
	}

	public String[] toStringArray() throws TypeMismatchException {
		return new String[] { toString() };
	}

	public Byte[] toByteObjArray() throws TypeMismatchException {
		return new Byte[] { toByteObject() };
	}

	public Double[] toDoubleObjArray() throws TypeMismatchException {
		return new Double[] { toDoubleObject() };
	}

	public Float[] toFloatObjArray() throws TypeMismatchException {
		return new Float[] { toFloatObject() };
	}

	public Integer[] toIntegerObjArray() throws TypeMismatchException {
		return new Integer[] { toIntegerObject() };
	}

	public Long[] toLongObjArray() throws TypeMismatchException {
		return new Long[] { toLongObject() };
	}

	public Short[] toShortObjArray() throws TypeMismatchException {
		return new Short[] { toShortObject() };
	}
}
