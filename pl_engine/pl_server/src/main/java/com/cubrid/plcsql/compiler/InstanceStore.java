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

package com.cubrid.plcsql.compiler;

import com.cubrid.plcsql.compiler.ast.TypeSpec;
import com.cubrid.plcsql.compiler.type.Type;
import com.cubrid.plcsql.compiler.type.TypeChar;
import com.cubrid.plcsql.compiler.type.TypeNumeric;
import com.cubrid.plcsql.compiler.type.TypeRecord;
import com.cubrid.plcsql.compiler.type.TypeVarchar;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

// store of instances of various classes such as types, coercions
// which are genrated during a single session of compilation

public class InstanceStore {

    // for bogus TypeSpec
    public final Map<Type, TypeSpec> bogusTypeSpec = new HashMap<>();

    // for Coercion.RecordToRecord
    public final CoercionStore recToRec = new CoercionStore(Coercion.RecordToRecord.DUMMY);

    // for Coercion.Identity
    public final CoercionStore identity = new CoercionStore(Coercion.Identity.DUMMY);

    // for Coercion.Conversion
    public final CoercionStore conv = new CoercionStore(Coercion.Conversion.DUMMY);

    // for TypeChar
    public final Map<Integer, TypeChar> typeChar = new HashMap<>();

    // for TypeVarchar
    public final Map<Integer, TypeVarchar> typeVarchar = new HashMap<>();

    // for TypeNumeric
    public final Map<Integer, TypeNumeric> typeNumeric = new HashMap<>();

    // for TypeRecord
    public final Map<List<Misc.Pair<String, Type>>, TypeRecord> typeRecord = new HashMap<>();

    public static class CoercionStore {

        public final Map<Type, Map<Type, Coercion>> store = new HashMap<>();

        CoercionStore(Coercion dummy) {
            this.dummy = dummy;
        }

        Coercion get(Type src, Type dst) {

            Map<Type, Coercion> storeInner = store.get(src);
            if (storeInner == null) {
                storeInner = new HashMap<>();
                store.put(src, storeInner);
            }

            Coercion c = storeInner.get(dst);
            if (c == null) {
                c = dummy.create(src, dst);
                storeInner.put(dst, c);
            }

            return c;
        }

        // ---------------------------------------
        // Private
        // ---------------------------------------

        private final Coercion dummy;
    }
}
