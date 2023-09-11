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

    public List<String> codeLines = new ArrayList<>();  // no LinkedList : frequent access by indexes
    public StringBuilder codeRangeMarkers = new StringBuilder();

    public void buildCodeLines(Unit unit) {
        CodeToResolve ctr = visitUnit(unit);
        ctr.resolve(0, codeLines, codeRangeMarkers);

        System.out.println(String.format("[temp] code range markers = [%s]", codeRangeMarkers.toString()));
        System.out.println(String.format("[temp] code\n%s", String.join("\n", codeLines.toArray(dummyStrArr))));
    }

    @Override
    public <E extends AstNode> CodeTemplateList visitNodeList(NodeList<E> nodeList) {
        CodeTemplateList list = new CodeTemplateList();
        for (E e : nodeList.nodes) {
            list.addElement((CodeTemplate) visit(e));
        }
        return list;
    }

    // -----------------------------------------------------------------
    // Unit
    //
    private static final String tmplGetConn =
        "Connection conn = DriverManager.getConnection(\"jdbc:default:connection::?autonomous_transaction=%s\");";
    private static final String[] tmplUnit =
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
            "    %'%NULLIFY-OUT-PARAMETERS'%",
            "",
            "    try {",
            "      Long[] sql_rowcount = new Long[] { null };",
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
        };

    @Override
    public CodeToResolve visitUnit(Unit node) {

        // get connection, if necessary
        String strGetConn = node.connectionRequired ? String.format(tmplGetConn, node.autonomousTransaction) :
            "// connection not required";

        // declarations
        Object codeDeclClass = node.routine.decls == null ? "// no declarations" :
            new CodeTemplate(
                "DeclClass of Unit",
                Misc.UNKNOWN_LINE_COLUMN,
                tmplDeclClass,
                "%'BLOCK'%", node.routine.getDeclBlockName(),
                "%'%DECLARATIONS'%", visitNodeList(node.routine.decls).setDelimiter(""));

        // imports
        String[] importsArray = node.getImportsArray();
        int len = importsArray.length;
        for (int i = 0; i < len; i++) {
            importsArray[i] = "import " + importsArray[i] + ";";
        }

        // parameters
        Object strParamArr = Misc.isEmpty(node.routine.paramList) ? "// no parameters" :
            visitNodeList(node.routine.paramList).setDelimiter(",");

        // nullify OUT parameters
        String strNullifyOutParam = DeclRoutine.getNullifyOutParamStr(node.routine.paramList);

        return new CodeTemplate(
            "Unit",
            new int[] { 1, 1 },
            tmplUnit,
            "%'%IMPORTS'%", importsArray,
            "%'CLASS-NAME'%", node.getClassName(),
            "%'RETURN-TYPE'%", node.routine.retType == null ? "void" : node.routine.retType.javaCode,
            "%'METHOD-NAME'%", node.routine.name,
            "%'%PARAMETERS'%", strParamArr,
            "%'%NULLIFY-OUT-PARAMETERS'%", strNullifyOutParam.split("\n"),
            "%'GET-CONNECTION'%", strGetConn,
            "%'%DECL-CLASS'%", codeDeclClass,
            "%'%BODY'%", visit(node.routine.body)
        );
    }

    // -----------------------------------------------------------------
    // Routine (Procedure, Function)
    //

    private static final String[] tmplDeclRoutine = new String[] {
        "%'RETURN-TYPE'% %'METHOD-NAME'%(",
        "    %'PARAMETERS'%",
        "  ) throws Exception {",
        "",
        "  %'NULLIFY-OUT-PARAMETERS'%",
        "",
        "  %'DECL-CLASS'%",
        "",
        "  %'BODY'%",
        "}"
    };

    private CodeToResolve visitDeclRoutine(DeclRoutine node) {

        assert node.paramList != null;

        // declarations
        Object codeDeclClass = node.decls == null ? "// no declarations" :
            new CodeTemplate(
                "DeclClass of Routine",
                Misc.UNKNOWN_LINE_COLUMN,
                tmplDeclClass,
                "%'BLOCK'%", node.getDeclBlockName(),
                "%'%DECLARATIONS'%", visitNodeList(node.decls).setDelimiter(""));

        String strNullifyOutParam = DeclRoutine.getNullifyOutParamStr(node.paramList);

        return new CodeTemplate(
            "DeclRoutine",
            Misc.getLineColumnOf(node.ctx),
            tmplDeclRoutine,
            "%'RETURN-TYPE'%", node.retType == null ? "void" : node.retType.javaCode,
            "%'%PARAMETERS'%", visitNodeList(node.paramList).setDelimiter(","),
            "%'%NULLIFY-OUT-PARAMETERS'%", strNullifyOutParam.split("\n"),
            "%'%DECL-CLASS'%", codeDeclClass,
            "%'%BODY'%", visitBody(node.body),
            "%'METHOD-NAME'%", node.name);
    }

    @Override
    public CodeToResolve visitDeclFunc(DeclFunc node) {
        return visitDeclRoutine(node);
    }

    @Override
    public CodeToResolve visitDeclProc(DeclProc node) {
        return visitDeclRoutine(node);
    }

    @Override
    public CodeToResolve visitDeclParamIn(DeclParamIn node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitDeclParamOut(DeclParamOut node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitDeclVar(DeclVar node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitDeclConst(DeclConst node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitDeclCursor(DeclCursor node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitDeclLabel(DeclLabel node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitDeclException(DeclException node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprBetween(ExprBetween node) {
        assert false : "unimplemented yet";
        return null;
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
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprCond(ExprCond node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprCursorAttr(ExprCursorAttr node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprDate(ExprDate node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprDatetime(ExprDatetime node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprFalse(ExprFalse node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprField(ExprField node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprGlobalFuncCall(ExprGlobalFuncCall node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprId(ExprId node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprIn(ExprIn node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprLike(ExprLike node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprBuiltinFuncCall(ExprBuiltinFuncCall node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprLocalFuncCall(ExprLocalFuncCall node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprNull(ExprNull node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprUint(ExprUint node) {
        return new CodeTemplate("ExprUnit",
            Misc.getLineColumnOf(node.ctx),
            new String[] { node.toJavaCode() }); // TODO: temporary
    }

    @Override
    public CodeToResolve visitExprFloat(ExprFloat node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprSerialVal(ExprSerialVal node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprSqlRowCount(ExprSqlRowCount node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprStr(ExprStr node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprTime(ExprTime node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprTrue(ExprTrue node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprUnaryOp(ExprUnaryOp node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprTimestamp(ExprTimestamp node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprAutoParam(ExprAutoParam node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprSqlCode(ExprSqlCode node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExprSqlErrm(ExprSqlErrm node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtAssign(StmtAssign node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtBasicLoop(StmtBasicLoop node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtBlock(StmtBlock node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtExit(StmtExit node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtCase(StmtCase node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtCommit(StmtCommit node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtContinue(StmtContinue node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtCursorClose(StmtCursorClose node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtCursorFetch(StmtCursorFetch node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtCursorOpen(StmtCursorOpen node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtExecImme(StmtExecImme node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtStaticSql(StmtStaticSql node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtForCursorLoop(StmtForCursorLoop node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtForIterLoop(StmtForIterLoop node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtForStaticSqlLoop(StmtForStaticSqlLoop node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtForExecImmeLoop(StmtForExecImmeLoop node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtGlobalProcCall(StmtGlobalProcCall node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtIf(StmtIf node) {
        assert false : "unimplemented yet";
        return null;
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
        return new CodeTemplate("StmtNull", Misc.getLineColumnOf(node.ctx), ";");
    }

    @Override
    public CodeToResolve visitStmtOpenFor(StmtOpenFor node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtRaise(StmtRaise node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtRaiseAppErr(StmtRaiseAppErr node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtReturn(StmtReturn node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtRollback(StmtRollback node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitStmtWhileLoop(StmtWhileLoop node) {
        assert false : "unimplemented yet";
        return null;
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
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitExName(ExName node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitTypeSpecPercent(TypeSpecPercent node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitTypeSpecSimple(TypeSpecSimple node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitCaseExpr(CaseExpr node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitCaseStmt(CaseStmt node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitCondExpr(CondExpr node) {
        assert false : "unimplemented yet";
        return null;
    }

    @Override
    public CodeToResolve visitCondStmt(CondStmt node) {
        assert false : "unimplemented yet";
        return null;
    }

    interface CodeToResolve {
        void resolve(int indentLevel, List<String> codeLines, StringBuilder codeRangeMarkers);
    }

    // -----------------------------------------------------------------
    // Private
    // -----------------------------------------------------------------

    private static final int[] POSITION_IGNORED = new int[] { -1, -1 };
    private static final String[] dummyStrArr = new String[0];

    private static final String[] tmplDeclClass =
        new String[] {
            "class Decl_of_%'BLOCK'% {",
            "  Decl_of_%'BLOCK'%() throws Exception {};",
            "  %'DECLARATIONS'%",
            "}",
            "Decl_of_%'BLOCK'% %'BLOCK'% = new Decl_of_%'BLOCK'%();"
        };

    private static class CodeTemplateList implements CodeToResolve {

        boolean resolved;
        final List<CodeTemplate> elements;
        String delimiter;

        CodeTemplateList() {
            elements = new ArrayList<>();
        }

        void addElement(CodeTemplate element) {     // NOTE: CodeTemplate, not CodeToResolve
            elements.add(element);
        }

        CodeTemplateList setDelimiter(String delimiter) {
            this.delimiter = delimiter;
            return this;
        }

        public void resolve(int indentLevel, List<String> codeLines, StringBuilder codeRangeMarkers) {

            assert !resolved : "already resolved";
            assert delimiter != null : "delimiter must be set before resolve() is called";

            if (delimiter.length() == 0) {
                for (CodeTemplate t: elements) {
                    t.resolve(indentLevel, codeLines, codeRangeMarkers);
                }
            } else {
                boolean first = true;
                for (CodeTemplate t: elements) {
                    if (first) {
                        first = false;
                    } else {
                        int lastIdx = codeLines.size() - 1;
                        assert lastIdx >= 0;
                        codeLines.set(lastIdx, codeLines.get(lastIdx) + delimiter);
                    }
                    t.resolve(indentLevel, codeLines, codeRangeMarkers);
                }
            }

            resolved = true;
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

        CodeTemplate(String astNode, int[] plcsqlPos, String template, Object... pairs) {
            this(astNode, plcsqlPos, new String[] { template }, pairs);
        }

        CodeTemplate(String astNode, int[] plcsqlPos, String[] template, Object... pairs) {

            assert plcsqlPos != null && plcsqlPos.length == 2;
            assert template != null;

            this.astNode = astNode;

            int plcsqlLine = plcsqlPos[0];
            int plcsqlColumn = plcsqlPos[1];

            // used: plcsqlLineColumn, template, substitutions
            if (plcsqlLine < 0 && plcsqlColumn < 0) {
                this.plcsqlLineColumn = null;   // do not mark code range in this case
            } else {
                assert plcsqlLine > 0 && plcsqlColumn > 0 : String.format(
                        "%s - line and column numbers of code templates must be positive integers: (%d, %d)",
                        astNode, plcsqlLine, plcsqlColumn);
                this.plcsqlLineColumn = String.format("%d,%d", plcsqlLine, plcsqlColumn);
            }
            this.template = template;

            int len = pairs.length;
            assert len % 2 == 0 : astNode + " - the number of substitution pairs elements must be an even number";
            for (int i = 0; i < len; i += 2) {

                assert pairs[i] instanceof String : astNode + " - first element of each pair must be a String: " + pairs[i];
                String hole = (String) pairs[i];
                assert hole.startsWith("%'") : astNode + " - first element of each pair must indicate a hole: " + pairs[i];

                Object thing = pairs[i + 1];
                if (thing instanceof String || thing instanceof String[] || thing instanceof CodeToResolve) {
                    // String is for a small hole, and the String[] and CodeToResolve are for a big hole
                    this.substitutions.put(hole, thing);
                } else {
                    assert false : "invalid type of a substitute " + thing;
                }
            }
        }

        public void resolve(int indentLevel, List<String> codeLines, StringBuilder codeRangeMarkers) {

            assert !resolved : "already resolved";

            // Critical Condition:
            // The range (start and end line numbers) of code of an AST node resolved by this method does not change
            // once this method is done for the AST node.

            boolean markCodeRange = plcsqlLineColumn != null;
            if (markCodeRange) {
                codeRangeMarkers.append(String.format(" (%d,%s", codeLines.size() + 1, plcsqlLineColumn));
            }

            for (String line: template) {
                assert line.indexOf("\n") == -1 : "every line of a templates must be a single line: '" + line + "'";
                resolveTemplateLine(line, indentLevel, codeLines, codeRangeMarkers);
            }

            if (markCodeRange) {
                codeRangeMarkers.append(String.format(" )%d", codeLines.size() + 1));
            }

            resolved = true;
        }

        // -----------------------------------------------
        // Private
        // -----------------------------------------------

        private String substituteSmallHolesInLine(String line, Set<String> smallHoles) {

            for (String hole: substitutions.keySet()) {
                if (smallHoles.contains(hole)) {
                    assert !isBigHole(hole) : "a big hole in the small holes set: '" + hole + "'";
                    Object substitute = substitutions.get(hole);
                    if (substitute instanceof String) {
                        line = line.replace(hole, (String) substitute);
                    } else {
                        assert false : "substitute for a small hole '" + hole + "' is not a String: '" + substitute + "'";
                    }
                }
            }
            assert line.indexOf("%'") == -1 :
                "holes cannot remain after completing small holes substitutions: '" + line + "'";

            return line;
        }

        private void resolveTemplateLine(String line, int indentLevel, List<String> codeLines,
                StringBuilder codeRangeMarkers) {

            assert line != null;

            assert !line.endsWith(" ") : "a template line may not have a trailing space: '" + line + "'";


            Set<String> smallHoles = new HashSet<>();
            String bigHole = getHoles(smallHoles, line);
            if (bigHole == null) {

                // case 1: word replacements in a single line (namely, small holes)

                line = substituteSmallHolesInLine(line, smallHoles);
                String indent = Misc.getIndent(indentLevel);
                codeLines.add(indent + line);
            } else {
                assert smallHoles.size() == 0;

                // case 2: expanded to multiple lines (namely, a single big hole)

                int spaces = line.indexOf(bigHole);
                int indentLevelDelta = spaces / Misc.INDENT_SIZE;
                String indent = Misc.getIndent(indentLevel + indentLevelDelta);

                String remainder = line.substring(spaces + bigHole.length());

                Object substitute = substitutions.get(bigHole);
                if (substitute instanceof String) {
                    String l = (String) substitute;
                    if (l.indexOf("%'") == -1) {
                        codeLines.add(indent + l);
                    } else {
                        resolveTemplateLine(l, indentLevel, codeLines, codeRangeMarkers);
                    }
                } else if (substitute instanceof String[]) {
                    for (String l: (String[]) substitute) {
                        if (l.indexOf("%'") == -1) {
                            codeLines.add(indent + l);
                        } else {
                            resolveTemplateLine(l, indentLevel, codeLines, codeRangeMarkers);
                        }
                    }
                } else if (substitute instanceof CodeToResolve) {
                    int startLineIdx = codeLines.size();

                    ((CodeToResolve) substitute).resolve(indentLevel + indentLevelDelta, codeLines, codeRangeMarkers);

                    // Replace small holes, if any, returned from the subnodes.
                    // NOTE: Big holes do not exist in the code resolved by subnodes because the code templates are
                    //       written so. Otherwise, the code range of sub-subnodes, if any, should be altered.
                    int upper = codeLines.size();
                    for (int i = startLineIdx; i < upper; i++) {

                        String l = codeLines.get(i);
                        smallHoles.clear();
                        bigHole = getHoles(smallHoles, l);
                        assert bigHole == null : "a line resolved by a subnode has a big hole: '" + l + "'";
                                // this condition is critical for the code range markers not to change once settled.
                        if (smallHoles.size() > 0) {
                            l = substituteSmallHolesInLine(l, smallHoles);
                            codeLines.set(i, l);
                        }
                    }

                } else {
                    assert false : "substitute for a big hole '" + bigHole +
                        "' is neither a String, String[] nor a CodeToResolve: '" + substitute + "'";
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

        private static boolean isBigHole(String hole) {
            return hole.startsWith("%'%");
        }

        //
        private static String getHoles(Set<String> holes, String line) {

            int i = 0;
            int len = line.length();
            boolean first = true;
            while (i < len) {
                int begin = line.indexOf("%'", i);
                if (begin == -1) {
                    return null;
                } else {
                    int end = line.indexOf("'%", begin + 2);
                    assert end != -1 : "not closed hole in a line '" + line + "'";
                    i = end + 2;

                    String hole = line.substring(begin, i);
                    if (first) {
                        first = false;
                        if (isBigHole(hole)) {
                            for (int j = 0; j < begin; j++) {
                                assert line.charAt(j) == ' ' : "only spaces allowed before a big hole: '" + line + "'";
                            }
                            assert line.indexOf("%'", i) == -1 : "no more holes after a big hole: '" + line + "'";

                            return hole;
                        }
                    } else {
                        assert !isBigHole(hole) : "big holes must be the only hole in the line: '" + hole + "'";
                    }

                    holes.add(hole);
                }
            }

            return null;
        }
    }
}

