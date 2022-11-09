package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

import java.util.Arrays;

public class DeclCursor extends Decl implements I_DeclId {

    public final String name;
    public final NodeList<I_DeclParam> paramList;
    public final ExprStr sql;
    public final NodeList<ExprId> usedVars;

    public int[] paramRefCounts;
    public int[] usedValuesMap;

    public DeclCursor(String name, NodeList<I_DeclParam> paramList, ExprStr sql, NodeList<ExprId> usedVars) {
        this.name = name;
        this.paramList = paramList;
        this.sql = sql;
        this.usedVars = usedVars;

        setHostValuesMap(paramList, usedVars);
    }

    public TypeSpec typeSpec() {
        assert false: "unreachable";    // cursors do not appear alone in a program
        throw new RuntimeException("unreachable");
    }

    @Override
    public String typeStr() {
        return "cursor";
    }

    @Override
    public String toJavaCode() {
        return String.format("final Query $%s = new Query(%s);\n  // param-ref-counts: %s\n  // used-values-map: %s",
            name, sql.toJavaCode(),
            Arrays.toString(paramRefCounts),
            Arrays.toString(usedValuesMap));
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private void setHostValuesMap(NodeList<I_DeclParam> paramList, NodeList<ExprId> usedVars) {

        int paramSize = paramList == null ? 0 : paramList.nodes.size();
        int usedSize = usedVars == null ? 0 : usedVars.nodes.size();

        paramRefCounts = new int[paramSize];    // NOTE: filled with zeros
        usedValuesMap = new int[usedSize];      // NOTE: filled with zeros

        for (int i = 0; i < paramSize; i++) {
            I_DeclParam di = paramList.nodes.get(i);
            for (int j = 0; j < usedSize; j++) {
                I_DeclId dj = usedVars.nodes.get(j).decl;
                if (di == dj) {     // NOTE: reference equality
                    usedValuesMap[j] = -(i + 1);
                    paramRefCounts[i]++;
                }
            }
        }
    }
}
