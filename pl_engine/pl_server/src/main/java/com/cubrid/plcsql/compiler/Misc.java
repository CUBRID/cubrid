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

import com.cubrid.plcsql.compiler.ast.NodeList;
import java.io.PrintStream;
import java.util.HashMap;
import java.util.Map;
import org.antlr.v4.runtime.ParserRuleContext;
import org.antlr.v4.runtime.Token;
import org.antlr.v4.runtime.tree.ParseTree;

public class Misc {

    public static class Pair<X, Y> {
        public X e1;
        public Y e2;

        public Pair(X e1, Y e2) {
            this.e1 = e1;
            this.e2 = e2;
        }
    }

    public static final int[] UNKNOWN_LINE_COLUMN = new int[] {-1, -1};
    public static final String INDENT = "  "; // two spaces
    public static final int INDENT_SIZE = INDENT.length();

    public enum RoutineType {
        FUNC,
        PROC,
    }

    public static boolean isEmpty(NodeList nl) {
        return (nl == null || nl.nodes.size() == 0);
    }

    public static String detachPkgName(String routineName) {
        int idx = routineName.indexOf('$');
        return idx >= 0 ? routineName.substring(idx + 1) : routineName;
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

        return new int[] {start.getLine(), start.getCharPositionInLine() + 1};
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

    public static void printIndent(PrintStream out, int indentLevel) {

        for (int i = 0; i < indentLevel; i++) {
            out.print(INDENT);
        }
    }

    public static String combineLines(String... lines) {
        return String.join("\n", lines);
    }

    public static String indentLines(String lines, int indentLevel) {
        return indentLines(lines, indentLevel, false);
    }

    public static String indentLines(String lines, int indentLevel, boolean skipFirstLine) {

        assert lines != null;
        assert indentLevel >= 0;

        if (indentLevel == 0) {
            return lines;
        }

        String[] split = lines.split("\n");
        String indent = getIndent(indentLevel);
        for (int i = 0; i < split.length; i++) {

            if (skipFirstLine && i == 0) {
                continue;
            }

            if (split[i].length() > 0) {
                split[i] = indent + split[i];
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

    public static String getIndent(int indentLevel) {
        if (indentLevel < 10) {
            return smallIndents[indentLevel];
        } else {
            String ret;
            synchronized (bigIndents) {
                ret = bigIndents.get(indentLevel);
                if (ret == null) {
                    ret = makeIndent(indentLevel);
                    bigIndents.put(indentLevel, ret);
                }
            }
            return ret;
        }
    }

    // ----------------------------------------------
    // Private
    // ----------------------------------------------

    private static final String[] smallIndents =
            new String[] {
                makeIndent(0), makeIndent(1), makeIndent(2), makeIndent(3), makeIndent(4),
                makeIndent(5), makeIndent(6), makeIndent(7), makeIndent(8), makeIndent(9)
            };
    private static final Map<Integer, String> bigIndents = new HashMap<Integer, String>();

    private static String peelId(String id) {
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

    private static String makeIndent(int indentLevel) {
        StringBuffer sbuf = new StringBuffer();
        for (int i = 0; i < indentLevel; i++) {
            sbuf.append(INDENT);
        }

        return sbuf.toString();
    }
}
