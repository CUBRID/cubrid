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
        CodeToResolve ctr = visitUnit(unit);
        ctr.resolve(0, codeLines, codeRangeMarkers);
        for (String s: codeLines) {
            if (s.indexOf("\n") != -1) {
                throw new RuntimeException("each code line must be a single line: '" + s + "'");
            }
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
            "Unit",
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
        return new CodeTemplate("ExprBinaryOp", Misc.getLineColumnOf(node.ctx),
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
        return new CodeTemplate("ExprUnit", Misc.getLineColumnOf(node.ctx), new String[] { node.toJavaCode() }); // TODO: temporary
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
            new CodeTemplate("StmtLocalProcCall", Misc.getLineColumnOf(node.ctx),
                
                new String[] {
                    "%'BLOCK'%%'NAME'%();",
                },
                "%'BLOCK'%", block,
                "%'NAME'%", node.name
            ) :
            new CodeTemplate("StmtLocalProcCall", Misc.getLineColumnOf(node.ctx),
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
        return new CodeTemplate("Body", POSITION_IGNORED,
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
        void resolve(int indentLevel, List<String> codeLines, StringBuilder codeRangeMarkers);
    }

    // -----------------------------------------------------------------
    // Private
    // -----------------------------------------------------------------

    private static final int[] POSITION_IGNORED = new int[] { -1, -1 };
    private static final String[] dummyStrArr = new String[0];

    private static class CodeList implements CodeToResolve {

        boolean resolved;
        final List<CodeTemplate> elements;

        CodeList() {
            elements = new ArrayList<>();
        }

        void addElement(CodeTemplate element) {     // NOTE: CodeTemplate, not CodeToResolve
            elements.add(element);
        }

        public void resolve(int indentLevel, List<String> codeLines, StringBuilder codeRangeMarkers) {

            if (resolved) {
                throw new RuntimeException("already resolved");
            } else {

                for (CodeTemplate t: elements) {
                    t.resolve(indentLevel, codeLines, codeRangeMarkers);
                }

                resolved = true;
            }
        }
    }

    private static class CodeTemplate implements CodeToResolve {

        boolean resolved;

        final String astNode;
        final String plcsqlLineColumn;
        final String[] template;
        final LinkedHashMap<String, Object> substitutions = new LinkedHashMap<>();
            // key (String) - template hole name
            // value (Object) - String, String[] or CodeToResolve to fill the hole

        CodeTemplate(String astNode, int[] pos, String[] template, Object... pairs) {

            assert pos != null && pos.length == 2;
            assert template != null;

            this.astNode = astNode;

            int plcsqlLine = pos[0];
            int plcsqlColumn = pos[1];

            // used: plcsqlLineColumn, template, substitutions
            if (plcsqlLine < 0 && plcsqlColumn < 0) {
                this.plcsqlLineColumn = null;   // do not mark code range in this case
            } else {
                if (plcsqlLine <= 0 || plcsqlColumn <= 0) {
                    throw new RuntimeException(String.format(
                        "%s - line and column numbers of code templates must be positive integers: (%d, %d)",
                        astNode, plcsqlLine, plcsqlColumn));
                }
                this.plcsqlLineColumn = String.format("%d,%d", plcsqlLine, plcsqlColumn);
            }
            this.template = template;

            int len = pairs.length;
            if (len % 2 != 0) {
                throw new RuntimeException(astNode + " - the number of substitution pairs elements must be an even number");
            }
            for (int i = 0; i < len; i += 2) {
                if (!(pairs[i] instanceof String)) {
                    throw new RuntimeException(astNode + " - first element of each pair must be a String: " + pairs[i]);
                }
                Object thing = pairs[i + 1];
                if (thing instanceof String || thing instanceof String[] || thing instanceof CodeToResolve) {
                    // String is for a small hole, and the String[] and CodeToResolve are for a big hole
                    this.substitutions.put((String) pairs[i], thing);
                } else {
                    throw new RuntimeException("invalid type of a substitute " + thing);
                }
            }
        }

        public void resolve(int indentLevel, List<String> codeLines, StringBuilder codeRangeMarkers) {

            if (resolved) {
                throw new RuntimeException(astNode + " - already resolved");
            } else {

                boolean markCodeRange = plcsqlLineColumn != null;
                if (markCodeRange) {
                    codeRangeMarkers.append(String.format(" (%d,%s", codeLines.size() + 1, plcsqlLineColumn));
                }

                for (String line: template) {
                    resolveTemplateLine(line, indentLevel, codeLines, codeRangeMarkers);
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

        private void resolveTemplateLine(String line, int indentLevel, List<String> codeLines, StringBuilder codeRangeMarkers) {

            assert line != null;

            if (line.endsWith(" ")) {
                throw new RuntimeException("a template line may not have a trailing space: '" + line + "'");
            }

            String indent = Misc.getIndent(indentLevel);

            Set<String> smallHoles = new HashSet<>();
            String bigHole = getHoles(smallHoles, line);
            if (bigHole == null) {

                // case 1: word replacements in a single line (namely, small holes)
                for (String hole: substitutions.keySet()) {
                    if (smallHoles.contains(hole)) {
                        if (isBigHole(hole)) {
                            throw new RuntimeException("a big hole in the small holes set: '" + hole + "'");
                        }
                        Object substitute = substitutions.get(hole);
                        if (substitute instanceof String) {
                            line = line.replace(hole, (String) substitute);
                        } else {
                            throw new RuntimeException("substitute for a small hole '" + hole + "' is not a String: '" + substitute + "'");
                        }
                    }
                }
                if (line.indexOf("%'") != -1) {
                    throw new RuntimeException("no holes can remain after completing substitutions: '" + line + "'");
                }
                codeLines.add(indent + line);
            } else {
                assert smallHoles.size() == 0;

                // case 2: expanded to multiple lines (namely, single big hole)
                int spaces = line.indexOf(bigHole);
                int indentLevelDelta = spaces / Misc.INDENT_SIZE;
                String remainder = line.substring(spaces + bigHole.length());

                Object substitute = substitutions.get(bigHole);
                if (substitute instanceof String[]) {
                    indent = indent + Misc.getIndent(indentLevelDelta);
                    for (String l: (String[]) substitute) {
                        if (l.indexOf("%'") != -1) {
                            throw new RuntimeException("a line in a string array substitute may not have a hole: '" + l + "'");
                        }
                        codeLines.add(indent + l);
                    }
                } else if (substitute instanceof CodeToResolve) {
                    ((CodeToResolve) substitute).resolve(indentLevel + indentLevelDelta, codeLines, codeRangeMarkers);
                } else {
                    throw new RuntimeException("substitute for a big hole '" + bigHole +
                        "' is neither a String nor a CodeToResolve: '" + substitute + "'");
                }

                // Append the remainder, if any, to the last line.
                // This is mainly for the commas after expressions.
                if (remainder.length() > 0) {
                    int lastLineIndex = codeLines.size() - 1;
                    String lastLine = codeLines.get(lastLineIndex);
                    codeLines.set(lastLineIndex, lastLine + remainder);
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
                    if (end == -1) {
                       throw new RuntimeException("not closed hole in a line '" + line + "'");
                    }
                    i = end + 2;

                    String hole = line.substring(begin, i);
                    if (first) {
                        first = false;
                        if (isBigHole(hole)) {
                            for (int j = 0; j < begin; j++) {
                                if (line.charAt(j) != ' ') {
                                   throw new RuntimeException("only spaces allowed before a big hole: '" + line + "'");
                                }
                            }
                            if (line.indexOf("%'", i) != -1) {
                                throw new RuntimeException("no more holes after a big hole: '" + line + "'");
                            }

                            return hole;
                        }
                    } else {
                        if (isBigHole(hole)) {
                            throw new RuntimeException("big holes must be the only hole in the line: '" + hole + "'");
                        }

                    }

                    holes.add(hole);
                }
            }

            return null;
        }
    }
}

