/*
 * Copyright (C) 2008 Search Solution Corporation.
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

import com.cubrid.plcsql.compiler.antlrgen.PcsParser;
import com.cubrid.plcsql.compiler.antlrgen.PcsParserBaseListener;
import com.cubrid.plcsql.compiler.ast.DeclParamIn;
import com.cubrid.plcsql.compiler.ast.DeclVar;
import com.cubrid.plcsql.compiler.ast.ExprId;
import com.cubrid.plcsql.compiler.ast.I_DeclId;
import com.cubrid.plcsql.compiler.ast.I_DeclParam;
import com.cubrid.plcsql.compiler.ast.NodeList;
import org.antlr.v4.runtime.Token;
import org.antlr.v4.runtime.tree.TerminalNode;

public class TempSqlStringifier extends PcsParserBaseListener {

    public NodeList<ExprId> usedVars = new NodeList<>();
    public NodeList<ExprId> intoVars = null;
    public StringBuffer sbuf = new StringBuffer();

    private SymbolStack symbolStack;

    TempSqlStringifier(SymbolStack symbolStack) {
        this.symbolStack = symbolStack;
    }

    @Override
    public void visitTerminal(TerminalNode node) {
        Token t = node.getSymbol();

        int ty = t.getType();
        String txt = t.getText();

        if (withinId) {
            String var = txt.toUpperCase();
            var = Misc.peelId(var);

            I_DeclId decl = symbolStack.getDeclId(var);
            if (decl != null && (decl instanceof DeclVar || decl instanceof I_DeclParam)) {
                if (withinIntoClause) {
                    assert !(decl instanceof DeclParamIn)
                            : "in-parameter " + txt + " cannot be used in into-clauses";
                    intoVars.nodes.add(new ExprId(var, symbolStack.getCurrentScope(), decl));
                } else {
                    usedVars.nodes.add(new ExprId(var, symbolStack.getCurrentScope(), decl));
                    sbuf.append(" ?");
                }
                return;
            }
        }

        if (!withinIntoClause) {

            if (sbuf.length() > 0) {
                sbuf.append(" ");
            }
            sbuf.append(txt);
        }
    }

    @Override
    public void enterS_identifier(PcsParser.S_identifierContext ctx) {
        withinId = true;
    }

    @Override
    public void exitS_identifier(PcsParser.S_identifierContext ctx) {
        withinId = false;
    }

    @Override
    public void enterS_into_clause(PcsParser.S_into_clauseContext ctx) {
        withinIntoClause = true;
        intoVars = new NodeList<>();
    }

    @Override
    public void exitS_into_clause(PcsParser.S_into_clauseContext ctx) {
        withinIntoClause = false;
    }

    private boolean withinId = false;
    private boolean withinIntoClause = false;
}
