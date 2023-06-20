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

import java.util.List;
import java.util.ArrayList;

public class JavaCodeWriter extends AstVisitor<Object> {

    public List<String> codeLines = new ArrayList<>();
    public List<String> codeRangeMarkers = new ArrayList<>();

    // JavaCodeWriter's every visit* always returns null
    // Its result is updates in codeLines and codeRangeMarkers

    public Object visit(AstNode node) {

        if (node.ctx != null) {
            int[] originalLineCol = Misc.LineColumnOf(node.ctx);
            codeRangeMarkers.add(String.format("(%d,%d,%d", codeLines.size() + 1, originalLineCol[0], originalLineCol[1]));
        }

        node.accept(this);

        if (node.ctx != null) {
            codeRangeMarkers.add(String.format(")%d", codeLines.size() + 1));
        }

        return null;
    }

    public Object visitUnit(Unit node) {

        String returnType = node.routine.retType == null ? "void" : node.routine.retType.toJavaCode();
        String strParams = routine.paramList == null ? "// no parameters" : routine.paramList.toJavaCode(",\n");

        // add import lines
        for (String s: node.importsStr.split("\n")) { // TODO: keep importsStr as a Set<String> rahter than a single string
            addSingleLine(s);
        }
        addLines(
            "import static com.cubrid.plcsql.predefined.sp.SpLib.*;",
            "",
            "public class %'CLASS-NAME'% {"
                .replace("%'CLASS-NAME'%", node.getClassName()),
            "",
            "  public static %'RETURN-TYPE'% %'METHOD-NAME'%("
                .replace("%'RETURN-TYPE'%", returnType)
                .replace("%'METHOD-NAME'%", routine.name),
            "      %'PARAMETERS'%",
                .replace("      %'PARAMETERS'%", Misc.indentLines(strParams, 3))
            "    ) throws Exception {",
            "",
            "    try {",
            "      Long[] sql_rowcount = new Long[] { -1L };",
            "      %'GET-CONNECTION'%",
                .replace("      %'GET-CONNECTION'%", Misc.indentLines(strGetConn, 3))
            "",

        );

        return null;
    }

    public Object visitDeclFunc(DeclFunc node) {
        return null;
    }

    public Object visitDeclProc(DeclProc node) {
        return null;
    }

    public Object visitDeclParamIn(DeclParamIn node) {
        return null;
    }

    public Object visitDeclParamOut(DeclParamOut node) {
        return null;
    }

    public Object visitDeclVar(DeclVar node) {
        return null;
    }

    public Object visitDeclConst(DeclConst node) {
        return null;
    }

    public Object visitDeclCursor(DeclCursor node) {
        return null;
    }

    public Object visitDeclLabel(DeclLabel node) {
        return null;
    }

    public Object visitDeclException(DeclException node) {
        return null;
    }

    public Object visitExprBetween(ExprBetween node) {
        return null;
    }

    public Object visitExprBinaryOp(ExprBinaryOp node) {
        return null;
    }

    public Object visitExprCase(ExprCase node) {
        return null;
    }

    public Object visitExprCond(ExprCond node) {
        return null;
    }

    public Object visitExprCursorAttr(ExprCursorAttr node) {
        return null;
    }

    public Object visitExprDate(ExprDate node) {
        return null;
    }

    public Object visitExprDatetime(ExprDatetime node) {
        return null;
    }

    public Object visitExprFalse(ExprFalse node) {
        return null;
    }

    public Object visitExprField(ExprField node) {
        return null;
    }

    public Object visitExprGlobalFuncCall(ExprGlobalFuncCall node) {
        return null;
    }

    public Object visitExprId(ExprId node) {
        return null;
    }

    public Object visitExprIn(ExprIn node) {
        return null;
    }

    public Object visitExprLike(ExprLike node) {
        return null;
    }

    public Object visitExprLocalFuncCall(ExprLocalFuncCall node) {
        return null;
    }

    public Object visitExprNull(ExprNull node) {
        return null;
    }

    public Object visitExprUint(ExprUint node) {
        return null;
    }

    public Object visitExprFloat(ExprFloat node) {
        return null;
    }

    public Object visitExprSerialVal(ExprSerialVal node) {
        return null;
    }

    public Object visitExprSqlRowCount(ExprSqlRowCount node) {
        return null;
    }

    public Object visitExprStr(ExprStr node) {
        return null;
    }

    public Object visitExprTime(ExprTime node) {
        return null;
    }

    public Object visitExprTrue(ExprTrue node) {
        return null;
    }

    public Object visitExprUnaryOp(ExprUnaryOp node) {
        return null;
    }

    public Object visitExprTimestamp(ExprTimestamp node) {
        return null;
    }

    public Object visitExprAutoParam(ExprAutoParam node) {
        return null;
    }

    public Object visitExprSqlCode(ExprSqlCode node) {
        return null;
    }

    public Object visitExprSqlErrm(ExprSqlErrm node) {
        return null;
    }

    public Object visitStmtAssign(StmtAssign node) {
        return null;
    }

    public Object visitStmtBasicLoop(StmtBasicLoop node) {
        return null;
    }

    public Object visitStmtBlock(StmtBlock node) {
        return null;
    }

    public Object visitStmtExit(StmtExit node) {
        return null;
    }

    public Object visitStmtCase(StmtCase node) {
        return null;
    }

    public Object visitStmtCommit(StmtCommit node) {
        return null;
    }

    public Object visitStmtContinue(StmtContinue node) {
        return null;
    }

    public Object visitStmtCursorClose(StmtCursorClose node) {
        return null;
    }

    public Object visitStmtCursorFetch(StmtCursorFetch node) {
        return null;
    }

    public Object visitStmtCursorOpen(StmtCursorOpen node) {
        return null;
    }

    public Object visitStmtExecImme(StmtExecImme node) {
        return null;
    }

    public Object visitStmtStaticSql(StmtStaticSql node) {
        return null;
    }

    public Object visitStmtForCursorLoop(StmtForCursorLoop node) {
        return null;
    }

    public Object visitStmtForIterLoop(StmtForIterLoop node) {
        return null;
    }

    public Object visitStmtForStaticSqlLoop(StmtForStaticSqlLoop node) {
        return null;
    }

    public Object visitStmtForExecImmeLoop(StmtForExecImmeLoop node) {
        return null;
    }

    public Object visitStmtGlobalProcCall(StmtGlobalProcCall node) {
        return null;
    }

    public Object visitStmtIf(StmtIf node) {
        return null;
    }

    public Object visitStmtLocalProcCall(StmtLocalProcCall node) {
        return null;
    }

    public Object visitStmtNull(StmtNull node) {
        return null;
    }

    public Object visitStmtOpenFor(StmtOpenFor node) {
        return null;
    }

    public Object visitStmtRaise(StmtRaise node) {
        return null;
    }

    public Object visitStmtRaiseAppErr(StmtRaiseAppErr node) {
        return null;
    }

    public Object visitStmtReturn(StmtReturn node) {
        return null;
    }

    public Object visitStmtRollback(StmtRollback node) {
        return null;
    }

    public Object visitStmtWhileLoop(StmtWhileLoop node) {
        return null;
    }

    public Object visitBody(Body node) {
        return null;
    }

    public Object visitExHandler(ExHandler node) {
        return null;
    }

    public Object visitExName(ExName node) {
        return null;
    }

    public Object visitTypeSpecPercent(TypeSpecPercent node) {
        return null;
    }

    public Object visitTypeSpecSimple(TypeSpecSimple node) {
        return null;
    }

    public Object visitCaseExpr(CaseExpr node) {
        return null;
    }

    public Object visitCaseStmt(CaseStmt node) {
        return null;
    }

    public Object visitCondExpr(CondExpr node) {
        return null;
    }

    public Object visitCondStmt(CondStmt node) {
        return null;
    }

    // -----------------------------------------------------------------
    // Private
    // -----------------------------------------------------------------

    private int indentLevel = -1;

    private void addSingleStr(String ss) {
        assert(ss != null);
        assert(indent >= 0);

        String indent = Misc.getIndent(indentLevel);
        for (String s: ss.split("\n") {
            codeLines.add(indent + s);
        }
    }

    private void addStrArr(String... arr) {
        for (String ss: arr) {
            addSingleLine(ss);
        }
    }
}

