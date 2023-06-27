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

public class JavaCodeWriter extends AstVisitor<String[]> {

    public List<String> codeLines = new ArrayList<>();
    public StringBuilder codeRangeMarkers = new StringBuilder();

    public void buildCodeLines(Unit unit) {
        TextToResolve ttr = visit(unit);
        ttr.resolve(codeLines, codeRangeMarkers);
        for (String s: codeLines) {
            assert(s.indexOf("\n") == -1) : "not a single line: " + s;
        }

        System.out.println(String.format("[temp] code range markers = [%s]", codeRangeMarkers.toString()));
    }

    @Override
    public <E extends AstNode> TextToResolve visitNodeList(NodeList<E> nodeList) {
        LinkedList<String> list = new LinkedList<>();
        for (E e : nodeList.nodes) {
            String[] r = visit(e);
            list.addAll(r);
        }
        return list.toArray(dummyStrArr);
    }

    @Override
    public TextToResolve visitUnit(Unit node) {

        String[] strParamArr = Misc.isEmpty(node.routine.paramList) ?
            new String[] { "// no parameters" } :
            visit(node.routine.paramList);

        String strGetConn = node.connectionRequired ?
            String.format("Connection conn = DriverManager.getConnection" +
                "(\"jdbc:default:connection::?autonomous_transaction=%s\");", node.autonomousTransaction) :
            "// connection not required";

        String strDecls = "// no declarations"; // TODO: temporary

        return new TextToResolve(
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
        ).resolved;
    }

    @Override
    public TextToResolve visitDeclFunc(DeclFunc node) {
        return null;
    }

    @Override
    public TextToResolve visitDeclProc(DeclProc node) {
        return null;
    }

    @Override
    public TextToResolve visitDeclParamIn(DeclParamIn node) {
        return null;
    }

    @Override
    public TextToResolve visitDeclParamOut(DeclParamOut node) {
        return null;
    }

    @Override
    public TextToResolve visitDeclVar(DeclVar node) {
        return null;
    }

    @Override
    public TextToResolve visitDeclConst(DeclConst node) {
        return null;
    }

    @Override
    public TextToResolve visitDeclCursor(DeclCursor node) {
        return null;
    }

    @Override
    public TextToResolve visitDeclLabel(DeclLabel node) {
        return null;
    }

    @Override
    public TextToResolve visitDeclException(DeclException node) {
        return null;
    }

    @Override
    public TextToResolve visitExprBetween(ExprBetween node) {
        return null;
    }

    @Override
    public TextToResolve visitExprBinaryOp(ExprBinaryOp node) {
        return null;
    }

    @Override
    public TextToResolve visitExprCase(ExprCase node) {
        return null;
    }

    @Override
    public TextToResolve visitExprCond(ExprCond node) {
        return null;
    }

    @Override
    public TextToResolve visitExprCursorAttr(ExprCursorAttr node) {
        return null;
    }

    @Override
    public TextToResolve visitExprDate(ExprDate node) {
        return null;
    }

    @Override
    public TextToResolve visitExprDatetime(ExprDatetime node) {
        return null;
    }

    @Override
    public TextToResolve visitExprFalse(ExprFalse node) {
        return null;
    }

    @Override
    public TextToResolve visitExprField(ExprField node) {
        return null;
    }

    @Override
    public TextToResolve visitExprGlobalFuncCall(ExprGlobalFuncCall node) {
        return null;
    }

    @Override
    public TextToResolve visitExprId(ExprId node) {
        return null;
    }

    @Override
    public TextToResolve visitExprIn(ExprIn node) {
        return null;
    }

    @Override
    public TextToResolve visitExprLike(ExprLike node) {
        return null;
    }

    @Override
    public TextToResolve visitExprLocalFuncCall(ExprLocalFuncCall node) {
        return null;
    }

    @Override
    public TextToResolve visitExprNull(ExprNull node) {
        return null;
    }

    @Override
    public TextToResolve visitExprUint(ExprUint node) {
        return null;
    }

    @Override
    public TextToResolve visitExprFloat(ExprFloat node) {
        return null;
    }

    @Override
    public TextToResolve visitExprSerialVal(ExprSerialVal node) {
        return null;
    }

    @Override
    public TextToResolve visitExprSqlRowCount(ExprSqlRowCount node) {
        return null;
    }

    @Override
    public TextToResolve visitExprStr(ExprStr node) {
        return null;
    }

    @Override
    public TextToResolve visitExprTime(ExprTime node) {
        return null;
    }

    @Override
    public TextToResolve visitExprTrue(ExprTrue node) {
        return null;
    }

    @Override
    public TextToResolve visitExprUnaryOp(ExprUnaryOp node) {
        return null;
    }

    @Override
    public TextToResolve visitExprTimestamp(ExprTimestamp node) {
        return null;
    }

    @Override
    public TextToResolve visitExprAutoParam(ExprAutoParam node) {
        return null;
    }

    @Override
    public TextToResolve visitExprSqlCode(ExprSqlCode node) {
        return null;
    }

    @Override
    public TextToResolve visitExprSqlErrm(ExprSqlErrm node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtAssign(StmtAssign node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtBasicLoop(StmtBasicLoop node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtBlock(StmtBlock node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtExit(StmtExit node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtCase(StmtCase node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtCommit(StmtCommit node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtContinue(StmtContinue node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtCursorClose(StmtCursorClose node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtCursorFetch(StmtCursorFetch node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtCursorOpen(StmtCursorOpen node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtExecImme(StmtExecImme node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtStaticSql(StmtStaticSql node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtForCursorLoop(StmtForCursorLoop node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtForIterLoop(StmtForIterLoop node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtForStaticSqlLoop(StmtForStaticSqlLoop node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtForExecImmeLoop(StmtForExecImmeLoop node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtGlobalProcCall(StmtGlobalProcCall node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtIf(StmtIf node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtLocalProcCall(StmtLocalProcCall node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtNull(StmtNull node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtOpenFor(StmtOpenFor node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtRaise(StmtRaise node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtRaiseAppErr(StmtRaiseAppErr node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtReturn(StmtReturn node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtRollback(StmtRollback node) {
        return null;
    }

    @Override
    public TextToResolve visitStmtWhileLoop(StmtWhileLoop node) {
        return null;
    }

    @Override
    public TextToResolve visitBody(Body node) {
        return null;
    }

    @Override
    public TextToResolve visitExHandler(ExHandler node) {
        return null;
    }

    @Override
    public TextToResolve visitExName(ExName node) {
        return null;
    }

    @Override
    public TextToResolve visitTypeSpecPercent(TypeSpecPercent node) {
        return null;
    }

    @Override
    public TextToResolve visitTypeSpecSimple(TypeSpecSimple node) {
        return null;
    }

    @Override
    public TextToResolve visitCaseExpr(CaseExpr node) {
        return null;
    }

    @Override
    public TextToResolve visitCaseStmt(CaseStmt node) {
        return null;
    }

    @Override
    public TextToResolve visitCondExpr(CondExpr node) {
        return null;
    }

    @Override
    public TextToResolve visitCondStmt(CondStmt node) {
        return null;
    }

    // -----------------------------------------------------------------
    // Private
    // -----------------------------------------------------------------

    static class TextToResolve {

        String[] resolved;

        final String plcsqlLineColumn;
        final String[] template;
        final LinkedHashMap<String, String[]> substitutions = new LinkedHashMap<>();
            // (template hole name, String, String[] or TextToResolve to fill the hole)

        final List<TextToResolve> subtexts = new ArrayList<>();

        TextToResolve(int plcsqlLine, int plcsqlColumn, String[] template, Object... pairs) {

            assert(template != null);

            // unused: subtexts
            subtexts = null;

            // used: plcsqlLineColumn, template, substitutions
            if (plcsqlLine < 0 || plcsqlColumn < 0) {
                this.plcsqlLineColumn = null;
            } else {
                this.plcsqlLineColumn = String.format("%d,%d", plcsqlLine, plcsqlColumn);
            }
            this.template = template;

            int len = pairs.length;
            assert(len % 2 == 0);
            for (int i = 0; i < len; i += 2) {
                assert(pairs[i] instanceof String);
                Object thing = pairs[i + 1];
                if (thing instanceof String) {
                    this.substitutions.put((String) pairs[i], new String[] { (String) thing });
                } else if (thing instanceof String[]) {
                    this.substitutions.put((String) pairs[i], (String[]) thing);
                } else if (thing instanceof TextToResolve) {
                    this.substitutions.put((String) pairs[i], ((TextToResolve) thing).resolved);
                } else {
                    assert(false);
                }
            }
        }

        TextToResolve() {

            // unused: template, substitutions
            this.plcsqlLineColumn = null;
            this.template = null;
            this.substitutions = null;

            // used: plcsqlLineColumn, subtexts
            subtexts = new ArrayList<>();
        }

        void addSubtexts(TextToResolve subtext) {
            subtexts.add(subtext);
        }

        String[] resolve(List<String> codeLines, StringBuilder codeRangeMarkers) {

            if (resolved == null) {

                boolean markCodeRange = plcsqlLineColumn != null;
                if (markCodeRange) {
                    codeRangeMarkers.append(String.format(" (%d,%s", codeLines.size() + 1, plcsqlLineColumn));
                }

                if (template == null) {
                    for (TextToResolve t: subtexts) {
                        t.resolve(codeLines, codeRangeMarkers);
                    }
                } else {
                    for (String line: template) {
                        List<String> expanded = expandTemplateLine(line);
                        codeLines.addAll(expanded);
                    }
                }

                if (markCodeRange) {
                    codeRangeMarkers.append(String.format(" )%d", codeLines.size() + 1);
                }

                resolved = accum.toArray(dummy);
            }

            return resolved;
        }


        // -----------------------------------------------
        // Private
        // -----------------------------------------------

        private static final String[] dummy = new String[0];

        private List<String> expandTemplateLine(String line) {

            assert(line != null);

            LinkedList<String> list1 = new LinkedList<>();
            LinkedList<String> list2 = new LinkedList<>();
            Set<String> holes = new HashSet<>();

            list1.add(line);

            while (true) {

                boolean changed = false;
                while (!list1.isEmpty()) {

                    String l = list1.removeFirst();

                    holes.clear();  // to reuse a set rather than creating temporary ones
                    getHoles(holes, l);
                    if (holes.isEmpty()) {
                        list2.addLast(l);
                    } else {

                        if (holes.size() == 1 && holes.contains(l.trim())) {
                            // this l is for indented whole replacement

                            assert(!l.endsWith(" "));    // do not append a trailing space to a template line
                            int indents = l.indexOf("%'");
                            assert(indents % Misc.INDENT.length() == 0);
                            int indentLevel = indents / Misc.INDENT.length();

                            addNewLines(list2, indentLevel, substitutions.get(l.trim()));
                            changed = true;

                        } else {

                            for (String hole: substitutions.keySet()) {
                                if (holes.contains(hole)) {
                                    String[] newLines = substitutions.get(hole);
                                    assert(newLines.length == 1);    // a single string
                                    l = l.replace(hole, newLines[0]);
                                    changed = true;
                                }
                            }
                            assert(l.indexOf("%'") == -1);  // no remaining holes
                            list2.addLast(l);
                        }
                    }
                }

                if (changed) {
                    // swap list1 and list2
                    LinkedList<String> store = list1;
                    list1 = list2;
                    list2 = store;
                } else {
                    return list2;
                }
            }
        }

        private void addNewLines(LinkedList<String> list, int indentLevel, String[] newLines) {

            String indent = Misc.getIndent(indentLevel);

            for (String s: newLines) {
                list.addLast(indent + s);
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
}

