package com.cubrid.plcsql.driver;

import org.antlr.v4.runtime.*;
import org.antlr.v4.runtime.tree.*;

import com.cubrid.plcsql.transpiler.antlrgen.PcsLexer;
import com.cubrid.plcsql.transpiler.antlrgen.PcsParser;

import com.cubrid.plcsql.transpiler.ParseTreePrinter;
import com.cubrid.plcsql.transpiler.ParseTreeConverter;

import com.cubrid.plcsql.transpiler.ast.Unit;

import java.io.File;
import java.io.FileInputStream;
import java.io.PrintStream;
import java.io.FileReader;
import java.io.BufferedReader;
import java.io.FileNotFoundException;
import java.io.IOException;

import java.util.Map;
import java.util.TreeMap;

public class TestDriver
{
    private static ParseTree parse(String inFilePath) {

        long t0, t;

        t0 = System.currentTimeMillis();

        File f = new File(inFilePath);
        if (!f.isFile()) {
            throw new RuntimeException(inFilePath + " is not a file");
        }

        System.out.println(String.format("  creating File: %f sec",
            ((t = System.currentTimeMillis()) - t0) / 1000.0));
        t0 = t;

        FileInputStream in;
        try {
            in = new FileInputStream(f);
        } catch (FileNotFoundException e) {
            throw new RuntimeException(e);
        }

        System.out.println(String.format("  creating FileInputStream: %f sec",
            ((t = System.currentTimeMillis()) - t0) / 1000.0));
        t0 = t;

        ANTLRInputStream input;
        try {
            input = new ANTLRInputStream(in);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }

        System.out.println(String.format("  creating ANTLRInputStream: %f sec",
            ((t = System.currentTimeMillis()) - t0) / 1000.0));
        t0 = t;

        PcsLexer lexer = new PcsLexer(input);
        CommonTokenStream tokens = new CommonTokenStream(lexer);
        PcsParser parser = new PcsParser(tokens);

        System.out.println(String.format("  creating PcsLexer: %f sec",
            ((t = System.currentTimeMillis()) - t0) / 1000.0));
        t0 = t;

        ParseTree ret = parser.sql_script();

        System.out.println(String.format("  calling parser: %f sec",
            (System.currentTimeMillis() - t0) / 1000.0));

        return ret;
    }

    private static PrintStream getParseTreePrinterOutStream(int seq) {

        // create a output stream to print parse tree
        String outfile = String.format("./pt/T%05d.pt", seq);
        File g = new File(outfile);
        try {
            return new PrintStream(g);
        } catch (FileNotFoundException e) {
            throw new RuntimeException(e);
        }
    }

    private static PrintStream getJavaCodeOutStream(String className) {

        String outfile = String.format("./pt/%s.java", className);
        File g = new File(outfile);
        if (g.exists()) {
            throw new RuntimeException("file exists: " + outfile);
        }

        try {
            return new PrintStream(g);
        } catch (FileNotFoundException e) {
            throw new RuntimeException(e);
        }
    }

    public static void main(String[] args) {

        if (args.length != 2) {
           throw new RuntimeException("requires two arguments (a PL/CSQL file path and its sequence number)");
        }

        long t, t0;

        t0 = System.currentTimeMillis();

        String infile = args[0];
        ParseTree tree = parse(infile);
        if (tree == null) {
            throw new RuntimeException("parsing failed");
        }

        System.out.println(String.format("parsing: %f sec", ((t = System.currentTimeMillis()) - t0) / 1000.0));
        t0 = t;

        // get sequence number of the input file
        int seq;
        try {
            seq = Integer.parseInt(args[1]);
        } catch (NumberFormatException e) {
            throw new RuntimeException(e);
        }

        // walk with a pretty printer to print parse tree
        PrintStream out = getParseTreePrinterOutStream(seq);
        ParseTreePrinter pp = new ParseTreePrinter(out, infile);
        ParseTreeWalker.DEFAULT.walk(pp, tree);
        out.close();

        System.out.println(String.format("printing: %f sec", ((t = System.currentTimeMillis()) - t0) / 1000.0));
        t0 = t;

        ParseTreeConverter converter = new ParseTreeConverter();
        Unit unit = (Unit) converter.visit(tree);
        out = getJavaCodeOutStream(unit.getClassName());
        out.println(String.format("// seq=%05d, input-file=%s", seq, infile));
        out.print(unit.toJavaCode());
        out.close();

        System.out.println(String.format("converting: %f sec", ((t = System.currentTimeMillis()) - t0) / 1000.0));
        t0 = t;

    }
}
