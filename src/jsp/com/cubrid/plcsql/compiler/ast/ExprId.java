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

import com.cubrid.plcsql.compiler.Scope;
import com.cubrid.plcsql.compiler.visitor.AstVisitor;
import org.antlr.v4.runtime.ParserRuleContext;

public class ExprId extends Expr {

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitExprId(this);
    }

    public final String name;
    public final Scope scope;
    public final DeclId decl;
    public boolean prefixDeclBlock;

    public ExprId(ParserRuleContext ctx, String name, Scope scope, DeclId decl) {
        super(ctx);

        this.name = name;
        this.scope = scope;
        this.decl = decl;
        prefixDeclBlock = decl.scope().declDone;
    }

    @Override
    public String toJavaCode() {
        if (decl instanceof DeclParamOut) {
            return String.format("%s[0]", name);
        } else if (decl instanceof DeclParamIn) {
            return String.format("%s", name);
        } else if (decl instanceof DeclForIter) {
            return String.format("%s_i%d", name, decl.scope().level);
        } else if (decl instanceof DeclForRecord) {
            return String.format("%s_r%d", name, decl.scope().level);
        } else if (decl instanceof DeclConst || decl instanceof DeclCursor) {
            if (prefixDeclBlock) {
                return String.format("%s.%s", decl.scope().block, name);
            } else {
                return String.format("%s", name);
            }
        } else if (decl instanceof DeclVar) {
            if (prefixDeclBlock) {
                return String.format("%s.%s[0]", decl.scope().block, name);
            } else {
                return String.format("%s[0]", name);
            }
        } else {
            assert false;
            return null;
        }
    }

    public String toJavaCodeForOutParam() {
        if (decl instanceof DeclParamOut) {
            return String.format("%s", name);
        } else if (decl instanceof DeclVar) {
            if (prefixDeclBlock) {
                return String.format("%s.%s", decl.scope().block, name);
            } else {
                return String.format("%s", name);
            }
        } else {
            assert false;
            return null;
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
