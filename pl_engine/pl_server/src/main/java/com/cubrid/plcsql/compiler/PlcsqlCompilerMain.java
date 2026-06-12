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
import com.cubrid.jsp.data.CompileRequest;
import com.cubrid.jsp.data.CompileResponse;
import com.cubrid.plcsql.compiler.antlrgen.PlcLexer;
import com.cubrid.plcsql.compiler.antlrgen.PlcParser;
import com.cubrid.plcsql.compiler.ast.Decl;
import com.cubrid.plcsql.compiler.ast.Unit;
import com.cubrid.plcsql.compiler.ast.UnitPkg;
import com.cubrid.plcsql.compiler.ast.UnitSp;
import com.cubrid.plcsql.compiler.ast.loopOpt.SqlUse;
import com.cubrid.plcsql.compiler.error.SemanticError;
import com.cubrid.plcsql.compiler.error.SyntaxError;
import com.cubrid.plcsql.compiler.visitor.JavaCodeWriter;
import com.cubrid.plcsql.compiler.visitor.TypeChecker;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.PrintStream;
import java.util.HashSet;
import java.util.Set;
import org.antlr.v4.runtime.*;
import org.antlr.v4.runtime.tree.*;

public class PlcsqlCompilerMain {

    // temporary code - the owner and revision strings will come from the server
    private static int revision = 1;

    public static CompileResponse compilePLCSQL(CompileRequest request) {
        return compilePLCSQL(request, Integer.toString(revision++));
    }
    // end of temporary code

    public static CompileResponse compilePLCSQL(CompileRequest request, String revision) {

        try {
            return compileInner(new InstanceStore(), revision, request);
        } catch (SyntaxError e) {
            CompileResponse err = new CompileResponse(-1, e.line, e.column, e.getMessage());
            return err;
        } catch (SemanticError e) {
            CompileResponse err = new CompileResponse(-1, e.line, e.column, e.getMessage());
            return err;
        } catch (Throwable e) {
            Server.log(e);
            CompileResponse err = new CompileResponse(-1, 0, 0, "internal error");
            return err;
        }
    }

    // ------------------------------------------------------------------
    // Private
    // ------------------------------------------------------------------

    private static final int OPT_PRINT_PARSE_TREE = 1 << 1;

    private static final String STR_EXPECTING = " expecting ";
    private static final int STR_EXPECTING_LEN = STR_EXPECTING.length();

    private static String cutExpectingClause(String errMsg) {

        int idx;
        if (errMsg != null && (idx = errMsg.lastIndexOf(STR_EXPECTING)) > 0) {

            String tail = errMsg.substring(idx + STR_EXPECTING_LEN);

            if (tail.matches("[A-Z0-9_]+") /* single token name */
                    || (tail.startsWith("'")
                            && tail.endsWith("'")) /* single token of the form '...' */
                    || (tail.startsWith("{")
                            && tail.endsWith("}") /* multiple tokens of the form {...} */)) {

                errMsg = errMsg.substring(0, idx);
            }
        }

        return errMsg;
    }

    private static ParseTree parse(CharStream input, boolean verbose, StringBuilder logStore) {

        long t0 = 0L;
        if (verbose) {
            t0 = System.currentTimeMillis();
        }

        PlcLexer lexer = new PlcLexer(input);

        SyntaxErrorIndicator lei = new SyntaxErrorIndicator(false);
        lexer.removeErrorListeners(); // This removes unwanted console output
        lexer.addErrorListener(lei);

        CommonTokenStream tokens = new CommonTokenStream(lexer);
        PlcParser parser = new PlcParser(tokens);

        SyntaxErrorIndicator sei = new SyntaxErrorIndicator(true);
        parser.removeErrorListeners(); // This removes unwanted console output
        parser.addErrorListener(sei);

        if (verbose) {
            t0 = logElapsedTime(logStore, "  preparing parser", t0);
        }

        ParseTree ret = parser.sql_script();

        if (verbose) {
            logElapsedTime(logStore, "  calling parser", t0);
        }

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

    private static CompileResponse compileInner(
            InstanceStore iStore, String revision, CompileRequest request) {

        int type = request.type;
        String code = request.code;
        String bodyCode = request.bodyCode;
        String owner = request.owner;

        // System.out.println("[TEMP] text to the compiler");
        // System.out.println(code);

        assert (type == CompileRequest.PLCSQL_COMPILE_TYPE_SP
                        && !Misc.isEmptyStr(code)
                        && Misc.isEmptyStr(bodyCode))
                || (type == CompileRequest.PLCSQL_COMPILE_TYPE_PKG_SPEC && !Misc.isEmptyStr(code))
                || (type == CompileRequest.PLCSQL_COMPILE_TYPE_PKG_BODY
                        && Misc.isEmptyStr(code)
                        && !Misc.isEmptyStr(bodyCode));

        boolean verbose = request.mode.contains("v");
        boolean printParseTree = request.mode.contains("p");

        long t0 = 0L;
        StringBuilder logStore = null;
        if (verbose) {
            t0 = System.currentTimeMillis();
            logStore = new StringBuilder();
        }

        // ------------------------------------------
        // parsing

        ParseTree codeTree = null, bodyCodeTree = null;

        if (type == CompileRequest.PLCSQL_COMPILE_TYPE_SP) {
            // for an SP

            CharStream input = CharStreams.fromString(code);
            codeTree = parse(input, verbose, logStore);
            if (codeTree == null) {
                throw new RuntimeException("parsing failed");
            }
        } else {
            // for a Package

            if (!Misc.isEmptyStr(bodyCode)) {
                CharStream input = CharStreams.fromString(bodyCode);
                bodyCodeTree = parse(input, verbose, logStore);
                if (bodyCodeTree == null) {
                    throw new RuntimeException("parsing failed for the package body code");
                }
            }

            if (Misc.isEmptyStr(code)) {
                assert type == CompileRequest.PLCSQL_COMPILE_TYPE_PKG_BODY;
                // just return:
                // semantic check and further processes are not possible without a spec code
                return new CompileResponse(type);
            } else {
                CharStream input = CharStreams.fromString(code);
                codeTree = parse(input, verbose, logStore);
                if (codeTree == null) {
                    throw new RuntimeException("parsing failed for the package spec code");
                }
            }
        }

        if (verbose) {
            t0 = logElapsedTime(logStore, "parsing", t0);
        }

        // ------------------------------------------
        // printing parse tree (optional)

        if (printParseTree) {

            // walk with a pretty printer to print parse tree
            PrintStream out = getParseTreePrinterOutStream();
            ParseTreePrinter pp = new ParseTreePrinter(out);

            if (type == CompileRequest.PLCSQL_COMPILE_TYPE_SP) {
                // for SP
                ParseTreeWalker.DEFAULT.walk(pp, codeTree);
            } else {
                // for Packages
                if (codeTree != null) {
                    ParseTreeWalker.DEFAULT.walk(pp, codeTree);
                }
                if (bodyCodeTree != null) {
                    ParseTreeWalker.DEFAULT.walk(pp, bodyCodeTree);
                }
            }

            out.close();

            if (verbose) {
                t0 = logElapsedTime(logStore, "parse tree printing", t0);
            }
        }

        // ------------------------------------------
        // converting parse tree to AST

        Unit unit;
        UnitSp unitSp = null;
        UnitPkg unitPkg = null;
        ParseTreeConverter converter = new ParseTreeConverter(iStore, owner, revision);

        if (type == CompileRequest.PLCSQL_COMPILE_TYPE_SP) {
            unit = unitSp = (UnitSp) converter.visit(codeTree);
        } else {
            // either codeTree or bodyCodeTree can be null, but not both
            unit = unitPkg = converter.convertPackageCode(codeTree, bodyCodeTree);
        }

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

        Set<SqlUse> sqlUsesInRecursiveCalls = new HashSet<>(); // collected in TypeChecker
        TypeChecker typeChecker =
                new TypeChecker(
                        iStore,
                        converter.symbolStack,
                        converter.dependencies,
                        owner,
                        sqlUsesInRecursiveCalls);
        typeChecker.visit(unit);

        if (verbose) {
            t0 = logElapsedTime(logStore, "typechecking", t0);
        }

        // ------------------------------------------
        // Java code generation

        String javaCode;
        if (type == CompileRequest.PLCSQL_COMPILE_TYPE_SP) {
            javaCode = new JavaCodeWriter(iStore, sqlUsesInRecursiveCalls).buildCodeLines(unit);
        } else {
            // temporary code
            javaCode =
                    String.format("public class %s { public int i = 1; }", unitPkg.getClassName());
        }

        if (verbose) {
            logElapsedTime(logStore, "Java code generation", t0);
        }

        // ------------------------------------------

        if (verbose) {
            Server.log(unit.getClassName() + logStore.toString());
        }

        if (type == CompileRequest.PLCSQL_COMPILE_TYPE_SP) {

            return new CompileResponse(
                    CompileRequest.PLCSQL_COMPILE_TYPE_SP,
                    javaCode,
                    unitSp.getClassName(),
                    unitSp.getJavaSignature(),
                    unitSp.routine.sqlDataAccess,
                    typeChecker.dependencies);
        } else if (type == CompileRequest.PLCSQL_COMPILE_TYPE_PKG_SPEC) {

            CompileResponse resp =
                    new CompileResponse(
                            CompileRequest.PLCSQL_COMPILE_TYPE_PKG_SPEC,
                            javaCode,
                            unitPkg.getClassName(),
                            typeChecker.dependencies);

            assert converter.pkgSpecItems != null;
            for (Decl d : converter.pkgSpecItems.nodes) {
                d.addAsPkgItem(resp);
            }

            return resp;
        } else {
            assert false;
            return null;
        }
    }

    private static class SyntaxErrorIndicator extends BaseErrorListener {

        final boolean forParser;

        public SyntaxErrorIndicator(boolean forParser) {
            super();
            this.forParser = forParser;
        }

        @Override
        public void syntaxError(
                Recognizer<?, ?> recognizer,
                Object offendingSymbol,
                int line,
                int charPositionInLine,
                String msg,
                RecognitionException e) {

            // throw SyntaxError at the first syntax error
            String errMsg = forParser ? cutExpectingClause(msg) : msg;
            throw new SyntaxError(line, charPositionInLine + 1, errMsg);
        }
    }
}
