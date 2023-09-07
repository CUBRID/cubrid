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

package com.cubrid.plcsql.compiler.visitor;

import com.cubrid.plcsql.compiler.ast.*;
import com.cubrid.plcsql.compiler.Misc;

import java.util.Set;
import java.util.HashSet;
import java.util.List;
import java.util.ArrayList;
import java.util.LinkedList;
import java.util.LinkedHashMap;

public class JavaCodeWriter extends AstVisitor<JavaCodeWriter.CodeToResolve> {

    public List<String> codeLines = new ArrayList<>();
    public StringBuilder codeRangeMarkers = new StringBuilder();

    public void buildCodeLines(Unit unit) {
        CodeToResolve ttr = visit(unit);
        ttr.resolve(codeLines, codeRangeMarkers, 0);
        for (String s: codeLines) {
            assert(s.indexOf("\n") == -1) : "not a single line: " + s;
        }

        System.out.println(String.format("[temp] code range markers = [%s]", codeRangeMarkers.toString()));
        System.out.println(String.format("[temp] code\n%s", String.join("\n", codeLines.toArray(dummyStrArr))));
    }

    @Override
    public <E extends AstNode> CodeToResolve visitNodeList(NodeList<E> nodeList) {
        CodeList list = new CodeList();
        for (E e : nodeList.nodes) {
            list.addElement((CodeTemplate) visit(e));
        }
        return list;
    }

    @Override
    public CodeToResolve visitUnit(Unit node) {

        Object strParamArr = Misc.isEmpty(node.routine.paramList) ?
            new String[] { "// no parameters" } :
            visit(node.routine.paramList);

        String strGetConn = node.connectionRequired ?
            String.format("Connection conn = DriverManager.getConnection" +
                "(\"jdbc:default:connection::?autonomous_transaction=%s\");", node.autonomousTransaction) :
            "// connection not required";

        String[] strDecls = new String[] { "// no declarations" }; // TODO: temporary

        return new CodeTemplate(
            new int[] { 0, 0 },
            new String[] {
                "%'%IMPORTS'%",
                "import static com.cubrid.plcsql.predefined.sp.SpLib.*;",
                "",
                "public class %'CLASS-NAME'% {",
                "",
                "  public static %'RETURN-TYPE'% %'METHOD-NAME'%(",
                "      %'%PARAMETERS'%",
                "    ) throws Exception {",
                "",
                "    try {",
                "      Long[] sql_rowcount = new Long[] { -1L };",
                "      %'GET-CONNECTION'%",
                "",
                "      %'%DECL-CLASS'%",
                "",
                "      %'%BODY'%",
                "    } catch (OutOfMemoryError e) {",
                "      Server.log(e);",
                "      throw new STORAGE_ERROR();",
                "    } catch (PlcsqlRuntimeError e) {",
                "      throw e;",
                "    } catch (Throwable e) {",
                "      Server.log(e);",
                "      throw new PROGRAM_ERROR();",
                "    }",
                "  }",
                "}"
            },
            "%'%IMPORTS'%", node.getImportsArray(),
            "%'CLASS-NAME'%", node.getClassName(),
            "%'RETURN-TYPE'%", node.routine.retType == null ? "void" : visit(node.routine.retType),
            "%'METHOD-NAME'%", node.routine.name,
            "%'%PARAMETERS'%", strParamArr,
            "%'GET-CONNECTION'%", strGetConn,
            "%'%DECL-CLASS'%", strDecls,
            "%'%BODY'%", visit(node.routine.body)
        );
    }

    @Override
    public CodeToResolve visitDeclFunc(DeclFunc node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitDeclProc(DeclProc node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitDeclParamIn(DeclParamIn node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitDeclParamOut(DeclParamOut node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitDeclVar(DeclVar node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitDeclConst(DeclConst node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitDeclCursor(DeclCursor node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitDeclLabel(DeclLabel node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitDeclException(DeclException node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprBetween(ExprBetween node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprBinaryOp(ExprBinaryOp node) {
        return new CodeTemplate(Misc.getLineColumnOf(node.ctx),
            new String[] {
                "op%'OPERATION'%(",
                "  %'%LEFT-OPERAND'%,",
                "  %'%RIGHT-OPERAND'%",
                ")"
            },
            "%'OPERATION'%", node.opStr,
            "%'%LEFT-OPERAND'%", visit(node.left),
            "%'%RIGHT-OPERAND'%", visit(node.right)
        );
    }

    @Override
    public CodeToResolve visitExprCase(ExprCase node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprCond(ExprCond node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprCursorAttr(ExprCursorAttr node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprDate(ExprDate node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprDatetime(ExprDatetime node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprFalse(ExprFalse node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprField(ExprField node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprGlobalFuncCall(ExprGlobalFuncCall node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprId(ExprId node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprIn(ExprIn node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprLike(ExprLike node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprLocalFuncCall(ExprLocalFuncCall node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprNull(ExprNull node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprUint(ExprUint node) {
        return new CodeTemplate(Misc.getLineColumnOf(node.ctx), new String[] { node.toJavaCode() }); // TODO: temporary
    }

    @Override
    public CodeToResolve visitExprFloat(ExprFloat node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprSerialVal(ExprSerialVal node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprSqlRowCount(ExprSqlRowCount node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprStr(ExprStr node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprTime(ExprTime node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprTrue(ExprTrue node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprUnaryOp(ExprUnaryOp node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprTimestamp(ExprTimestamp node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprAutoParam(ExprAutoParam node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprSqlCode(ExprSqlCode node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExprSqlErrm(ExprSqlErrm node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtAssign(StmtAssign node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtBasicLoop(StmtBasicLoop node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtBlock(StmtBlock node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtExit(StmtExit node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtCase(StmtCase node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtCommit(StmtCommit node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtContinue(StmtContinue node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtCursorClose(StmtCursorClose node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtCursorFetch(StmtCursorFetch node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtCursorOpen(StmtCursorOpen node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtExecImme(StmtExecImme node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtStaticSql(StmtStaticSql node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtForCursorLoop(StmtForCursorLoop node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtForIterLoop(StmtForIterLoop node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtForStaticSqlLoop(StmtForStaticSqlLoop node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtForExecImmeLoop(StmtForExecImmeLoop node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtGlobalProcCall(StmtGlobalProcCall node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtIf(StmtIf node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtLocalProcCall(StmtLocalProcCall node) {

        String block = node.prefixDeclBlock ? node.decl.scope().block + "." : "";

        return Misc.isEmpty(node.args) ?
            new CodeTemplate(Misc.getLineColumnOf(node.ctx),
                new String[] {
                    "%'BLOCK'%%'NAME'%();",
                },
                "%'BLOCK'%", block,
                "%'NAME'%", node.name
            ) :
            new CodeTemplate(Misc.getLineColumnOf(node.ctx),
                new String[] {
                    "%'BLOCK'%%'NAME'%(",
                    "  %'%ARGUMENTS'%",
                    ");"
                },
                "%'BLOCK'%", block,
                "%'NAME'%", node.name,
                "%'%ARGUMENTS'%", visit(node.args)
            );

    }

    @Override
    public CodeToResolve visitStmtNull(StmtNull node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtOpenFor(StmtOpenFor node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtRaise(StmtRaise node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtRaiseAppErr(StmtRaiseAppErr node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtReturn(StmtReturn node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtRollback(StmtRollback node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitStmtWhileLoop(StmtWhileLoop node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitBody(Body node) {
        return new CodeTemplate(POSITION_IGNORED,
            new String[] {
                "try {",
                "  %'%STATEMENTS'%",
                "}",
                "%'%CATCHES'%"
            },
            "%'%STATEMENTS'%", visit(node.stmts),
            "%'%CATCHES'%", visit(node.exHandlers)
        );
    }

    @Override
    public CodeToResolve visitExHandler(ExHandler node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitExName(ExName node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitTypeSpecPercent(TypeSpecPercent node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitTypeSpecSimple(TypeSpecSimple node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitCaseExpr(CaseExpr node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitCaseStmt(CaseStmt node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitCondExpr(CondExpr node) {
        throw new RuntimeException("unimplemented yet");
    }

    @Override
    public CodeToResolve visitCondStmt(CondStmt node) {
        throw new RuntimeException("unimplemented yet");
    }

    interface CodeToResolve {
        void resolve(List<String> codeLines, StringBuilder codeRangeMarkers, int indentLevel);
    }

    // -----------------------------------------------------------------
    // Private
    // -----------------------------------------------------------------

    private static final int[] POSITION_IGNORED = new int[] { -1, -1 };
    private static final String[] dummyStrArr = new String[0];

    private static class CodeFixedWord implements CodeToResolve {

        final String fixedWord;

        CodeFixedWord(String fixedWord) {
            this.fixedWord = fixedWord;
        }

        public void resolve(List<String> codeLines, StringBuilder codeRangeMarkers, int indentLevel) {
            throw new RuntimeException("unreachable");
        }
    }

    private static class CodeList implements CodeToResolve {

        boolean resolved;
        final List<CodeTemplate> elements;

        CodeList() {
            elements = new ArrayList<>();
        }

        void addElement(CodeTemplate element) {
            elements.add(element);
        }

        public void resolve(List<String> codeLines, StringBuilder codeRangeMarkers, int indentLevel) {

            if (resolved) {
                throw new RuntimeException("already resolved");
            } else {

                for (CodeTemplate t: elements) {
                    t.resolve(codeLines, codeRangeMarkers, indentLevel);
                }

                resolved = true;
            }
        }
    }

    private static class CodeTemplate implements CodeToResolve {

        boolean resolved;

        final String plcsqlLineColumn;
        final String[] template;
        final LinkedHashMap<String, Object> substitutions = new LinkedHashMap<>();
            // key (String) - template hole name
            // value (Object) - String, String[] or CodeToResolve to fill the hole

        CodeTemplate(int[] pos, String[] template, Object... pairs) {

            assert(pos != null);
            assert(template != null);

            int plcsqlLine = pos[0];
            int plcsqlColumn = pos[1];

            // used: plcsqlLineColumn, template, substitutions
            if (plcsqlLine < 0 && plcsqlColumn < 0) {
                this.plcsqlLineColumn = null;   // do not mark code range in this case
            } else {
                assert(plcsqlLine >= 0 && plcsqlColumn >= 0);
                this.plcsqlLineColumn = String.format("%d,%d", plcsqlLine, plcsqlColumn);
            }
            this.template = template;

            int len = pairs.length;
            assert(len % 2 == 0);
            for (int i = 0; i < len; i += 2) {
                assert(pairs[i] instanceof String);
                Object thing = pairs[i + 1];
                if (thing instanceof CodeFixedWord) {
                    this.substitutions.put((String) pairs[i], ((CodeFixedWord) thing).fixedWord);
                } else if (thing instanceof String || thing instanceof String[] || thing instanceof CodeToResolve) {
                    this.substitutions.put((String) pairs[i], thing);
                } else {
                    assert(false);
                }
            }
        }

        public void resolve(List<String> codeLines, StringBuilder codeRangeMarkers, int indentLevel) {

            if (resolved) {
                throw new RuntimeException("already resolved");
            } else {

                boolean markCodeRange = plcsqlLineColumn != null;
                if (markCodeRange) {
                    codeRangeMarkers.append(String.format(" (%d,%s", codeLines.size() + 1, plcsqlLineColumn));
                }

                for (String line: template) {
                    resolveTemplateLine(codeLines, codeRangeMarkers, indentLevel, line);
                }

                if (markCodeRange) {
                    codeRangeMarkers.append(String.format(" )%d", codeLines.size() + 1));
                }

                resolved = true;
            }
        }

        // -----------------------------------------------
        // Private
        // -----------------------------------------------

        private void resolveTemplateLine(List<String> codeLines, StringBuilder codeRangeMarkers, int indentLevel,
                String line) {

            assert(line != null);
            assert(!line.endsWith(" ")) : "a template line has a trailing space: '" + line + "'";

            String indent = Misc.getIndent(indentLevel);

            Set<String> holes = new HashSet<>();
            String bigHole = getHoles(holes, line);
            if (bigHole == null) {
                // case 1 (namely, small hole) : word replacements in a single line
                for (String hole: substitutions.keySet()) {
                    if (holes.contains(hole)) {
                        assert(!isBigHole(hole)) : "wrong big hole " + hole;
                        Object substitute = substitutions.get(hole);
                        if (substitute instanceof String) {
                            line = line.replace(hole, (String) substitute);
                        } else {
                            assert(false) : "wrong substitution for " + hole; // cannot be a String[] or CodeToResolve
                        }
                    }
                }
                assert(line.indexOf("%'") == -1);  // no holes in the substitutes
                codeLines.add(indent + line);
            } else {
                // case 2 (namely, big hole) : expanded to multiple lines
                int spaces = line.indexOf(bigHole);
                int indentLevelDelta = spaces / Misc.INDENT_SIZE;
                String suffix = line.substring(spaces + bigHole.length());

                Object substitute = substitutions.get(bigHole);
                if (substitute instanceof String[]) {
                    indent = indent + Misc.getIndent(indentLevelDelta);
                    for (String l: (String[]) substitute) {
                        assert(l.indexOf("%'") == -1);  // no holes in the substitutes
                        codeLines.add(indent + l);
                    }
                } else if (substitute instanceof CodeToResolve) {
                    ((CodeToResolve) substitute).resolve(codeLines, codeRangeMarkers, indentLevel + indentLevelDelta);
                } else {
                    assert(false) : "wrong substitution for " + bigHole;  // cannot be a single String
                }

                // append the suffix, if any, to the last line (this is mainly for the commas after expressions)
                if (suffix.length() > 0) {
                    int lastLineIndex = codeLines.size() - 1;
                    String lastLine = codeLines.get(lastLineIndex);
                    codeLines.set(lastLineIndex, lastLine + suffix);
                }
            }
        }

        private boolean isBigHole(String hole) {
            return hole.startsWith("%'%");
        }

        //
        private String getHoles(Set<String> holes, String line) {

            int i = 0;
            int len = line.length();
            boolean first = true;
            while (i < len) {
                int begin = line.indexOf("%'", i);
                if (begin == -1) {
                    return null;
                } else {
                    int end = line.indexOf("'%", begin + 2);
                    assert(end != -1);
                    i = end + 2;

                    String hole = line.substring(begin, i);
                    if (first) {
                        first = false;
                        if (isBigHole(hole)) {
                            for (int j = 0; j < begin; j++) {
                                assert(line.charAt(j) == ' ');  // only spaces allowed before a big hole
                            }
                            assert(line.indexOf("%'", i) == -1); // ho more holes after a big hole

                            return hole;
                        }
                    }
                    holes.add(hole);
                }
            }

            return null;
        }
    }
}

