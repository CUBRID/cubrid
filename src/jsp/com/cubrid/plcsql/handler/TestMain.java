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
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
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
        long t0 = 0, t = 0;
        CompileInfo info = new CompileInfo();

        if (verbose) {
            t0 = System.currentTimeMillis();
        }

        // ------------------------------------------
        // preparing parser

        ANTLRInputStream input = new ANTLRInputStream(in);
        PcsLexerEx lexer = new PcsLexerEx(input);
        CommonTokenStream tokens = new CommonTokenStream(lexer);
        PcsParser parser = new PcsParser(tokens);
        SyntaxErrorIndicator sei = new SyntaxErrorIndicator();
        parser.addErrorListener(sei);

        if (verbose) {
            System.out.println(
                    String.format(
                            "preparing parser: %f sec",
                            ((t = System.currentTimeMillis()) - t0) / 1000.0));
            t0 = t;
        }

        // ------------------------------------------
        // parsing

        ParseTree ret = parser.sql_script();
        if (verbose) {
            System.out.println(
                    String.format(
                            "parsing: %f sec", ((t = System.currentTimeMillis()) - t0) / 1000.0));
            t0 = t;
        }
        if (ret == null) {
            return info;
        }

        // ------------------------------------------
        // converting

        ParseTreeConverter converter = new ParseTreeConverter(null); // TODO: replace null
        Unit unit = (Unit) converter.visit(ret);

        if (verbose) {
            System.out.println(
                    String.format(
                            "converting: %f sec",
                            ((t = System.currentTimeMillis()) - t0) / 1000.0));
            t0 = t;
        }

        // ------------------------------------------
        // typechecking

        TypeChecker typeChecker = new TypeChecker(converter.symbolStack);
        typeChecker.visitUnit(unit);

        if (verbose) {
            System.out.println(
                    String.format(
                            "typechecking: %f sec",
                            ((t = System.currentTimeMillis()) - t0) / 1000.0));
            t0 = t;
        }

        // ------------------------------------------

        info.translated = unit.toJavaCode();
        info.sqlTemplate = String.format(lexer.getCreateSqlTemplate(), unit.getJavaSignature());
        info.className = unit.getClassName();

        return info;
    }

    private static ParseTree parse(String inFilePath, String[] sqlTemplate) {

        long t0, t;

        t0 = System.currentTimeMillis();

        File f = new File(inFilePath);
        if (!f.isFile()) {
            throw new RuntimeException(inFilePath + " is not a file");
        }

        FileInputStream in;
        try {
            in = new FileInputStream(f);
        } catch (FileNotFoundException e) {
            throw new RuntimeException(e);
        }

        ANTLRInputStream input;
        try {
            input = new ANTLRInputStream(in);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }

        PcsLexerEx lexer = new PcsLexerEx(input);
        CommonTokenStream tokens = new CommonTokenStream(lexer);
        PcsParser parser = new PcsParser(tokens);

        SyntaxErrorIndicator sei = new SyntaxErrorIndicator();
        parser.addErrorListener(sei);

        System.out.println(
                String.format(
                        "  preparing parser: %f sec",
                        ((t = System.currentTimeMillis()) - t0) / 1000.0));
        t0 = t;

        ParseTree ret = parser.sql_script();

        System.out.println(
                String.format(
                        "  calling parser: %f sec", (System.currentTimeMillis() - t0) / 1000.0));

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

    public static void main(String[] args) {

        if (args.length == 0) {
            throw new RuntimeException("requires arguments (PL/CSQL file paths)");
        }

        long t, t0;

        boolean optPrintParseTree = false;

        int i;
        for (i = 0; i < args.length; i++) {
            String arg = args[i];
            if (arg.startsWith("-")) {
                if ("-p".equals(arg)) {
                    optPrintParseTree = true;
                } else {
                    throw new RuntimeException("unknown option " + arg);
                }
            } else {
                break;
            }
        }

        int failCnt = 0;
        for (int j = i; j < args.length; j++) {

            System.out.println(String.format("file #%d: %s", j - i, args[j]));

            try {
                t0 = System.currentTimeMillis();

                // ------------------------------------------
                // parsing

                String infile = args[j];
                String[] sqlTemplate = new String[1];
                ParseTree tree = parse(infile, sqlTemplate);
                if (tree == null) {
                    throw new RuntimeException("parsing failed");
                }

                System.out.println(
                        String.format(
                                "parsing: %f sec",
                                ((t = System.currentTimeMillis()) - t0) / 1000.0));
                t0 = t;

                PrintStream out;

                // ------------------------------------------
                // printing parse tree (optional)

                if (optPrintParseTree) {
                    // walk with a pretty printer to print parse tree
                    out = getParseTreePrinterOutStream(j - i);
                    ParseTreePrinter pp = new ParseTreePrinter(out, infile);
                    ParseTreeWalker.DEFAULT.walk(pp, tree);
                    out.close();

                    System.out.println(
                            String.format(
                                    "printing: %f sec",
                                    ((t = System.currentTimeMillis()) - t0) / 1000.0));
                    t0 = t;
                }

                // ------------------------------------------
                // collect Static SQL in the parse tree

                StaticSqlCollector ssc = new StaticSqlCollector();
                ParseTreeWalker.DEFAULT.walk(ssc, tree);

                System.out.println(
                        String.format(
                                "collecting Static SQL: %f sec",
                                ((t = System.currentTimeMillis()) - t0) / 1000.0));
                t0 = t;

                // ------------------------------------------
                // call server API for each SQL to get its semantic information

                List<String> sqlTexts = new ArrayList(ssc.staticSqlTexts.values());
                List<SqlSemantics> sqlSemantics = ServerAPI.getSqlSemantics(sqlTexts);

                Map<ParserRuleContext, SqlSemantics> staticSqls = new HashMap<>();
                Iterator<SqlSemantics> iterSql = sqlSemantics.iterator();
                for (ParserRuleContext ctx : ssc.staticSqlTexts.keySet()) {
                    SqlSemantics ss = iterSql.next();
                    assert ss != null;
                    if (ss.errCode == 0) {
                        staticSqls.put(ctx, ss);
                    } else {
                        throw new SemanticError(Misc.getLineOf(ctx), ss.errMsg); // s410
                    }
                }

                System.out.println(
                        String.format(
                                "analyzing Static SQL: %f sec",
                                ((t = System.currentTimeMillis()) - t0) / 1000.0));
                t0 = t;

                // ------------------------------------------
                // converting parse tree to AST

                ParseTreeConverter converter = new ParseTreeConverter(staticSqls);
                Unit unit = (Unit) converter.visit(tree);

                System.out.println(
                        String.format(
                                "converting: %f sec",
                                ((t = System.currentTimeMillis()) - t0) / 1000.0));
                t0 = t;

                // ------------------------------------------
                // ask server semantic infomation
                // . signature of a global procedure/function
                // . whether a name represent a serial or not
                // . type of a table column
                converter.askServerSemanticQuestions();

                System.out.println(
                        String.format(
                                "asking server global semantics : %f sec",
                                ((t = System.currentTimeMillis()) - t0) / 1000.0));
                t0 = t;

                // ------------------------------------------
                // typechecking

                TypeChecker typeChecker = new TypeChecker(converter.symbolStack);
                typeChecker.visitUnit(unit);

                System.out.println(
                        String.format(
                                "typechecking: %f sec",
                                ((t = System.currentTimeMillis()) - t0) / 1000.0));
                t0 = t;

                // ------------------------------------------
                // generating Java file

                out = getJavaCodeOutStream(unit.getClassName());
                out.println(String.format("// seq=%05d, input-file=%s", j - i, infile));
                out.print(unit.toJavaCode());

                System.out.println(
                        String.format(
                                "generating Java file: %f sec",
                                ((t = System.currentTimeMillis()) - t0) / 1000.0));
                t0 = t;

                // ------------------------------------------

                out.close();

                /*
                System.out.println(
                        "create statement: " + String.format(sqlTemplate[0], unit.getJavaSignature()));
                 */

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
                        "total: %d, success: %d, failure: %d",
                        args.length, (args.length - failCnt), failCnt));
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
