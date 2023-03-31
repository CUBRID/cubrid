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

import java.util.List;

public enum CoercionScheme {
    ComparisonOperator {
        public List<Coerce> getCoercions(List<TypeSpec> argTypes, List<TypeSpec> paramTypes) {
            assert argTypes.size() == 2 && paramTypes.size() == 2;
            return null;    // TODO
        }
    },

    ArithmeticOperator {
        public List<Coerce> getCoercions(List<TypeSpec> argTypes, List<TypeSpec> paramTypes) {
            assert argTypes.size() == 2 && paramTypes.size() == 2;
            return null;    // TODO
        }
    },

    BetweenOperator {
        public List<Coerce> getCoercions(List<TypeSpec> argTypes, List<TypeSpec> paramTypes) {
            assert argTypes.size() == 3 && paramTypes.size() == 3;
            return null;    // TODO
        }
    },

    InOperator {
        public List<Coerce> getCoercions(List<TypeSpec> argTypes, List<TypeSpec> paramTypes) {
            return null;    // TODO
        }
    },

    BitwiseOperator {
        public List<Coerce> getCoercions(List<TypeSpec> argTypes, List<TypeSpec> paramTypes) {
            return null;    // TODO
        }
    },

    Individual {
        public List<Coerce> getCoercions(List<TypeSpec> argTypes, List<TypeSpec> paramTypes) {
            return null;    // TODO
        }
    };

    public abstract List<Coerce> getCoercions(List<TypeSpec> argTypes, List<TypeSpec> paramTypes);
}
