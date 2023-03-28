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

package com.cubrid.plcsql.handler;

import com.cubrid.jsp.data.CompileInfo;
import com.cubrid.plcsql.compiler.Misc;
import com.cubrid.plcsql.compiler.ParseTreeConverter;
import com.cubrid.plcsql.compiler.ParseTreePrinter;
import com.cubrid.plcsql.compiler.PcsLexerEx;
import com.cubrid.plcsql.compiler.SemanticError;
import com.cubrid.plcsql.compiler.ServerAPI;
import com.cubrid.plcsql.compiler.SqlSemantics;
import com.cubrid.plcsql.compiler.StaticSqlCollector;
import com.cubrid.plcsql.compiler.antlrgen.PcsParser;
import com.cubrid.plcsql.compiler.ast.Unit;
import com.cubrid.plcsql.compiler.visitor.TypeChecker;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.PrintStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import org.antlr.v4.runtime.*;
import org.antlr.v4.runtime.tree.*;

public class TestMain {

    public static CompileInfo compilePLCSQL(String in, boolean verbose) {
        int optionFlags = verbose ? OPT_VERBOSE : 0;
        CharStream input = CharStreams.fromString(in);
        return compileInner(input, optionFlags, 0, null);
    }

    public static void main(String[] args) {

        if (args.length == 0) {
            throw new RuntimeException("requires arguments (PL/CSQL file paths)");
        }

        int optionFlags = OPT_VERBOSE;

        int i;
        for (i = 0; i < args.length; i++) {
            String arg = args[i];
            if (arg.startsWith("-")) {
                if ("-p".equals(arg)) {
                    optionFlags |= OPT_PRINT_PARSE_TREE;
                } else {
                    throw new RuntimeException("unknown option " + arg);
                }
            } else {
                break;
            }
        }

        int total = (args.length - i);
        int failCnt = 0;
        for (int j = i; j < args.length; j++) {

            int seq = j - i;
            String infile = args[j];
            System.out.println(String.format("file #%d: %s", seq, infile));

            try {
                // compile
                CharStream input = CharStreams.fromFileName(infile);
                CompileInfo info = compileInner(input, optionFlags, seq, infile);

                // generate Java file
                long t0 = System.currentTimeMillis();
                PrintStream out = getJavaCodeOutStream(info.className);
                out.println(String.format("// seq=%05d, input-file=%s", seq, infile));
                out.print(info.translated);
                out.close();
                printElapsedTime("generating Java file", t0);

                //
                System.out.println("create statement: " + info.createStmt);
                System.out.println(" - success");

            } catch (Throwable e) {
                if (e instanceof SemanticError) {
                    System.err.println(
                            "Semantic Error on line " + ((SemanticError) e).lineNo + ":");
                }

                e.printStackTrace();
                System.err.println(" - failure");
                failCnt++;
            }
        }

        System.out.println(
                String.format(
                        "total: %d, success: %d, failure: %d", total, (total - failCnt), failCnt));
    }

    // ------------------------------------------------------------------
    // Private
    // ------------------------------------------------------------------

    private static final int OPT_VERBOSE = 1;
    private static final int OPT_PRINT_PARSE_TREE = 1 << 1;

    private static ParseTree parse(CharStream input, boolean verbose, String[] sqlTemplate) {

        long t0 = 0L;

        if (verbose) {
            t0 = System.currentTimeMillis();
        }

        PcsLexerEx lexer = new PcsLexerEx(input);
        CommonTokenStream tokens = new CommonTokenStream(lexer);
        PcsParser parser = new PcsParser(tokens);

        SyntaxErrorIndicator sei = new SyntaxErrorIndicator();
        parser.addErrorListener(sei);

        if (verbose) {
            t0 = printElapsedTime("  preparing parser", t0);
        }

        ParseTree ret = parser.sql_script();

        if (verbose) {
            printElapsedTime("  calling parser", t0);
        }

        if (sei.hasError) {
            throw new RuntimeException("syntax error");
        }

        sqlTemplate[0] = lexer.getCreateSqlTemplate();
        return ret;
    }

    private static PrintStream getParseTreePrinterOutStream(int seq) {

        // create a output stream to print parse tree
        String outfile = String.format("./pt/T%05d.pt", seq);
        File g = new File(outfile);
        try {
            return new PrintStream(g);
        } catch (FileNotFoundException e) {
            throw new RuntimeException(e);
        }
    }

    private static PrintStream getJavaCodeOutStream(String className) {

        String outfile = String.format("./pt/%s.java", className);
        File g = new File(outfile);
        if (g.exists()) {
            throw new RuntimeException("file exists: " + outfile);
        }

        try {
            return new PrintStream(g);
        } catch (FileNotFoundException e) {
            throw new RuntimeException(e);
        }
    }

    private static long printElapsedTime(String msg, long t0) {
        long t = System.currentTimeMillis();
        System.out.println(String.format("%s: %f sec", msg, (t - t0) / 1000.0));
        return t;
    }

    private static CompileInfo compileInner(
            CharStream input, int optionFlags, int seq, String infile) {

        boolean verbose = (optionFlags & OPT_VERBOSE) > 0;

        long t0 = 0L;
        if (verbose) {
            t0 = System.currentTimeMillis();
        }

        // ------------------------------------------
        // parsing

        String[] sqlTemplate = new String[1];
        ParseTree tree = parse(input, verbose, sqlTemplate);
        if (tree == null) {
            throw new RuntimeException("parsing failed");
        }

        if (verbose) {
            t0 = printElapsedTime("parsing", t0);
        }

        // ------------------------------------------
        // printing parse tree (optional)

        if ((optionFlags & OPT_PRINT_PARSE_TREE) > 0) {
            // walk with a pretty printer to print parse tree
            PrintStream out = getParseTreePrinterOutStream(seq);
            ParseTreePrinter pp = new ParseTreePrinter(out, infile);
            ParseTreeWalker.DEFAULT.walk(pp, tree);
            out.close();

            if (verbose) {
                t0 = printElapsedTime("printing", t0);
            }
        }

        // ------------------------------------------
        // collect Static SQL in the parse tree

        StaticSqlCollector ssc = new StaticSqlCollector();
        ParseTreeWalker.DEFAULT.walk(ssc, tree);

        if (verbose) {
            t0 = printElapsedTime("collecting Static SQL", t0);
        }

        // ------------------------------------------
        // call server API for each SQL to get its semantic information

        List<String> sqlTexts = new ArrayList(ssc.staticSqlTexts.values());
        List<SqlSemantics> sqlSemantics =
                ServerAPI.getSqlSemantics(sqlTexts); // server interaction  may take a long time

        int seqNo = -1;
        Iterator<ParserRuleContext> iterCtx = ssc.staticSqlTexts.keySet().iterator();
        Map<ParserRuleContext, SqlSemantics> staticSqls = new HashMap<>();
        for (SqlSemantics ss : sqlSemantics) {

            assert ss.seqNo >= 0;

            ParserRuleContext ctx = null;
            while (true) {
                ctx = iterCtx.next();
                assert ctx != null;
                seqNo++;
                if (seqNo == ss.seqNo) {
                    break;
                }
            }

            if (ss.errCode == 0) {
                staticSqls.put(ctx, ss);
            } else {
                throw new SemanticError(Misc.getLineOf(ctx), ss.errMsg); // s410
            }
        }

        if (verbose) {
            t0 = printElapsedTime("analyzing Static SQL", t0);
        }

        // ------------------------------------------
        // converting parse tree to AST

        ParseTreeConverter converter = new ParseTreeConverter(staticSqls);
        Unit unit = (Unit) converter.visit(tree);

        if (verbose) {
            t0 = printElapsedTime("converting", t0);
        }

        // ------------------------------------------
        // ask server semantic infomation
        // . signature of a global procedure/function
        // . whether a name represent a serial or not
        // . type of a table column
        converter.askServerSemanticQuestions();

        if (verbose) {
            t0 = printElapsedTime("asking server global semantics", t0);
        }

        // ------------------------------------------
        // typechecking

        TypeChecker typeChecker = new TypeChecker(converter.symbolStack);
        typeChecker.visitUnit(unit);

        if (verbose) {
            t0 = printElapsedTime("typechecking", t0);
        }

        // ------------------------------------------
        //

        CompileInfo info = new CompileInfo();
        info.translated = unit.toJavaCode();
        info.createStmt = String.format(sqlTemplate[0], unit.getJavaSignature());
        info.className = unit.getClassName();

        return info;
    }

    private static class SyntaxErrorIndicator extends BaseErrorListener {

        boolean hasError = false;

        @Override
        public void syntaxError(
                Recognizer<?, ?> recognizer,
                Object offendingSymbol,
                int line,
                int charPositionInLine,
                String msg,
                RecognitionException e) {
            hasError = true;
        }
    }
}
