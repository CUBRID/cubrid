package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;
import com.cubrid.plcsql.compiler.Scope;

public class ExprId implements I_Expr {

    public final String name;
    public final Scope scope;
    public final I_DeclId decl;

    public ExprId(String name, Scope scope, I_DeclId decl) {
        this.name = name;
        this.scope = scope;
        this.decl = decl;
    }

    @Override
    public String toJavaCode() {
        if (decl instanceof DeclParamOut) {
            return String.format("$%s[0]", name);
        } else if (decl instanceof DeclParamIn) {
            return String.format("$%s", name);
        } else if (decl instanceof DeclForIter) {
            return String.format("$%s_i%d", name, decl.scope().level);
        } else if (decl instanceof DeclForRecord) {
            return String.format("$%s_r%d", name, decl.scope().level);
        } else if (decl instanceof DeclConst || decl instanceof DeclCursor) {
            if (scope.routine.equals(decl.scope().routine)) {
                return String.format("%s.$%s", decl.scope().block, name);
            } else {
                return String.format("$%s", name);
            }
        } else if (decl instanceof DeclVar) {
            if (scope.routine.equals(decl.scope().routine)) {
                return String.format("%s.$%s[0]", decl.scope().block, name);
            } else {
                return String.format("$%s[0]", name);
            }
        } else {
            assert false;
            return null;
        }
    }

    public String toJavaCodeForOutParam() {
        if (decl instanceof DeclParamOut) {
            return String.format("$%s", name);
        } else if (decl instanceof DeclVar) {
            if (scope.routine.equals(decl.scope().routine)) {
                return String.format("%s.$%s", decl.scope().block, name);
            } else {
                return String.format("$%s", name);
            }
        } else {
            assert false;
            return null;
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
