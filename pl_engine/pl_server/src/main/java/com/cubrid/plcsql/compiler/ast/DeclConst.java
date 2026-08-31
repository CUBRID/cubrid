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

import com.cubrid.jsp.data.CompileResponse;
import com.cubrid.plcsql.compiler.serverapi.ServerConstants;
import com.cubrid.plcsql.compiler.visitor.AstVisitor;
import org.antlr.v4.runtime.ParserRuleContext;

public class DeclConst extends DeclIdTypeDeclared {

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitDeclConst(this);
    }

    public final TypeSpec typeSpec;
    public final boolean notNull;
    public final Expr val;

    public DeclConst(
            ParserRuleContext ctx,
            String name,
            String comment,
            TypeSpec typeSpec,
            boolean notNull,
            Expr val) {
        super(ctx, name, comment);

        this.typeSpec = typeSpec;
        this.notNull = notNull;
        this.val = val;
    }

    public TypeSpec typeSpec() {
        return typeSpec;
    }

    @Override
    public String kind() {
        return "constant";
    }

    @Override
    public void addAsPkgItem(CompileResponse resp) {
        resp.addPkgVar(
                typeSpec.type.dbType,
                typeSpec.type.prec,
                typeSpec.type.scale,
                ServerConstants.PKG_VAR_CONSTANT | (notNull ? ServerConstants.PKG_VAR_NOT_NULL : 0),
                name.toLowerCase(),
                comment);
    }
}
