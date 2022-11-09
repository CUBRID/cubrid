package com.cubrid.plcsql.compiler;

import com.cubrid.plcsql.compiler.ast.I_Expr;
import com.cubrid.plcsql.compiler.ast.I_DeclId;
import com.cubrid.plcsql.compiler.ast.I_DeclParam;
import com.cubrid.plcsql.compiler.ast.NodeList;
import com.cubrid.plcsql.compiler.ast.ExprId;
import com.cubrid.plcsql.compiler.ast.DeclVar;
import com.cubrid.plcsql.compiler.ast.DeclParamIn;

import org.antlr.v4.runtime.Token;

import org.antlr.v4.runtime.tree.TerminalNode;

import com.cubrid.plcsql.compiler.antlrgen.PcsParser;
import com.cubrid.plcsql.compiler.antlrgen.PcsParserBaseListener;

public class TempSqlStringifier extends PcsParserBaseListener {

    public NodeList<ExprId> usedVars = new NodeList<>();
    public NodeList<ExprId> intoVars = null;
    public StringBuffer sbuf = new StringBuffer();

	@Override public void
    visitTerminal(TerminalNode node) {
        Token t = node.getSymbol();

        int ty = t.getType();
        String txt = t.getText();

        if (withinId) {
            String var = txt.toUpperCase();
            var = Misc.peelId(var);

            I_DeclId decl = SymbolStack.getDeclId(var);
            if (decl != null && (decl instanceof DeclVar || decl instanceof I_DeclParam)) {
                if (withinIntoClause) {
                    assert !(decl instanceof DeclParamIn): "in-parameter " + txt + " cannot be used in into-clauses";
                    intoVars.nodes.add(new ExprId(var, SymbolStack.getCurrentScope(), decl));
                } else {
                    usedVars.nodes.add(new ExprId(var, SymbolStack.getCurrentScope(), decl));
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

    @Override public void
    enterS_identifier(PcsParser.S_identifierContext ctx) {
        withinId = true;
    }

    @Override public void
    exitS_identifier(PcsParser.S_identifierContext ctx) {
        withinId = false;
    }


    @Override public void
    enterS_into_clause(PcsParser.S_into_clauseContext ctx) {
        withinIntoClause = true;
        intoVars = new NodeList<>();
    }

    @Override public void
    exitS_into_clause(PcsParser.S_into_clauseContext ctx) {
        withinIntoClause = false;
    }

    private boolean withinId = false;
    private boolean withinIntoClause = false;
}

