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

import com.cubrid.plcsql.compiler.Misc;
import com.cubrid.plcsql.compiler.visitor.AstVisitor;
import org.antlr.v4.runtime.ParserRuleContext;

public class StmtCase extends Stmt {

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitStmtCase(this);
    }

    public final int level;
    public final Expr selector;
    public final NodeList<CaseStmt> whenParts;
    public final NodeList<Stmt> elsePart;

    public StmtCase(
            ParserRuleContext ctx,
            int level,
            Expr selector,
            NodeList<CaseStmt> whenParts,
            NodeList<Stmt> elsePart) {
        super(ctx);

        this.level = level;
        this.selector = selector;
        this.whenParts = whenParts;
        this.elsePart = elsePart;
    }

    @Override
    public String toJavaCode() {

        assert selectorType != null;

        String elseCode;
        if (elsePart == null) {
            elseCode = "throw new CASE_NOT_FOUND();";
        } else {
            elseCode = elsePart.toJavaCode();
        }

        return tmplStmtCase
                .replace("%'SELECTOR-TYPE'%", selectorType.toJavaCode())
                .replace("%'SELECTOR-VALUE'%", selector.toJavaCode())
                .replace("  %'WHEN-PARTS'%", Misc.indentLines(whenParts.toJavaCode(" else "), 1))
                .replace("    %'ELSE-PART'%", Misc.indentLines(elseCode, 2))
                .replace("%'LEVEL'%", "" + level) // level replacement must go last
        ;
    }

    public void setSelectorType(TypeSpec ty) {
        this.selectorType = ty;
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private TypeSpec selectorType;

    private static final String tmplStmtCase =
            Misc.combineLines(
                    "{",
                    "  %'SELECTOR-TYPE'% selector_%'LEVEL'% = %'SELECTOR-VALUE'%;",
                    "  %'WHEN-PARTS'% else {",
                    "    %'ELSE-PART'%",
                    "  }",
                    "}");
}
