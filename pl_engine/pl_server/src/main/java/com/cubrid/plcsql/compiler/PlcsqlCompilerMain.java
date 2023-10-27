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

import com.cubrid.jsp.Server;
import com.cubrid.jsp.data.CompileInfo;
import com.cubrid.plcsql.compiler.antlrgen.PlcParser;
import com.cubrid.plcsql.compiler.ast.Unit;
import com.cubrid.plcsql.compiler.serverapi.ServerAPI;
import com.cubrid.plcsql.compiler.serverapi.SqlSemantics;
import com.cubrid.plcsql.compiler.visitor.JavaCodeWriter;
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

public class PlcsqlCompilerMain {

    public static CompileInfo compilePLCSQL(String in, boolean verbose) {

        // System.out.println("[TEMP] text to the compiler");
        // System.out.println(in);

        int optionFlags = verbose ? OPT_VERBOSE : 0;
        CharStream input = CharStreams.fromString(in);
        try {
            return compileInner(input, optionFlags);
        } catch (SyntaxError e) {
            CompileInfo err = new CompileInfo(-1, e.line, e.column, e.getMessage());
            return err;
        } catch (SemanticError e) {
            CompileInfo err = new CompileInfo(-1, e.line, e.column, e.getMessage());
            return err;
        } catch (Throwable e) {
            Server.log(e);
            CompileInfo err = new CompileInfo(-1, 0, 0, "internal error");
            return err;
        }
    }

    // ------------------------------------------------------------------
    // Private
    // ------------------------------------------------------------------

    private static final int OPT_VERBOSE = 1;
    private static final int OPT_PRINT_PARSE_TREE = 1 << 1;

    private static ParseTree parse(CharStream input, boolean verbose, String[] sqlTemplate, StringBuilder logStore) {

        long t0 = 0L;
        if (verbose) {
            t0 = System.currentTimeMillis();
        }

        PlcLexerEx lexer = new PlcLexerEx(input);
        CommonTokenStream tokens = new CommonTokenStream(lexer);
        PlcParser parser = new PlcParser(tokens);

        SyntaxErrorIndicator sei = new SyntaxErrorIndicator();
        parser.removeErrorListeners();
        parser.addErrorListener(sei);

        if (verbose) {
            t0 = logElapsedTime(logStore, "  preparing parser", t0);
        }

        ParseTree ret = parser.sql_script();

        if (verbose) {
            logElapsedTime(logStore, "  calling parser", t0);
        }

        if (sei.hasError) {
            throw new SyntaxError(sei.line, sei.column, sei.msg);
        }

        sqlTemplate[0] = lexer.getCreateSqlTemplate();
        return ret;
    }

    private static PrintStream getParseTreePrinterOutStream() {

        // create a output stream to print parse tree
        String outfile =
                Server.getServer().getRootPath().toString()
                        + File.separatorChar
                        + "log"
                        + File.separatorChar
                        + "PL-parse-tree.txt";
        File g = new File(outfile);
        try {
            return new PrintStream(g);
        } catch (FileNotFoundException e) {
            throw new RuntimeException(e);
        }
    }

    private static long logElapsedTime(StringBuilder logStore, String msg, long t0) {
        long t = System.currentTimeMillis();
        logStore.append(String.format("\n%7d : %s", (t - t0), msg));
        return t;
    }

    private static CompileInfo compileInner(CharStream input, int optionFlags) {

        boolean verbose = (optionFlags & OPT_VERBOSE) > 0;

        long t0 = 0L;
        StringBuilder logStore = null;
        if (verbose) {
            t0 = System.currentTimeMillis();
            logStore = new StringBuilder();
        }

        // ------------------------------------------
        // parsing

        String[] sqlTemplate = new String[1];
        ParseTree tree = parse(input, verbose, sqlTemplate, logStore);
        if (tree == null) {
            throw new RuntimeException("parsing failed");
        }

        if (verbose) {
            t0 = logElapsedTime(logStore, "parsing", t0);
        }

        // ------------------------------------------
        // printing parse tree (optional)

        if ((optionFlags & OPT_PRINT_PARSE_TREE) > 0) {
            // walk with a pretty printer to print parse tree
            PrintStream out = getParseTreePrinterOutStream();
            ParseTreePrinter pp = new ParseTreePrinter(out);
            ParseTreeWalker.DEFAULT.walk(pp, tree);
            out.close();

            if (verbose) {
                t0 = logElapsedTime(logStore, "printing parse tree", t0);
            }
        }

        // ------------------------------------------
        // collect Static SQL in the parse tree

        StaticSqlCollector ssc = new StaticSqlCollector();
        ParseTreeWalker.DEFAULT.walk(ssc, tree);

        if (verbose) {
            t0 = logElapsedTime(logStore, "collecting Static SQL statements", t0);
        }

        // ------------------------------------------
        // call server API for each SQL to get its semantic information

        List<String> sqlTexts = new ArrayList(ssc.staticSqlTexts.values());
        List<SqlSemantics> sqlSemantics =
                ServerAPI.getSqlSemantics(sqlTexts); // server interaction may take a long time

        int seqNo = -1;
        Iterator<ParserRuleContext> iterCtx = ssc.staticSqlTexts.keySet().iterator();
        Map<ParserRuleContext, SqlSemantics> staticSqls = new HashMap<>();

        if (sqlSemantics != null) {
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
                    throw new SemanticError(Misc.getLineColumnOf(ctx), ss.errMsg); // s410
                }
            }
        }

        if (verbose) {
            t0 = logElapsedTime(logStore, "semantics check of Static SQL statements by server", t0);
        }

        // ------------------------------------------
        // converting parse tree to AST

        ParseTreeConverter converter = new ParseTreeConverter(staticSqls);
        Unit unit = (Unit) converter.visit(tree);

        if (verbose) {
            t0 = logElapsedTime(logStore, "converting to AST", t0);
        }

        // ------------------------------------------
        // ask server semantic infomation
        // . signature of a global procedure/function
        // . whether a name represent a serial or not
        // . type of a table column
        converter.askServerSemanticQuestions();

        if (verbose) {
            t0 = logElapsedTime(logStore, "getting global semantics information from server", t0);
        }

        // ------------------------------------------
        // typechecking

        TypeChecker typeChecker = new TypeChecker(converter.symbolStack);
        typeChecker.visitUnit(unit);

        if (verbose) {
            t0 = logElapsedTime(logStore, "typechecking", t0);
        }

        // ------------------------------------------
        // Java code generation

        String javaCode = new JavaCodeWriter().buildCodeLines(unit);

        if (verbose) {
            logElapsedTime(logStore, "Java code generation", t0);
        }

        // ------------------------------------------

        if (verbose) {
            Server.log(unit.getClassName() + logStore.toString());
        }

        CompileInfo info =
                new CompileInfo(
                        javaCode,
                        String.format(sqlTemplate[0], unit.getJavaSignature()),
                        unit.getClassName(),
                        unit.getJavaSignature());
        return info;
    }

    private static class SyntaxErrorIndicator extends BaseErrorListener {

        boolean hasError;
        int line;
        int column;
        String msg;

        @Override
        public void syntaxError(
                Recognizer<?, ?> recognizer,
                Object offendingSymbol,
                int line,
                int charPositionInLine,
                String msg,
                RecognitionException e) {

            this.hasError = true;
            this.line = line;
            this.column = charPositionInLine + 1; // charPositionInLine starts from 0
            this.msg = msg;
        }
    }
}
