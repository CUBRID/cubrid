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
import com.cubrid.plcsql.compiler.visitor.AstVisitor;
import java.util.Set;
import org.antlr.v4.runtime.ParserRuleContext;

public class ExprField extends Expr implements AssignTarget {

    public void setType(Type type) {
        this.type = type;
    }

    public void setColIndex(int colIndex) {
        this.colIndex = colIndex;
    }

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitExprField(this);
    }

    public final ExprId record;
    public final String fieldName;

    public ExprField(ParserRuleContext ctx, ExprId record, String fieldName) {
        super(ctx);

        this.record = record;
        this.fieldName = fieldName;
    }

    @Override
    public String javaCode() {

        if (colIndex > 0) {

            // record is for a Static SQL
            //
            assert type != null;
            return String.format("(%s.%s)", record.javaCode(), fieldName);
        } else {

            // record is for a Dynamic SQL
            //
            assert type == null;
            return String.format(
                    "getFieldWithName(%s_r%d, \"%s\")",
                    record.name, record.decl.scope.level, fieldName);
        }
    }

    @Override
    public String javaCodeForOutParam() {

        if (colIndex > 0) {

            // record is for a Static SQL
            //
            assert type != null;
            return String.format("(%s.%s)", record.javaCode(), fieldName);
        } else {

            // record is for a Dynamic SQL
            //
            assert type == null;
            throw new RuntimeException("unreachable");
        }

    }

    @Override
    public String name() {
        return record.name + "." + fieldName;
    }

    @Override
    public boolean isAssignableTo() {
        return record.isAssignableTo();
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private Type type;
    private int colIndex;
}
