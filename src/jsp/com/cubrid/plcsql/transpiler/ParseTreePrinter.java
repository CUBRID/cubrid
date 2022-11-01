package com.cubrid.plcsql.transpiler;

import org.antlr.v4.runtime.Token;
import org.antlr.v4.runtime.ParserRuleContext;

import org.antlr.v4.runtime.tree.TerminalNode;

import com.cubrid.plcsql.transpiler.antlrgen.PcsParser;
import com.cubrid.plcsql.transpiler.antlrgen.PcsParserBaseListener;

import java.io.PrintStream;

public class ParseTreePrinter extends PcsParserBaseListener {

    public ParseTreePrinter(PrintStream out, String infile) {
        super();
        this.out = out;
        this.infile = infile;
    }

	@Override public void enterEveryRule(ParserRuleContext ctx) {

        String nonTerminal = Misc.ctxToNonTerminalName(ctx);
        if (nonTerminal.equals("sql_script")) {
            out.println("# " + infile);
        }

        Misc.printIndent(out, level);
        out.println(nonTerminal);
        level++;
    }

	@Override public void exitEveryRule(ParserRuleContext ctx) {

        level--;

        String nonTerminal = Misc.ctxToNonTerminalName(ctx);
        if (nonTerminal.equals("sql_script")) {
            if (level != 0) {
                throw new RuntimeException("unreachable");
            }
        }
    }

	@Override public void visitTerminal(TerminalNode node) {
        Misc.printIndent(out, level);
        Token t = node.getSymbol();
        out.println(String.format("'%s': %s", t.getText(),
            PcsParser.VOCABULARY.getSymbolicName(t.getType())));
    }

    private int level = 0;
    private PrintStream out;
    private String infile;
}

