/*
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

package com.cubrid.plcsql.compiler.type;

import com.cubrid.jsp.value.NumericValue;
import com.cubrid.plcsql.compiler.InstanceStore;

public class TypeNumeric extends Type {

    public final int precision;
    public final short scale;
    /*
     * unique key formula: precision * KEY_MULTIPLIER + scale
     *
     * scale range: MIN(-214) ~ MAX(252), total span = 466
     * For two (precision, scale) pairs, (p1, s1) and (p2, s2) which have the same key,
     * p1 * M + s1 = p2 * M + s2
     * (p1 - p2) * M = (s2 - s1)
     * Since | s2 - s1 | is a multiple of M (500) and is less than or equal to 466,
     * (s2 - s1)  can only be zero and so is (p1 - p2). That is, p1 = p2 and s1 = s2.
     * Therefore, two different (precision, scale) pairs cannot have the same key.
     *
     * using 500 for safety margin.
     *
     * Note: simple integer arithmetic is chosen over alternatives
     *       (string key, composite object) for better performance.
     */
    private static final int KEY_MULTIPLIER = 500;

    public static TypeNumeric getInstance(InstanceStore iStore, int precision, short scale) {

        assert precision <= NumericValue.DB_MAX_NUMERIC_PRECISION
                && precision >= NumericValue.DB_MIN_NUMERIC_PRECISION;
        assert scale <= NumericValue.DB_MAX_NUMERIC_SCALE
                && scale >= NumericValue.DB_MIN_NUMERIC_SCALE;

        int key = precision * KEY_MULTIPLIER + scale;
        TypeNumeric ret = iStore.typeNumeric.get(key);
        if (ret == null) {
            ret = new TypeNumeric(precision, scale);
            iStore.typeNumeric.put(key, ret);
        }

        return ret;
    }

    // ---------------------------------------------------------------------------
    // Private
    // ---------------------------------------------------------------------------

    private static String getPlcName(int precision, short scale) {
        if (precision == NumericValue.DB_DEFAULT_NUMERIC_PRECISION) {
            return "Numeric";
        } else {
            return String.format("Numeric(%d, %d)", precision, scale);
        }
    }

    private static String getTypicalValueStr(int precision, short scale) {
        if (precision == NumericValue.DB_DEFAULT_NUMERIC_PRECISION) {
            return "cast(0.1 as numeric)";
        } else {
            return String.format("cast(0.1 as numeric(%d, %d))", precision, scale);
        }
    }

    private TypeNumeric(int precision, short scale) {
        super(
                IDX_NUMERIC,
                getPlcName(precision, scale),
                "java.math.BigDecimal",
                getTypicalValueStr(precision, scale));
        this.precision = precision;
        this.scale = scale;
    }
}
