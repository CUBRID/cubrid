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

import com.cubrid.plcsql.compiler.visitor.AstVisitor;
import org.antlr.v4.runtime.ParserRuleContext;

public class DeclProc extends DeclRoutine {

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitDeclProc(this);
    }

    public DeclProc(
            ParserRuleContext ctx,
            String name,
            String comment,
            StmtLoop.LoopOptimizables loopOptimizables,
            NodeList<DeclParam> paramList,
            int directive,
            NodeList<Decl> decls,
            Body body) {
        super(ctx, name, comment, loopOptimizables, paramList, directive, null, decls, body);
    }

    public DeclProc(
            ParserRuleContext ctx,
            String name,
            String comment,
            StmtLoop.LoopOptimizables loopOptimizables,
            NodeList<DeclParam> paramList,
            int directive) {
        super(ctx, name, comment, loopOptimizables, paramList, directive, null, null, new Body());
    }

    @Override
    public String kind() {
        return "procedure";
    }

    @Override
    public boolean givesBodyOf(Decl d) {

        if (d == null || d.getClass() != DeclProc.class) {
            return false;
        }

        DeclProc other = (DeclProc) d;

        // name and parameters must be the same
        if (!this.name.equals(other.name) || !this.paramList.equals(other.paramList)) {
            return false;
        }

        // this must have a body and the other may not
        return (this.body != null && other.body == null);
    }
}
