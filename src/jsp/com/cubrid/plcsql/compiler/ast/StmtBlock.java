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

import org.antlr.v4.runtime.ParserRuleContext;

import com.cubrid.plcsql.compiler.SemanticError;
import com.cubrid.plcsql.compiler.visitor.AstVisitor;

import com.cubrid.plcsql.compiler.Misc;

public class StmtBlock extends Stmt {

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitStmtBlock(this);
    }

    public final String block;
    public final NodeList<Decl> decls;
    public final Body body;

    public StmtBlock(ParserRuleContext ctx, String block, NodeList<Decl> decls, Body body) {
        super(ctx);

        this.block = block;
        this.decls = decls;
        this.body = body;
    }

    @Override
    public String toJavaCode() {

        String strDeclClass =
                decls == null
                        ? "// no declarations"
                        : tmplDeclClass
                                .replace("%'BLOCK'%", block)
                                .replace(
                                        "  %'DECLARATIONS'%",
                                        Misc.indentLines(decls.toJavaCode(), 1));

        return tmplBlock
                .replace("  %'DECL-CLASS'%", Misc.indentLines(strDeclClass, 1))
                .replace("  %'BODY'%", Misc.indentLines(body.toJavaCode(), 1));
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private static final String tmplBlock =
            Misc.combineLines("{", "", "  %'DECL-CLASS'%", "", "  %'BODY'%", "}");

    private static final String tmplDeclClass =
            Misc.combineLines(
                    "class Decl_of_%'BLOCK'% {",
                    "  Decl_of_%'BLOCK'%() throws Exception {};",
                    "  %'DECLARATIONS'%",
                    "}",
                    "Decl_of_%'BLOCK'% %'BLOCK'% = new Decl_of_%'BLOCK'%();");
}
