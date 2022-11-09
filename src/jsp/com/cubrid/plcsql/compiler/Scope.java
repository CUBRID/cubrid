package com.cubrid.plcsql.compiler;

public class Scope {

    public final String routine;
    public final String block;
    public final int level;

    Scope(String routine, String block, int level) {
        this.routine = routine;
        this.block = block;
        this.level = level;
    }
}


