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

public class StmtBasicLoop extends Stmt {

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitStmtBasicLoop(this);
    }

    public final DeclLabel declLabel;
    public final NodeList<Stmt> stmts;

    public StmtBasicLoop(ParserRuleContext ctx, DeclLabel declLabel, NodeList<Stmt> stmts) {
        super(ctx);

        this.declLabel = declLabel;
        this.stmts = stmts;
    }

    @Override
    public String toJavaCode() {
        return tmpl.replace(
                        "%'OPT-LABEL'%", declLabel == null ? "// no label" : declLabel.toJavaCode())
                .replace("  %'STATEMENTS'%", Misc.indentLines(stmts.toJavaCode(), 1));
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    // NOTE: why I use 'while(opNot(false))' instead of simpler 'while(true)':
    // Compiling Java code below with javac causes 'unreachable statement' error
    //     while (true) {
    //         continue;
    //     }
    //     ... // a statement
    // However the following does not
    //     while (opNot(false)) {
    //         continue;
    //     }
    //     ... // a statement
    // It seems that static analysis of javac does not go beyond method call boundaries

    private static final String tmpl =
            Misc.combineLines("%'OPT-LABEL'%", "while (opNot(false)) {", "  %'STATEMENTS'%", "}");
}
