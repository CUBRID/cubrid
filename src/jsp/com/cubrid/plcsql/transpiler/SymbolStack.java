package com.cubrid.plcsql.transpiler;

import com.cubrid.plcsql.transpiler.ast.*;

import static com.cubrid.plcsql.transpiler.antlrgen.PcsParser.*;

import java.util.LinkedList;
import java.util.Map;
import java.util.TreeMap;

public class SymbolStack {

    static int pushSymbolTable(String name, boolean forRoutine) {

        int level = symbolTableStack.size();
        name = name.toLowerCase();

        String routine, block;
        if (forRoutine) {
            routine = name + "_" + level;
            block = name;
        } else {
            routine = currSymbolTable.scope.routine;
            block = name + "_" + level;
        }

        currSymbolTable = new SymbolTable(new Scope(routine, block, level));
        symbolTableStack.addFirst(currSymbolTable);

        return level;
    }

    static void popSymbolTable() {
        assert symbolTableStack.size() > 0;
        symbolTableStack.removeFirst();
        currSymbolTable = symbolTableStack.peek();
    }

    static int getSize() {
        return symbolTableStack.size();
    }

    static Scope getCurrentScope() {
        return currSymbolTable.scope;
    }

    static <D extends I_Decl> void putDecl(String name, D decl) {
        assert decl != null;

        Map<String, D> map = currSymbolTable.<D>map((Class<D>) decl.getClass());
        if (map.containsKey(name)) {
            assert false: decl.typeStr() + " " + name + " has already been declared in the same routine";
            throw new RuntimeException("unreachable");
        }

        decl.setScope(currSymbolTable.scope);
        map.put(name, decl);
    }

    static I_DeclId         getDeclId       (String name) { return getDecl(I_DeclId.class, name); }
    static DeclException    getDeclException(String name) { return getDecl(DeclException.class, name); }
    static DeclProc         getDeclProc     (String name) { return getDecl(DeclProc.class, name); }
    static DeclFunc         getDeclFunc     (String name) { return getDecl(DeclFunc.class, name); }
    static DeclLabel        getDeclLabel    (String name) { return getDecl(DeclLabel.class, name); }

    // ----------------------------------------------------
    // Private
    // ----------------------------------------------------

    private static SymbolTable currSymbolTable;

    private static LinkedList<SymbolTable> symbolTableStack = new LinkedList<>();

    private static class SymbolTable {
        final Scope scope;
        //final Map<String, DeclCursor> cursors;
        final Map<String, I_DeclId> ids;
        final Map<String, DeclException> exceptions;
        final Map<String, DeclProc> procs;
        final Map<String, DeclFunc> funcs;
        final Map<String, DeclLabel> labels;

        SymbolTable(Scope scope) {
            this.scope = scope;

            //cursors = new TreeMap<>();
            ids = new TreeMap<>();
            exceptions = new TreeMap<>();
            procs = new TreeMap<>();
            funcs = new TreeMap<>();
            labels = new TreeMap<>();
        }

        <D extends I_Decl> Map<String, D> map(Class<D> declClass) {
            /*if (DeclCursor.class.isAssignableFrom(declClass)) {
                return (Map<String, D>) cursors;
            } else */if (I_DeclId.class.isAssignableFrom(declClass)) {
                return (Map<String, D>) ids;
            } else if (DeclException.class.isAssignableFrom(declClass)) {
                return (Map<String, D>) exceptions;
            } else if (DeclProc.class.isAssignableFrom(declClass)) {
                return (Map<String, D>) procs;
            } else if (DeclFunc.class.isAssignableFrom(declClass)) {
                return (Map<String, D>) funcs;
            } else if (DeclLabel.class.isAssignableFrom(declClass)) {
                return (Map<String, D>) labels;
            } else {
                assert false: ("unknown declaration type: " + declClass.getSimpleName());
                return null;
            }
        }
    }

    private static <D extends I_Decl> D getDecl(Class<D> declClass, String name) {
        assert name != null;

        int size = symbolTableStack.size();
        for (int i = 0; i < size; i++) {
            SymbolTable t = symbolTableStack.get(i);
            Map<String, D> map = t.<D>map(declClass);
            if (map.containsKey(name)) {
                return map.get(name);
            }
        }

        return null;
    }
}
