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

package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.type.Type;
import com.cubrid.plcsql.compiler.type.TypeVariadic;
import com.cubrid.plcsql.compiler.visitor.AstVisitor;
import org.antlr.v4.runtime.ParserRuleContext;
import java.util.Map;
import java.util.HashMap;

public class TypeSpec extends AstNode {

    public Type type;

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitTypeSpec(this);
    }

    public TypeSpec(ParserRuleContext ctx, Type type) {
        super(ctx);
        this.type = type;
    }

    private static Map<Type, TypeSpec> bogus = new HashMap<>(); // bogus TypeSpec means one with the null context

    static {
        for (int i = Type.IDX_OBJECT; i < Type.BOUND_OF_IDX; i++) {
            Type ty = Type.getTypeByIdx(i);
            TypeVariadic vty = TypeVariadic.getInstance(ty);
            bogus.put(ty, new TypeSpec(null, ty));
            bogus.put(vty, new TypeSpec(null, vty));
        }
    }

    public static TypeSpec getBogus(Type type) {
        TypeSpec ret = bogus.get(type);
        if (ret == null) {
            ret = new TypeSpec(null, type);
            bogus.put(type, ret);
        }

        return ret;
    }
}
