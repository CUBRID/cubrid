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

package com.cubrid.plcsql.compiler;

import java.io.PrintStream;
import org.antlr.v4.runtime.ParserRuleContext;
import org.antlr.v4.runtime.Token;
import org.antlr.v4.runtime.tree.ParseTree;

public class Misc {

    public enum RoutineType {
        FUNC,
        PROC,
    }

    public static int[] getLineColumnOf(ParserRuleContext ctx) {
        if (ctx == null) {
            return UNKNOWN_LINE_COLUMN;
        }

        Token start = null;
        while (true) {
            start = ctx.getStart();
            if (start == null) {
                ctx = ctx.getParent();
                assert ctx != null;
            } else {
                break;
            }
        }
        assert start != null;

        return new int[] { start.getLine(), start.getCharPositionInLine() + 1 };
    }

    public static String getNormalizedText(ParseTree ctx) {
        assert ctx != null;
        int children = ctx.getChildCount();

        if (children <= 1) {
            return peelId(ctx.getText().toUpperCase());
        } else {
            StringBuffer sbuf = new StringBuffer();
            for (int i = 0; i < children; i++) {
                if (i > 0) {
                    sbuf.append(' ');
                }
                ParseTree child = ctx.getChild(i);
                sbuf.append(getNormalizedText(child));
            }
            return sbuf.toString();
        }
    }

    public static String getNormalizedText(String s) {
        return peelId(s.toUpperCase());
    }

    public static void printIndent(PrintStream out, int indents) {

        for (int i = 0; i < indents; i++) {
            out.print(INDENT);
        }
    }

    public static String combineLines(String... lines) {
        return String.join("\n", lines);
    }

    public static String indentLines(String lines, int indents) {
        return indentLines(lines, indents, false);
    }

    public static String indentLines(String lines, int indents, boolean skipFirstLine) {

        assert lines != null;
        assert indents >= 0;

        if (indents == 0) {
            return lines;
        }

        String[] split = lines.split("\n");
        for (int i = 0; i < split.length; i++) {

            if (skipFirstLine && i == 0) {
                continue;
            }

            if (split[i].length() > 0) {
                split[i] = getIndents(indents) + split[i];
            }
        }

        return String.join("\n", split);
    }

    public static String ctxToNonTerminalName(ParserRuleContext ctx) {
        String nonTerminal = ctx.getClass().getSimpleName();
        nonTerminal =
                nonTerminal.substring(
                        0, nonTerminal.length() - 7); // 7: length of trailing 'Context'
        return nonTerminal.toLowerCase();
    }

    public static String peelId(String id) {
        assert id != null;

        int len = id.length();
        if (len >= 2) {

            if (id.startsWith("\"") && id.endsWith("\"")
                    || id.startsWith("[") && id.endsWith("]")
                    || id.startsWith("`") && id.endsWith("`")) {

                return id.substring(1, len - 1);
            }
        }

        return id;
    }

    // ----------------------------------------------
    // Private
    // ----------------------------------------------

    private static final String INDENT = "  "; // two spaces

    private static final int[] UNKNOWN_LINE_COLUMN = new int[] { 0, 0 };

    private static String getIndents(int indents) {
        StringBuffer sbuf = new StringBuffer();
        for (int i = 0; i < indents; i++) {
            sbuf.append(INDENT);
        }

        return sbuf.toString();
    }
}
