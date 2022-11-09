package com.cubrid.plcsql.compiler;

import org.antlr.v4.runtime.ParserRuleContext;

import java.io.PrintStream;

import java.util.Set;
import java.util.TreeSet;

public class Misc {

    public static void printIndent(PrintStream out, int indents) {

        for (int i = 0; i < indents; i++) {
            out.print(INDENT);
        }
    }

    public static String combineLines(String ... lines) {
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

            if (skipFirstLine && i== 0) {
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
        nonTerminal = nonTerminal.substring(0, nonTerminal.length() - 7);   // 7: length of trailing 'Context'
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

    private static final String INDENT = "  ";  // two spaces

    private static String getIndents(int indents) {
        StringBuffer sbuf = new StringBuffer();
        for (int i = 0; i < indents; i++) {
            sbuf.append(INDENT);
        }

        return sbuf.toString();
    }

}

