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
import java.io.PrintStream;
import org.antlr.v4.runtime.ParserRuleContext;
import org.antlr.v4.runtime.Token;
import org.antlr.v4.runtime.tree.TerminalNode;

public class ParseTreePrinter extends PcsParserBaseListener {

    public ParseTreePrinter(PrintStream out, String infile) {
        super();
        this.out = out;
        this.infile = infile;
    }

    @Override
    public void enterEveryRule(ParserRuleContext ctx) {

        String nonTerminal = Misc.ctxToNonTerminalName(ctx);
        if (nonTerminal.equals("sql_script")) {
            out.println("# " + infile);
        }

        Misc.printIndent(out, level);
        out.println(nonTerminal);
        level++;
    }

    @Override
    public void exitEveryRule(ParserRuleContext ctx) {

        level--;

        String nonTerminal = Misc.ctxToNonTerminalName(ctx);
        if (nonTerminal.equals("sql_script")) {
            if (level != 0) {
                throw new RuntimeException("unreachable");
            }
        }
    }

    @Override
    public void visitTerminal(TerminalNode node) {
        Misc.printIndent(out, level);
        Token t = node.getSymbol();
        out.println(
                String.format(
                        "'%s': %s",
                        t.getText(), PcsParser.VOCABULARY.getSymbolicName(t.getType())));
    }

    private int level = 0;
    private PrintStream out;
    private String infile;
}
