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

import java.util.HashMap;
import java.util.Map;

public class TypeNumeric extends Type {

    public final int precision;
    public final short scale;

    public static synchronized TypeNumeric getInstance(int precision, short scale) {

        assert precision <= 38 && precision >= 1;
        assert scale <= precision && scale >= 0;

        int key = precision * 100 + scale;
        TypeNumeric ret = instances.get(key);
        if (ret == null) {
            ret = new TypeNumeric(precision, scale);
            instances.put(key, ret);
        }

        return ret;
    }

    // ---------------------------------------------------------------------------
    // Private
    // ---------------------------------------------------------------------------

    private static final Map<Integer, TypeNumeric> instances = new HashMap<>();

    private static String getPlcName(int precision, short scale) {
        return String.format("Numeric(%d, %d)", precision, scale);
    }

    private static String getTypicalValueStr(int precision, short scale) {
        return String.format("cast(? as numeric(%d, %d))", precision, scale);
    }

    private TypeNumeric(int precision, short scale) {
        super(IDX_NUMERIC, getPlcName(precision, scale), "java.math.BigDecimal", getTypicalValueStr(precision, scale));
        this.precision = precision;
        this.scale = scale;
    }
}
