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

        String strDecls = "// no declarations"; // TODO: temporary

        return new CodeTemplate(
            0, 0,
            new String[] {
                "%'IMPORTS'%",
                "import static com.cubrid.plcsql.predefined.sp.SpLib.*;",
                "",
                "public class %'CLASS-NAME'% {",
                "",
                "  public static %'RETURN-TYPE'% %'METHOD-NAME'%(",
                "      %'PARAMETERS'%",
                "    ) throws Exception {",
                "",
                "    try {",
                "      Long[] sql_rowcount = new Long[] { -1L };",
                "      %'GET-CONNECTION'%",
                "",
                "      %'DECL-CLASS'%",
                "",
                "      %'BODY'%",
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
            "%'IMPORTS'%", node.getImportsArray(),
            "%'CLASS-NAME'%", node.getClassName(),
            "%'RETURN-TYPE'%", node.routine.retType == null ? "void" : visit(node.routine.retType),
            "%'METHOD-NAME'%", node.routine.name,
            "%'PARAMETERS'%", strParamArr,
            "%'GET-CONNECTION'%", strGetConn,
            "%'DECL-CLASS'%", strDecls,
            "%'BODY'%", visit(node.routine.body)
        );
    }

    @Override
    public CodeToResolve visitDeclFunc(DeclFunc node) {
        return null;
    }

    @Override
    public CodeToResolve visitDeclProc(DeclProc node) {
        return null;
    }

    @Override
    public CodeToResolve visitDeclParamIn(DeclParamIn node) {
        return null;
    }

    @Override
    public CodeToResolve visitDeclParamOut(DeclParamOut node) {
        return null;
    }

    @Override
    public CodeToResolve visitDeclVar(DeclVar node) {
        return null;
    }

    @Override
    public CodeToResolve visitDeclConst(DeclConst node) {
        return null;
    }

    @Override
    public CodeToResolve visitDeclCursor(DeclCursor node) {
        return null;
    }

    @Override
    public CodeToResolve visitDeclLabel(DeclLabel node) {
        return null;
    }

    @Override
    public CodeToResolve visitDeclException(DeclException node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprBetween(ExprBetween node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprBinaryOp(ExprBinaryOp node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprCase(ExprCase node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprCond(ExprCond node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprCursorAttr(ExprCursorAttr node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprDate(ExprDate node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprDatetime(ExprDatetime node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprFalse(ExprFalse node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprField(ExprField node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprGlobalFuncCall(ExprGlobalFuncCall node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprId(ExprId node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprIn(ExprIn node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprLike(ExprLike node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprLocalFuncCall(ExprLocalFuncCall node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprNull(ExprNull node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprUint(ExprUint node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprFloat(ExprFloat node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprSerialVal(ExprSerialVal node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprSqlRowCount(ExprSqlRowCount node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprStr(ExprStr node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprTime(ExprTime node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprTrue(ExprTrue node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprUnaryOp(ExprUnaryOp node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprTimestamp(ExprTimestamp node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprAutoParam(ExprAutoParam node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprSqlCode(ExprSqlCode node) {
        return null;
    }

    @Override
    public CodeToResolve visitExprSqlErrm(ExprSqlErrm node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtAssign(StmtAssign node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtBasicLoop(StmtBasicLoop node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtBlock(StmtBlock node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtExit(StmtExit node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtCase(StmtCase node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtCommit(StmtCommit node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtContinue(StmtContinue node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtCursorClose(StmtCursorClose node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtCursorFetch(StmtCursorFetch node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtCursorOpen(StmtCursorOpen node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtExecImme(StmtExecImme node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtStaticSql(StmtStaticSql node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtForCursorLoop(StmtForCursorLoop node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtForIterLoop(StmtForIterLoop node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtForStaticSqlLoop(StmtForStaticSqlLoop node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtForExecImmeLoop(StmtForExecImmeLoop node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtGlobalProcCall(StmtGlobalProcCall node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtIf(StmtIf node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtLocalProcCall(StmtLocalProcCall node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtNull(StmtNull node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtOpenFor(StmtOpenFor node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtRaise(StmtRaise node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtRaiseAppErr(StmtRaiseAppErr node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtReturn(StmtReturn node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtRollback(StmtRollback node) {
        return null;
    }

    @Override
    public CodeToResolve visitStmtWhileLoop(StmtWhileLoop node) {
        return null;
    }

    @Override
    public CodeToResolve visitBody(Body node) {
        return null;
    }

    @Override
    public CodeToResolve visitExHandler(ExHandler node) {
        return null;
    }

    @Override
    public CodeToResolve visitExName(ExName node) {
        return null;
    }

    @Override
    public CodeToResolve visitTypeSpecPercent(TypeSpecPercent node) {
        return null;
    }

    @Override
    public CodeToResolve visitTypeSpecSimple(TypeSpecSimple node) {
        return null;
    }

    @Override
    public CodeToResolve visitCaseExpr(CaseExpr node) {
        return null;
    }

    @Override
    public CodeToResolve visitCaseStmt(CaseStmt node) {
        return null;
    }

    @Override
    public CodeToResolve visitCondExpr(CondExpr node) {
        return null;
    }

    @Override
    public CodeToResolve visitCondStmt(CondStmt node) {
        return null;
    }

    // -----------------------------------------------------------------
    // Private
    // -----------------------------------------------------------------

    interface CodeToResolve {
        void resolve(List<String> codeLines, StringBuilder codeRangeMarkers, int indentLevel);
    }

    static class CodeFixedWord implements CodeToResolve {

        final String fixedWord;

        CodeFixedWord(String fixedWord) {
            this.fixedWord = fixedWord;
        }

        public void resolve(List<String> codeLines, StringBuilder codeRangeMarkers, int indentLevel) {
            throw new RuntimeException("unreachable");
        }
    }

    static class CodeTemplate implements CodeToResolve {

        boolean resolved;

        final String plcsqlLineColumn;
        final String[] template;
        final LinkedHashMap<String, Object> substitutions = new LinkedHashMap<>();
            // key (String) - template hole name
            // value (Object) - String, String[] or CodeToResolve to fill the hole

        CodeTemplate(int plcsqlLine, int plcsqlColumn, String[] template, Object... pairs) {

            assert(template != null);

            // used: plcsqlLineColumn, template, substitutions
            if (plcsqlLine < 0 && plcsqlColumn < 0) {
                this.plcsqlLineColumn = null;   // do not mark code range in this case
            } else {
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

        private static final String[] dummyStrArr = new String[0];

        private void resolveTemplateLine(List<String> codeLines, StringBuilder codeRangeMarkers, int indentLevel,
                String line) {

            assert(line != null);
            assert(!line.endsWith(" ")) : "a template line has a trailing space: '" + line + "'";

            String indent = Misc.getIndent(indentLevel);

            Set<String> holes = new HashSet<>();
            getHoles(holes, line);
            if (holes.size() == 1 && holes.contains(line.trim())) {
                // case 1: expanded to multiple lines
                String hole = line.trim();
                int indentLevelDelta = line.indexOf(hole) / Misc.INDENT_SIZE;

                Object substitute = substitutions.get(hole);
                if (substitute instanceof String[]) {
                    indent = indent + Misc.getIndent(indentLevelDelta);
                    for (String l: (String[]) substitute) {
                        assert(l.indexOf("%'") == -1);  // no holes in the substitutes
                        codeLines.add(indent + l);
                    }
                } else if (substitute instanceof CodeToResolve) {
                    ((CodeToResolve) substitute).resolve(codeLines, codeRangeMarkers, indentLevel + indentLevelDelta);
                } else {
                    assert(false);  // cannot be a single String
                }
            } else {
                // case 2: word replacements in a single line
                for (String hole: substitutions.keySet()) {
                    if (holes.contains(hole)) {
                        Object substitute = substitutions.get(hole);
                        if (substitute instanceof String) {
                            line = line.replace(hole, (String) substitute);
                        } else {
                            assert(false);  // cannot be a String[] or CodeToResolve
                        }
                    }
                }
                assert(line.indexOf("%'") == -1);  // no holes in the substitutes
                codeLines.add(indent + line);
            }
        }

        private void getHoles(Set<String> holes, String line) {

            int i = 0;
            int len = line.length();
            while (i < len) {
                int begin = line.indexOf("%'", i);
                if (begin == -1) {
                    return;
                } else {
                    int end = line.indexOf("'%", begin + 2);
                    assert(end != -1);
                    i = end + 2;
                    holes.add(line.substring(begin, i));
                }
            }
        }
    }

    static class CodeList implements CodeToResolve {

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
}

