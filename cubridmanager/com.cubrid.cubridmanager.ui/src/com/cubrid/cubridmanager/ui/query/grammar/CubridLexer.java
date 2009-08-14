// $ANTLR 3.0.1 Cubrid.g 2009-07-10 16:31:47
package com.cubrid.cubridmanager.ui.query.grammar;

import org.antlr.runtime.BaseRecognizer;
import org.antlr.runtime.CharStream;
import org.antlr.runtime.DFA;
import org.antlr.runtime.EarlyExitException;
import org.antlr.runtime.Lexer;
import org.antlr.runtime.MismatchedSetException;
import org.antlr.runtime.NoViableAltException;
import org.antlr.runtime.RecognitionException;

public class CubridLexer extends Lexer {
    public static final int FUNCTION=48;
    public static final int DECIMALLITERAL=145;
    public static final int MULTISET=68;
    public static final int STAR=130;
    public static final int AUTOCOMMIT=13;
    public static final int VARYING=117;
    public static final int PRECISION=85;
    public static final int MARKS=150;
    public static final int TRIGGER=108;
    public static final int CASE=19;
    public static final int CHAR=23;
    public static final int Q_MARK=139;
    public static final int ENDBRACKET=144;
    public static final int NOT=72;
    public static final int EXCEPT=42;
    public static final int CASCADE=21;
    public static final int FOREIGN=46;
    public static final int CACHE=20;
    public static final int EOF=-1;
    public static final int ACTION=4;
    public static final int CHARACTER=24;
    public static final int CLEAR=127;
    public static final int CREATE=29;
    public static final int INSERT=55;
    public static final int QUOTA=134;
    public static final int STRING_STR=101;
    public static final int USING=115;
    public static final int CONNECT=137;
    public static final int PATH=140;
    public static final int DATE_FORMAT=147;
    public static final int INTERSECTION=58;
    public static final int OFF=77;
    public static final int AUTO_INCREMENT=14;
    public static final int DOUBLE=38;
    public static final int SHARE=98;
    public static final int CHINESE=152;
    public static final int SELECT=94;
    public static final int WORK=123;
    public static final int INTO=59;
    public static final int DBQUOTA=135;
    public static final int UNIQUE=113;
    public static final int VIEW=119;
    public static final int ASC=11;
    public static final int STARTBRACE=131;
    public static final int KEY=62;
    public static final int NULL=73;
    public static final int ENDBRACE=132;
    public static final int ELSE=40;
    public static final int NO=71;
    public static final int ON=78;
    public static final int INT=56;
    public static final int PRIMARY=86;
    public static final int DELETE=35;
    public static final int NUMERIC=74;
    public static final int ROLLBACK=93;
    public static final int FILE=44;
    public static final int LIST=64;
    public static final int OF=76;
    public static final int RESTRICT=91;
    public static final int SEQUENCE=95;
    public static final int REAL=88;
    public static final int GROUP=49;
    public static final int WS=154;
    public static final int OR=81;
    public static final int PM=84;
    public static final int QUERY=87;
    public static final int CHECK=25;
    public static final int CALL=18;
    public static final int MULTISET_OF=69;
    public static final int END=128;
    public static final int REVERSE=100;
    public static final int FROM=47;
    public static final int UNTAB=126;
    public static final int DISTINCT=37;
    public static final int CONSTRAINT=28;
    public static final int TIMESTAMP=106;
    public static final int RENAME=90;
    public static final int DOLLAR=138;
    public static final int WHERE=121;
    public static final int CLASS=26;
    public static final int ALTER=7;
    public static final int OPTION=80;
    public static final int INNER=54;
    public static final int T159=159;
    public static final int T158=158;
    public static final int TIME_FORMAT=148;
    public static final int NCHAR=70;
    public static final int ORDER=82;
    public static final int ONLY=79;
    public static final int STARTBRACKET=143;
    public static final int ATTRIBUTE=12;
    public static final int T161=161;
    public static final int T162=162;
    public static final int TABLE=104;
    public static final int UPDATE=114;
    public static final int T163=163;
    public static final int DEFERRED=32;
    public static final int VARCHAR=116;
    public static final int T164=164;
    public static final int T165=165;
    public static final int FLOAT=45;
    public static final int T166=166;
    public static final int T167=167;
    public static final int T168=168;
    public static final int MONETARY=67;
    public static final int AND=9;
    public static final int ID=141;
    public static final int LENGTH=146;
    public static final int END_STRING=41;
    public static final int T160=160;
    public static final int INDEX=52;
    public static final int AS=10;
    public static final int TIME=105;
    public static final int ML_COMMENT=155;
    public static final int IN=51;
    public static final int THEN=107;
    public static final int JAPAN=153;
    public static final int OBJECT=75;
    public static final int REFERENCES=89;
    public static final int COMMA=129;
    public static final int IS=60;
    public static final int T169=169;
    public static final int LEFT=65;
    public static final int ALL=6;
    public static final int EQUAL=136;
    public static final int VCLASS=118;
    public static final int COLUMN=142;
    public static final int T174=174;
    public static final int T172=172;
    public static final int T173=173;
    public static final int SEQUENCE_OF=96;
    public static final int SUPERCLASS=103;
    public static final int EXISTS=43;
    public static final int DIFFERENCE=36;
    public static final int DOT=133;
    public static final int T170=170;
    public static final int T171=171;
    public static final int LIKE=63;
    public static final int AM=8;
    public static final int WITH=122;
    public static final int ADD=5;
    public static final int INTEGER=57;
    public static final int OUTER=83;
    public static final int BY=17;
    public static final int TO=110;
    public static final int INHERIT=53;
    public static final int DEFAULT=34;
    public static final int VALUES=111;
    public static final int SUBCLASS=102;
    public static final int TAB=125;
    public static final int RIGHT=92;
    public static final int SET=97;
    public static final int HAVING=50;
    public static final int Tokens=175;
    public static final int JOIN=61;
    public static final int UNION=112;
    public static final int CHANGE=22;
    public static final int COMMIT=27;
    public static final int DECIMAL=31;
    public static final int DROP=39;
    public static final int WHEN=120;
    public static final int ENTER=124;
    public static final int T156=156;
    public static final int T157=157;
    public static final int BIT=16;
    public static final int TRIGGERS=109;
    public static final int DESC=33;
    public static final int DATE=30;
    public static final int METHOD=66;
    public static final int BETWEEN=15;
    public static final int KOREA=151;
    public static final int STRING=149;
    public static final int SMALLINT=99;
    public CubridLexer() {;} 
    public CubridLexer(CharStream input) {
        super(input);
    }
    public String getGrammarFileName() { return "Cubrid.g"; }

    // $ANTLR start ACTION
    public final void mACTION() throws RecognitionException {
        try {
            int _type = ACTION;
            // Cubrid.g:8:8: ( 'ACTION' )
            // Cubrid.g:8:10: 'ACTION'
            {
            match("ACTION"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end ACTION

    // $ANTLR start ADD
    public final void mADD() throws RecognitionException {
        try {
            int _type = ADD;
            // Cubrid.g:9:5: ( 'ADD' )
            // Cubrid.g:9:7: 'ADD'
            {
            match("ADD"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end ADD

    // $ANTLR start ALL
    public final void mALL() throws RecognitionException {
        try {
            int _type = ALL;
            // Cubrid.g:10:5: ( 'ALL' )
            // Cubrid.g:10:7: 'ALL'
            {
            match("ALL"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end ALL

    // $ANTLR start ALTER
    public final void mALTER() throws RecognitionException {
        try {
            int _type = ALTER;
            // Cubrid.g:11:7: ( 'ALTER' )
            // Cubrid.g:11:9: 'ALTER'
            {
            match("ALTER"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end ALTER

    // $ANTLR start AM
    public final void mAM() throws RecognitionException {
        try {
            int _type = AM;
            // Cubrid.g:12:4: ( 'AM' )
            // Cubrid.g:12:6: 'AM'
            {
            match("AM"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end AM

    // $ANTLR start AND
    public final void mAND() throws RecognitionException {
        try {
            int _type = AND;
            // Cubrid.g:13:5: ( 'AND' )
            // Cubrid.g:13:7: 'AND'
            {
            match("AND"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end AND

    // $ANTLR start AS
    public final void mAS() throws RecognitionException {
        try {
            int _type = AS;
            // Cubrid.g:14:4: ( 'AS' )
            // Cubrid.g:14:6: 'AS'
            {
            match("AS"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end AS

    // $ANTLR start ASC
    public final void mASC() throws RecognitionException {
        try {
            int _type = ASC;
            // Cubrid.g:15:5: ( 'ASC' )
            // Cubrid.g:15:7: 'ASC'
            {
            match("ASC"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end ASC

    // $ANTLR start ATTRIBUTE
    public final void mATTRIBUTE() throws RecognitionException {
        try {
            int _type = ATTRIBUTE;
            // Cubrid.g:16:11: ( 'ATTRIBUTE' )
            // Cubrid.g:16:13: 'ATTRIBUTE'
            {
            match("ATTRIBUTE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end ATTRIBUTE

    // $ANTLR start AUTOCOMMIT
    public final void mAUTOCOMMIT() throws RecognitionException {
        try {
            int _type = AUTOCOMMIT;
            // Cubrid.g:17:12: ( 'AUTOCOMMIT' )
            // Cubrid.g:17:14: 'AUTOCOMMIT'
            {
            match("AUTOCOMMIT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end AUTOCOMMIT

    // $ANTLR start AUTO_INCREMENT
    public final void mAUTO_INCREMENT() throws RecognitionException {
        try {
            int _type = AUTO_INCREMENT;
            // Cubrid.g:18:16: ( 'AUTO_INCREMENT' )
            // Cubrid.g:18:18: 'AUTO_INCREMENT'
            {
            match("AUTO_INCREMENT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end AUTO_INCREMENT

    // $ANTLR start BETWEEN
    public final void mBETWEEN() throws RecognitionException {
        try {
            int _type = BETWEEN;
            // Cubrid.g:19:9: ( 'BETWEEN' )
            // Cubrid.g:19:11: 'BETWEEN'
            {
            match("BETWEEN"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end BETWEEN

    // $ANTLR start BIT
    public final void mBIT() throws RecognitionException {
        try {
            int _type = BIT;
            // Cubrid.g:20:5: ( 'BIT' )
            // Cubrid.g:20:7: 'BIT'
            {
            match("BIT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end BIT

    // $ANTLR start BY
    public final void mBY() throws RecognitionException {
        try {
            int _type = BY;
            // Cubrid.g:21:4: ( 'BY' )
            // Cubrid.g:21:6: 'BY'
            {
            match("BY"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end BY

    // $ANTLR start CALL
    public final void mCALL() throws RecognitionException {
        try {
            int _type = CALL;
            // Cubrid.g:22:6: ( 'CALL' )
            // Cubrid.g:22:8: 'CALL'
            {
            match("CALL"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end CALL

    // $ANTLR start CASE
    public final void mCASE() throws RecognitionException {
        try {
            int _type = CASE;
            // Cubrid.g:23:6: ( 'CASE' )
            // Cubrid.g:23:8: 'CASE'
            {
            match("CASE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end CASE

    // $ANTLR start CACHE
    public final void mCACHE() throws RecognitionException {
        try {
            int _type = CACHE;
            // Cubrid.g:24:7: ( 'CACHE' )
            // Cubrid.g:24:9: 'CACHE'
            {
            match("CACHE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end CACHE

    // $ANTLR start CASCADE
    public final void mCASCADE() throws RecognitionException {
        try {
            int _type = CASCADE;
            // Cubrid.g:25:9: ( 'CASCADE' )
            // Cubrid.g:25:11: 'CASCADE'
            {
            match("CASCADE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end CASCADE

    // $ANTLR start CHANGE
    public final void mCHANGE() throws RecognitionException {
        try {
            int _type = CHANGE;
            // Cubrid.g:26:8: ( 'CHANGE' )
            // Cubrid.g:26:10: 'CHANGE'
            {
            match("CHANGE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end CHANGE

    // $ANTLR start CHAR
    public final void mCHAR() throws RecognitionException {
        try {
            int _type = CHAR;
            // Cubrid.g:27:6: ( 'CHAR' )
            // Cubrid.g:27:8: 'CHAR'
            {
            match("CHAR"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end CHAR

    // $ANTLR start CHARACTER
    public final void mCHARACTER() throws RecognitionException {
        try {
            int _type = CHARACTER;
            // Cubrid.g:28:11: ( 'CHARACTER' )
            // Cubrid.g:28:13: 'CHARACTER'
            {
            match("CHARACTER"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end CHARACTER

    // $ANTLR start CHECK
    public final void mCHECK() throws RecognitionException {
        try {
            int _type = CHECK;
            // Cubrid.g:29:7: ( 'CHECK' )
            // Cubrid.g:29:9: 'CHECK'
            {
            match("CHECK"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end CHECK

    // $ANTLR start CLASS
    public final void mCLASS() throws RecognitionException {
        try {
            int _type = CLASS;
            // Cubrid.g:30:7: ( 'CLASS' )
            // Cubrid.g:30:9: 'CLASS'
            {
            match("CLASS"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end CLASS

    // $ANTLR start COMMIT
    public final void mCOMMIT() throws RecognitionException {
        try {
            int _type = COMMIT;
            // Cubrid.g:31:8: ( 'COMMIT' )
            // Cubrid.g:31:10: 'COMMIT'
            {
            match("COMMIT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end COMMIT

    // $ANTLR start CONSTRAINT
    public final void mCONSTRAINT() throws RecognitionException {
        try {
            int _type = CONSTRAINT;
            // Cubrid.g:32:12: ( 'CONSTRAINT' )
            // Cubrid.g:32:14: 'CONSTRAINT'
            {
            match("CONSTRAINT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end CONSTRAINT

    // $ANTLR start CREATE
    public final void mCREATE() throws RecognitionException {
        try {
            int _type = CREATE;
            // Cubrid.g:33:8: ( 'CREATE' )
            // Cubrid.g:33:10: 'CREATE'
            {
            match("CREATE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end CREATE

    // $ANTLR start DATE
    public final void mDATE() throws RecognitionException {
        try {
            int _type = DATE;
            // Cubrid.g:34:6: ( 'DATE' )
            // Cubrid.g:34:8: 'DATE'
            {
            match("DATE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end DATE

    // $ANTLR start DECIMAL
    public final void mDECIMAL() throws RecognitionException {
        try {
            int _type = DECIMAL;
            // Cubrid.g:35:9: ( 'DECIMAL' )
            // Cubrid.g:35:11: 'DECIMAL'
            {
            match("DECIMAL"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end DECIMAL

    // $ANTLR start DEFERRED
    public final void mDEFERRED() throws RecognitionException {
        try {
            int _type = DEFERRED;
            // Cubrid.g:36:10: ( 'DEFERRED' )
            // Cubrid.g:36:12: 'DEFERRED'
            {
            match("DEFERRED"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end DEFERRED

    // $ANTLR start DESC
    public final void mDESC() throws RecognitionException {
        try {
            int _type = DESC;
            // Cubrid.g:37:6: ( 'DESC' )
            // Cubrid.g:37:8: 'DESC'
            {
            match("DESC"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end DESC

    // $ANTLR start DEFAULT
    public final void mDEFAULT() throws RecognitionException {
        try {
            int _type = DEFAULT;
            // Cubrid.g:38:9: ( 'DEFAULT' )
            // Cubrid.g:38:11: 'DEFAULT'
            {
            match("DEFAULT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end DEFAULT

    // $ANTLR start DELETE
    public final void mDELETE() throws RecognitionException {
        try {
            int _type = DELETE;
            // Cubrid.g:39:8: ( 'DELETE' )
            // Cubrid.g:39:10: 'DELETE'
            {
            match("DELETE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end DELETE

    // $ANTLR start DIFFERENCE
    public final void mDIFFERENCE() throws RecognitionException {
        try {
            int _type = DIFFERENCE;
            // Cubrid.g:40:12: ( 'DIFFERENCE' )
            // Cubrid.g:40:14: 'DIFFERENCE'
            {
            match("DIFFERENCE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end DIFFERENCE

    // $ANTLR start DISTINCT
    public final void mDISTINCT() throws RecognitionException {
        try {
            int _type = DISTINCT;
            // Cubrid.g:41:10: ( 'DISTINCT' )
            // Cubrid.g:41:12: 'DISTINCT'
            {
            match("DISTINCT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end DISTINCT

    // $ANTLR start DOUBLE
    public final void mDOUBLE() throws RecognitionException {
        try {
            int _type = DOUBLE;
            // Cubrid.g:42:8: ( 'DOUBLE' )
            // Cubrid.g:42:10: 'DOUBLE'
            {
            match("DOUBLE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end DOUBLE

    // $ANTLR start DROP
    public final void mDROP() throws RecognitionException {
        try {
            int _type = DROP;
            // Cubrid.g:43:6: ( 'DROP' )
            // Cubrid.g:43:8: 'DROP'
            {
            match("DROP"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end DROP

    // $ANTLR start ELSE
    public final void mELSE() throws RecognitionException {
        try {
            int _type = ELSE;
            // Cubrid.g:44:6: ( 'ELSE' )
            // Cubrid.g:44:8: 'ELSE'
            {
            match("ELSE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end ELSE

    // $ANTLR start END_STRING
    public final void mEND_STRING() throws RecognitionException {
        try {
            int _type = END_STRING;
            // Cubrid.g:45:12: ( 'END' )
            // Cubrid.g:45:14: 'END'
            {
            match("END"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end END_STRING

    // $ANTLR start EXCEPT
    public final void mEXCEPT() throws RecognitionException {
        try {
            int _type = EXCEPT;
            // Cubrid.g:46:8: ( 'EXCET' )
            // Cubrid.g:46:10: 'EXCET'
            {
            match("EXCET"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end EXCEPT

    // $ANTLR start EXISTS
    public final void mEXISTS() throws RecognitionException {
        try {
            int _type = EXISTS;
            // Cubrid.g:47:8: ( 'EXISTS' )
            // Cubrid.g:47:10: 'EXISTS'
            {
            match("EXISTS"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end EXISTS

    // $ANTLR start FILE
    public final void mFILE() throws RecognitionException {
        try {
            int _type = FILE;
            // Cubrid.g:48:6: ( 'FILE' )
            // Cubrid.g:48:8: 'FILE'
            {
            match("FILE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end FILE

    // $ANTLR start FLOAT
    public final void mFLOAT() throws RecognitionException {
        try {
            int _type = FLOAT;
            // Cubrid.g:49:7: ( 'FLOAT' )
            // Cubrid.g:49:9: 'FLOAT'
            {
            match("FLOAT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end FLOAT

    // $ANTLR start FOREIGN
    public final void mFOREIGN() throws RecognitionException {
        try {
            int _type = FOREIGN;
            // Cubrid.g:50:9: ( 'FOREIGN' )
            // Cubrid.g:50:11: 'FOREIGN'
            {
            match("FOREIGN"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end FOREIGN

    // $ANTLR start FROM
    public final void mFROM() throws RecognitionException {
        try {
            int _type = FROM;
            // Cubrid.g:51:6: ( 'FROM' )
            // Cubrid.g:51:8: 'FROM'
            {
            match("FROM"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end FROM

    // $ANTLR start FUNCTION
    public final void mFUNCTION() throws RecognitionException {
        try {
            int _type = FUNCTION;
            // Cubrid.g:52:10: ( 'FUNCTION' )
            // Cubrid.g:52:12: 'FUNCTION'
            {
            match("FUNCTION"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end FUNCTION

    // $ANTLR start GROUP
    public final void mGROUP() throws RecognitionException {
        try {
            int _type = GROUP;
            // Cubrid.g:53:7: ( 'GROUP' )
            // Cubrid.g:53:9: 'GROUP'
            {
            match("GROUP"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end GROUP

    // $ANTLR start HAVING
    public final void mHAVING() throws RecognitionException {
        try {
            int _type = HAVING;
            // Cubrid.g:54:8: ( 'HAVING' )
            // Cubrid.g:54:10: 'HAVING'
            {
            match("HAVING"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end HAVING

    // $ANTLR start IN
    public final void mIN() throws RecognitionException {
        try {
            int _type = IN;
            // Cubrid.g:55:4: ( 'IN' )
            // Cubrid.g:55:6: 'IN'
            {
            match("IN"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end IN

    // $ANTLR start INDEX
    public final void mINDEX() throws RecognitionException {
        try {
            int _type = INDEX;
            // Cubrid.g:56:7: ( 'INDEX' )
            // Cubrid.g:56:9: 'INDEX'
            {
            match("INDEX"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end INDEX

    // $ANTLR start INHERIT
    public final void mINHERIT() throws RecognitionException {
        try {
            int _type = INHERIT;
            // Cubrid.g:57:9: ( 'INHERIT' )
            // Cubrid.g:57:11: 'INHERIT'
            {
            match("INHERIT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end INHERIT

    // $ANTLR start INNER
    public final void mINNER() throws RecognitionException {
        try {
            int _type = INNER;
            // Cubrid.g:58:7: ( 'INNER' )
            // Cubrid.g:58:9: 'INNER'
            {
            match("INNER"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end INNER

    // $ANTLR start INSERT
    public final void mINSERT() throws RecognitionException {
        try {
            int _type = INSERT;
            // Cubrid.g:59:8: ( 'INSERT' )
            // Cubrid.g:59:10: 'INSERT'
            {
            match("INSERT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end INSERT

    // $ANTLR start INT
    public final void mINT() throws RecognitionException {
        try {
            int _type = INT;
            // Cubrid.g:60:5: ( 'INT' )
            // Cubrid.g:60:7: 'INT'
            {
            match("INT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end INT

    // $ANTLR start INTEGER
    public final void mINTEGER() throws RecognitionException {
        try {
            int _type = INTEGER;
            // Cubrid.g:61:9: ( 'INTEGER' )
            // Cubrid.g:61:11: 'INTEGER'
            {
            match("INTEGER"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end INTEGER

    // $ANTLR start INTERSECTION
    public final void mINTERSECTION() throws RecognitionException {
        try {
            int _type = INTERSECTION;
            // Cubrid.g:62:14: ( 'INTERSECTION' )
            // Cubrid.g:62:16: 'INTERSECTION'
            {
            match("INTERSECTION"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end INTERSECTION

    // $ANTLR start INTO
    public final void mINTO() throws RecognitionException {
        try {
            int _type = INTO;
            // Cubrid.g:63:6: ( 'INTO' )
            // Cubrid.g:63:8: 'INTO'
            {
            match("INTO"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end INTO

    // $ANTLR start IS
    public final void mIS() throws RecognitionException {
        try {
            int _type = IS;
            // Cubrid.g:64:4: ( 'IS' )
            // Cubrid.g:64:6: 'IS'
            {
            match("IS"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end IS

    // $ANTLR start JOIN
    public final void mJOIN() throws RecognitionException {
        try {
            int _type = JOIN;
            // Cubrid.g:65:6: ( 'JOIN' )
            // Cubrid.g:65:8: 'JOIN'
            {
            match("JOIN"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end JOIN

    // $ANTLR start KEY
    public final void mKEY() throws RecognitionException {
        try {
            int _type = KEY;
            // Cubrid.g:66:5: ( 'KEY' )
            // Cubrid.g:66:7: 'KEY'
            {
            match("KEY"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end KEY

    // $ANTLR start LIKE
    public final void mLIKE() throws RecognitionException {
        try {
            int _type = LIKE;
            // Cubrid.g:67:6: ( 'LIKE' )
            // Cubrid.g:67:8: 'LIKE'
            {
            match("LIKE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end LIKE

    // $ANTLR start LIST
    public final void mLIST() throws RecognitionException {
        try {
            int _type = LIST;
            // Cubrid.g:68:6: ( 'LIST' )
            // Cubrid.g:68:8: 'LIST'
            {
            match("LIST"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end LIST

    // $ANTLR start LEFT
    public final void mLEFT() throws RecognitionException {
        try {
            int _type = LEFT;
            // Cubrid.g:69:6: ( 'LEFT' )
            // Cubrid.g:69:8: 'LEFT'
            {
            match("LEFT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end LEFT

    // $ANTLR start METHOD
    public final void mMETHOD() throws RecognitionException {
        try {
            int _type = METHOD;
            // Cubrid.g:70:8: ( 'METHOD' )
            // Cubrid.g:70:10: 'METHOD'
            {
            match("METHOD"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end METHOD

    // $ANTLR start MONETARY
    public final void mMONETARY() throws RecognitionException {
        try {
            int _type = MONETARY;
            // Cubrid.g:71:10: ( 'MONETARY' )
            // Cubrid.g:71:12: 'MONETARY'
            {
            match("MONETARY"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end MONETARY

    // $ANTLR start MULTISET
    public final void mMULTISET() throws RecognitionException {
        try {
            int _type = MULTISET;
            // Cubrid.g:72:10: ( 'MULTISET' )
            // Cubrid.g:72:12: 'MULTISET'
            {
            match("MULTISET"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end MULTISET

    // $ANTLR start MULTISET_OF
    public final void mMULTISET_OF() throws RecognitionException {
        try {
            int _type = MULTISET_OF;
            // Cubrid.g:73:13: ( 'MULTISET_OF' )
            // Cubrid.g:73:15: 'MULTISET_OF'
            {
            match("MULTISET_OF"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end MULTISET_OF

    // $ANTLR start NCHAR
    public final void mNCHAR() throws RecognitionException {
        try {
            int _type = NCHAR;
            // Cubrid.g:74:7: ( 'NCHAR' )
            // Cubrid.g:74:9: 'NCHAR'
            {
            match("NCHAR"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end NCHAR

    // $ANTLR start NO
    public final void mNO() throws RecognitionException {
        try {
            int _type = NO;
            // Cubrid.g:75:4: ( 'NO' )
            // Cubrid.g:75:6: 'NO'
            {
            match("NO"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end NO

    // $ANTLR start NOT
    public final void mNOT() throws RecognitionException {
        try {
            int _type = NOT;
            // Cubrid.g:76:5: ( 'NOT' )
            // Cubrid.g:76:7: 'NOT'
            {
            match("NOT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end NOT

    // $ANTLR start NULL
    public final void mNULL() throws RecognitionException {
        try {
            int _type = NULL;
            // Cubrid.g:77:6: ( 'NULL' )
            // Cubrid.g:77:8: 'NULL'
            {
            match("NULL"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end NULL

    // $ANTLR start NUMERIC
    public final void mNUMERIC() throws RecognitionException {
        try {
            int _type = NUMERIC;
            // Cubrid.g:78:9: ( 'NUMERIC' )
            // Cubrid.g:78:11: 'NUMERIC'
            {
            match("NUMERIC"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end NUMERIC

    // $ANTLR start OBJECT
    public final void mOBJECT() throws RecognitionException {
        try {
            int _type = OBJECT;
            // Cubrid.g:79:8: ( 'OBJECT' )
            // Cubrid.g:79:10: 'OBJECT'
            {
            match("OBJECT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end OBJECT

    // $ANTLR start OF
    public final void mOF() throws RecognitionException {
        try {
            int _type = OF;
            // Cubrid.g:80:4: ( 'OF' )
            // Cubrid.g:80:6: 'OF'
            {
            match("OF"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end OF

    // $ANTLR start OFF
    public final void mOFF() throws RecognitionException {
        try {
            int _type = OFF;
            // Cubrid.g:81:5: ( 'OFF' )
            // Cubrid.g:81:7: 'OFF'
            {
            match("OFF"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end OFF

    // $ANTLR start ON
    public final void mON() throws RecognitionException {
        try {
            int _type = ON;
            // Cubrid.g:82:4: ( 'ON' )
            // Cubrid.g:82:6: 'ON'
            {
            match("ON"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end ON

    // $ANTLR start ONLY
    public final void mONLY() throws RecognitionException {
        try {
            int _type = ONLY;
            // Cubrid.g:83:6: ( 'ONLY' )
            // Cubrid.g:83:8: 'ONLY'
            {
            match("ONLY"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end ONLY

    // $ANTLR start OPTION
    public final void mOPTION() throws RecognitionException {
        try {
            int _type = OPTION;
            // Cubrid.g:84:8: ( 'OPTION' )
            // Cubrid.g:84:10: 'OPTION'
            {
            match("OPTION"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end OPTION

    // $ANTLR start OR
    public final void mOR() throws RecognitionException {
        try {
            int _type = OR;
            // Cubrid.g:85:4: ( 'OR' )
            // Cubrid.g:85:6: 'OR'
            {
            match("OR"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end OR

    // $ANTLR start ORDER
    public final void mORDER() throws RecognitionException {
        try {
            int _type = ORDER;
            // Cubrid.g:86:7: ( 'ORDER' )
            // Cubrid.g:86:9: 'ORDER'
            {
            match("ORDER"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end ORDER

    // $ANTLR start OUTER
    public final void mOUTER() throws RecognitionException {
        try {
            int _type = OUTER;
            // Cubrid.g:87:7: ( 'OUTER' )
            // Cubrid.g:87:9: 'OUTER'
            {
            match("OUTER"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end OUTER

    // $ANTLR start PM
    public final void mPM() throws RecognitionException {
        try {
            int _type = PM;
            // Cubrid.g:88:4: ( 'PM' )
            // Cubrid.g:88:6: 'PM'
            {
            match("PM"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end PM

    // $ANTLR start PRECISION
    public final void mPRECISION() throws RecognitionException {
        try {
            int _type = PRECISION;
            // Cubrid.g:89:11: ( 'PRECISION' )
            // Cubrid.g:89:13: 'PRECISION'
            {
            match("PRECISION"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end PRECISION

    // $ANTLR start PRIMARY
    public final void mPRIMARY() throws RecognitionException {
        try {
            int _type = PRIMARY;
            // Cubrid.g:90:9: ( 'PRIMARY' )
            // Cubrid.g:90:11: 'PRIMARY'
            {
            match("PRIMARY"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end PRIMARY

    // $ANTLR start QUERY
    public final void mQUERY() throws RecognitionException {
        try {
            int _type = QUERY;
            // Cubrid.g:91:7: ( 'QUERY' )
            // Cubrid.g:91:9: 'QUERY'
            {
            match("QUERY"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end QUERY

    // $ANTLR start REAL
    public final void mREAL() throws RecognitionException {
        try {
            int _type = REAL;
            // Cubrid.g:92:6: ( 'REAL' )
            // Cubrid.g:92:8: 'REAL'
            {
            match("REAL"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end REAL

    // $ANTLR start REFERENCES
    public final void mREFERENCES() throws RecognitionException {
        try {
            int _type = REFERENCES;
            // Cubrid.g:93:12: ( 'REFERENCES' )
            // Cubrid.g:93:14: 'REFERENCES'
            {
            match("REFERENCES"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end REFERENCES

    // $ANTLR start RENAME
    public final void mRENAME() throws RecognitionException {
        try {
            int _type = RENAME;
            // Cubrid.g:94:8: ( 'RENAME' )
            // Cubrid.g:94:10: 'RENAME'
            {
            match("RENAME"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end RENAME

    // $ANTLR start RESTRICT
    public final void mRESTRICT() throws RecognitionException {
        try {
            int _type = RESTRICT;
            // Cubrid.g:95:10: ( 'RESTRICT' )
            // Cubrid.g:95:12: 'RESTRICT'
            {
            match("RESTRICT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end RESTRICT

    // $ANTLR start RIGHT
    public final void mRIGHT() throws RecognitionException {
        try {
            int _type = RIGHT;
            // Cubrid.g:96:7: ( 'RIGHT' )
            // Cubrid.g:96:9: 'RIGHT'
            {
            match("RIGHT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end RIGHT

    // $ANTLR start ROLLBACK
    public final void mROLLBACK() throws RecognitionException {
        try {
            int _type = ROLLBACK;
            // Cubrid.g:97:10: ( 'ROLLBACK' )
            // Cubrid.g:97:12: 'ROLLBACK'
            {
            match("ROLLBACK"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end ROLLBACK

    // $ANTLR start SELECT
    public final void mSELECT() throws RecognitionException {
        try {
            int _type = SELECT;
            // Cubrid.g:98:8: ( 'SELECT' )
            // Cubrid.g:98:10: 'SELECT'
            {
            match("SELECT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end SELECT

    // $ANTLR start SEQUENCE
    public final void mSEQUENCE() throws RecognitionException {
        try {
            int _type = SEQUENCE;
            // Cubrid.g:99:10: ( 'SEQUENCE' )
            // Cubrid.g:99:12: 'SEQUENCE'
            {
            match("SEQUENCE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end SEQUENCE

    // $ANTLR start SEQUENCE_OF
    public final void mSEQUENCE_OF() throws RecognitionException {
        try {
            int _type = SEQUENCE_OF;
            // Cubrid.g:100:13: ( 'SEQUENCE_OF' )
            // Cubrid.g:100:15: 'SEQUENCE_OF'
            {
            match("SEQUENCE_OF"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end SEQUENCE_OF

    // $ANTLR start SET
    public final void mSET() throws RecognitionException {
        try {
            int _type = SET;
            // Cubrid.g:101:5: ( 'SET' )
            // Cubrid.g:101:7: 'SET'
            {
            match("SET"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end SET

    // $ANTLR start SHARE
    public final void mSHARE() throws RecognitionException {
        try {
            int _type = SHARE;
            // Cubrid.g:102:7: ( 'SHARE' )
            // Cubrid.g:102:9: 'SHARE'
            {
            match("SHARE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end SHARE

    // $ANTLR start SMALLINT
    public final void mSMALLINT() throws RecognitionException {
        try {
            int _type = SMALLINT;
            // Cubrid.g:103:10: ( 'SMALLINT' )
            // Cubrid.g:103:12: 'SMALLINT'
            {
            match("SMALLINT"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end SMALLINT

    // $ANTLR start REVERSE
    public final void mREVERSE() throws RecognitionException {
        try {
            int _type = REVERSE;
            // Cubrid.g:104:9: ( 'REVERSE' )
            // Cubrid.g:104:11: 'REVERSE'
            {
            match("REVERSE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end REVERSE

    // $ANTLR start STRING_STR
    public final void mSTRING_STR() throws RecognitionException {
        try {
            int _type = STRING_STR;
            // Cubrid.g:105:12: ( 'STRING' )
            // Cubrid.g:105:14: 'STRING'
            {
            match("STRING"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end STRING_STR

    // $ANTLR start SUBCLASS
    public final void mSUBCLASS() throws RecognitionException {
        try {
            int _type = SUBCLASS;
            // Cubrid.g:106:10: ( 'SUBCLASS' )
            // Cubrid.g:106:12: 'SUBCLASS'
            {
            match("SUBCLASS"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end SUBCLASS

    // $ANTLR start SUPERCLASS
    public final void mSUPERCLASS() throws RecognitionException {
        try {
            int _type = SUPERCLASS;
            // Cubrid.g:107:12: ( 'SUPERCLASS' )
            // Cubrid.g:107:14: 'SUPERCLASS'
            {
            match("SUPERCLASS"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end SUPERCLASS

    // $ANTLR start TABLE
    public final void mTABLE() throws RecognitionException {
        try {
            int _type = TABLE;
            // Cubrid.g:108:7: ( 'TABLE' )
            // Cubrid.g:108:9: 'TABLE'
            {
            match("TABLE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end TABLE

    // $ANTLR start TIME
    public final void mTIME() throws RecognitionException {
        try {
            int _type = TIME;
            // Cubrid.g:109:6: ( 'TIME' )
            // Cubrid.g:109:8: 'TIME'
            {
            match("TIME"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end TIME

    // $ANTLR start TIMESTAMP
    public final void mTIMESTAMP() throws RecognitionException {
        try {
            int _type = TIMESTAMP;
            // Cubrid.g:110:11: ( 'TIMESTAMP' )
            // Cubrid.g:110:13: 'TIMESTAMP'
            {
            match("TIMESTAMP"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end TIMESTAMP

    // $ANTLR start THEN
    public final void mTHEN() throws RecognitionException {
        try {
            int _type = THEN;
            // Cubrid.g:111:6: ( 'THEN' )
            // Cubrid.g:111:8: 'THEN'
            {
            match("THEN"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end THEN

    // $ANTLR start TRIGGER
    public final void mTRIGGER() throws RecognitionException {
        try {
            int _type = TRIGGER;
            // Cubrid.g:112:9: ( 'TRIGGER' )
            // Cubrid.g:112:11: 'TRIGGER'
            {
            match("TRIGGER"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end TRIGGER

    // $ANTLR start TRIGGERS
    public final void mTRIGGERS() throws RecognitionException {
        try {
            int _type = TRIGGERS;
            // Cubrid.g:113:10: ( 'TRIGGERS' )
            // Cubrid.g:113:12: 'TRIGGERS'
            {
            match("TRIGGERS"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end TRIGGERS

    // $ANTLR start TO
    public final void mTO() throws RecognitionException {
        try {
            int _type = TO;
            // Cubrid.g:114:4: ( 'TO' )
            // Cubrid.g:114:6: 'TO'
            {
            match("TO"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end TO

    // $ANTLR start VALUES
    public final void mVALUES() throws RecognitionException {
        try {
            int _type = VALUES;
            // Cubrid.g:115:8: ( 'VALUES' )
            // Cubrid.g:115:10: 'VALUES'
            {
            match("VALUES"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end VALUES

    // $ANTLR start UNION
    public final void mUNION() throws RecognitionException {
        try {
            int _type = UNION;
            // Cubrid.g:116:7: ( 'UNION' )
            // Cubrid.g:116:9: 'UNION'
            {
            match("UNION"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end UNION

    // $ANTLR start UNIQUE
    public final void mUNIQUE() throws RecognitionException {
        try {
            int _type = UNIQUE;
            // Cubrid.g:117:8: ( 'UNIQUE' )
            // Cubrid.g:117:10: 'UNIQUE'
            {
            match("UNIQUE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end UNIQUE

    // $ANTLR start UPDATE
    public final void mUPDATE() throws RecognitionException {
        try {
            int _type = UPDATE;
            // Cubrid.g:118:8: ( 'UPDATE' )
            // Cubrid.g:118:10: 'UPDATE'
            {
            match("UPDATE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end UPDATE

    // $ANTLR start USING
    public final void mUSING() throws RecognitionException {
        try {
            int _type = USING;
            // Cubrid.g:119:7: ( 'USING' )
            // Cubrid.g:119:9: 'USING'
            {
            match("USING"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end USING

    // $ANTLR start VARCHAR
    public final void mVARCHAR() throws RecognitionException {
        try {
            int _type = VARCHAR;
            // Cubrid.g:120:9: ( 'VARCHAR' )
            // Cubrid.g:120:11: 'VARCHAR'
            {
            match("VARCHAR"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end VARCHAR

    // $ANTLR start VARYING
    public final void mVARYING() throws RecognitionException {
        try {
            int _type = VARYING;
            // Cubrid.g:121:9: ( 'VARYING' )
            // Cubrid.g:121:11: 'VARYING'
            {
            match("VARYING"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end VARYING

    // $ANTLR start VCLASS
    public final void mVCLASS() throws RecognitionException {
        try {
            int _type = VCLASS;
            // Cubrid.g:122:8: ( 'VCLASS' )
            // Cubrid.g:122:10: 'VCLASS'
            {
            match("VCLASS"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end VCLASS

    // $ANTLR start VIEW
    public final void mVIEW() throws RecognitionException {
        try {
            int _type = VIEW;
            // Cubrid.g:123:6: ( 'VIEW' )
            // Cubrid.g:123:8: 'VIEW'
            {
            match("VIEW"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end VIEW

    // $ANTLR start WHEN
    public final void mWHEN() throws RecognitionException {
        try {
            int _type = WHEN;
            // Cubrid.g:124:6: ( 'WHEN' )
            // Cubrid.g:124:8: 'WHEN'
            {
            match("WHEN"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end WHEN

    // $ANTLR start WHERE
    public final void mWHERE() throws RecognitionException {
        try {
            int _type = WHERE;
            // Cubrid.g:125:7: ( 'WHERE' )
            // Cubrid.g:125:9: 'WHERE'
            {
            match("WHERE"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end WHERE

    // $ANTLR start WITH
    public final void mWITH() throws RecognitionException {
        try {
            int _type = WITH;
            // Cubrid.g:126:6: ( 'WITH' )
            // Cubrid.g:126:8: 'WITH'
            {
            match("WITH"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end WITH

    // $ANTLR start WORK
    public final void mWORK() throws RecognitionException {
        try {
            int _type = WORK;
            // Cubrid.g:127:6: ( 'WORK' )
            // Cubrid.g:127:8: 'WORK'
            {
            match("WORK"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end WORK

    // $ANTLR start END
    public final void mEND() throws RecognitionException {
        try {
            int _type = END;
            // Cubrid.g:128:5: ( ';' )
            // Cubrid.g:128:7: ';'
            {
            match(';'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end END

    // $ANTLR start COMMA
    public final void mCOMMA() throws RecognitionException {
        try {
            int _type = COMMA;
            // Cubrid.g:129:7: ( ',' )
            // Cubrid.g:129:9: ','
            {
            match(','); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end COMMA

    // $ANTLR start STAR
    public final void mSTAR() throws RecognitionException {
        try {
            int _type = STAR;
            // Cubrid.g:130:6: ( '*' )
            // Cubrid.g:130:8: '*'
            {
            match('*'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end STAR

    // $ANTLR start STARTBRACE
    public final void mSTARTBRACE() throws RecognitionException {
        try {
            int _type = STARTBRACE;
            // Cubrid.g:131:12: ( '{' )
            // Cubrid.g:131:14: '{'
            {
            match('{'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end STARTBRACE

    // $ANTLR start ENDBRACE
    public final void mENDBRACE() throws RecognitionException {
        try {
            int _type = ENDBRACE;
            // Cubrid.g:132:10: ( '}' )
            // Cubrid.g:132:12: '}'
            {
            match('}'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end ENDBRACE

    // $ANTLR start DOT
    public final void mDOT() throws RecognitionException {
        try {
            int _type = DOT;
            // Cubrid.g:133:5: ( '.' )
            // Cubrid.g:133:7: '.'
            {
            match('.'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end DOT

    // $ANTLR start QUOTA
    public final void mQUOTA() throws RecognitionException {
        try {
            int _type = QUOTA;
            // Cubrid.g:134:7: ( '\\'' )
            // Cubrid.g:134:9: '\\''
            {
            match('\''); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end QUOTA

    // $ANTLR start DBQUOTA
    public final void mDBQUOTA() throws RecognitionException {
        try {
            int _type = DBQUOTA;
            // Cubrid.g:135:9: ( '\"' )
            // Cubrid.g:135:11: '\"'
            {
            match('\"'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end DBQUOTA

    // $ANTLR start EQUAL
    public final void mEQUAL() throws RecognitionException {
        try {
            int _type = EQUAL;
            // Cubrid.g:136:7: ( '=' )
            // Cubrid.g:136:9: '='
            {
            match('='); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end EQUAL

    // $ANTLR start CONNECT
    public final void mCONNECT() throws RecognitionException {
        try {
            int _type = CONNECT;
            // Cubrid.g:137:9: ( '||' )
            // Cubrid.g:137:11: '||'
            {
            match("||"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end CONNECT

    // $ANTLR start DOLLAR
    public final void mDOLLAR() throws RecognitionException {
        try {
            int _type = DOLLAR;
            // Cubrid.g:138:8: ( '$' )
            // Cubrid.g:138:10: '$'
            {
            match('$'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end DOLLAR

    // $ANTLR start Q_MARK
    public final void mQ_MARK() throws RecognitionException {
        try {
            int _type = Q_MARK;
            // Cubrid.g:139:8: ( '\\u003F' )
            // Cubrid.g:139:10: '\\u003F'
            {
            match('?'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end Q_MARK

    // $ANTLR start T156
    public final void mT156() throws RecognitionException {
        try {
            int _type = T156;
            // Cubrid.g:140:6: ( '+=' )
            // Cubrid.g:140:8: '+='
            {
            match("+="); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T156

    // $ANTLR start T157
    public final void mT157() throws RecognitionException {
        try {
            int _type = T157;
            // Cubrid.g:141:6: ( '-=' )
            // Cubrid.g:141:8: '-='
            {
            match("-="); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T157

    // $ANTLR start T158
    public final void mT158() throws RecognitionException {
        try {
            int _type = T158;
            // Cubrid.g:142:6: ( '*=' )
            // Cubrid.g:142:8: '*='
            {
            match("*="); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T158

    // $ANTLR start T159
    public final void mT159() throws RecognitionException {
        try {
            int _type = T159;
            // Cubrid.g:143:6: ( '/=' )
            // Cubrid.g:143:8: '/='
            {
            match("/="); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T159

    // $ANTLR start T160
    public final void mT160() throws RecognitionException {
        try {
            int _type = T160;
            // Cubrid.g:144:6: ( '&=' )
            // Cubrid.g:144:8: '&='
            {
            match("&="); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T160

    // $ANTLR start T161
    public final void mT161() throws RecognitionException {
        try {
            int _type = T161;
            // Cubrid.g:145:6: ( '|=' )
            // Cubrid.g:145:8: '|='
            {
            match("|="); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T161

    // $ANTLR start T162
    public final void mT162() throws RecognitionException {
        try {
            int _type = T162;
            // Cubrid.g:146:6: ( '^=' )
            // Cubrid.g:146:8: '^='
            {
            match("^="); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T162

    // $ANTLR start T163
    public final void mT163() throws RecognitionException {
        try {
            int _type = T163;
            // Cubrid.g:147:6: ( '%=' )
            // Cubrid.g:147:8: '%='
            {
            match("%="); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T163

    // $ANTLR start T164
    public final void mT164() throws RecognitionException {
        try {
            int _type = T164;
            // Cubrid.g:148:6: ( '|' )
            // Cubrid.g:148:8: '|'
            {
            match('|'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T164

    // $ANTLR start T165
    public final void mT165() throws RecognitionException {
        try {
            int _type = T165;
            // Cubrid.g:149:6: ( '&' )
            // Cubrid.g:149:8: '&'
            {
            match('&'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T165

    // $ANTLR start T166
    public final void mT166() throws RecognitionException {
        try {
            int _type = T166;
            // Cubrid.g:150:6: ( '+' )
            // Cubrid.g:150:8: '+'
            {
            match('+'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T166

    // $ANTLR start T167
    public final void mT167() throws RecognitionException {
        try {
            int _type = T167;
            // Cubrid.g:151:6: ( '<>' )
            // Cubrid.g:151:8: '<>'
            {
            match("<>"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T167

    // $ANTLR start T168
    public final void mT168() throws RecognitionException {
        try {
            int _type = T168;
            // Cubrid.g:152:6: ( '<=' )
            // Cubrid.g:152:8: '<='
            {
            match("<="); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T168

    // $ANTLR start T169
    public final void mT169() throws RecognitionException {
        try {
            int _type = T169;
            // Cubrid.g:153:6: ( '>=' )
            // Cubrid.g:153:8: '>='
            {
            match(">="); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T169

    // $ANTLR start T170
    public final void mT170() throws RecognitionException {
        try {
            int _type = T170;
            // Cubrid.g:154:6: ( '<' )
            // Cubrid.g:154:8: '<'
            {
            match('<'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T170

    // $ANTLR start T171
    public final void mT171() throws RecognitionException {
        try {
            int _type = T171;
            // Cubrid.g:155:6: ( '>' )
            // Cubrid.g:155:8: '>'
            {
            match('>'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T171

    // $ANTLR start T172
    public final void mT172() throws RecognitionException {
        try {
            int _type = T172;
            // Cubrid.g:156:6: ( '-' )
            // Cubrid.g:156:8: '-'
            {
            match('-'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T172

    // $ANTLR start T173
    public final void mT173() throws RecognitionException {
        try {
            int _type = T173;
            // Cubrid.g:157:6: ( '/' )
            // Cubrid.g:157:8: '/'
            {
            match('/'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T173

    // $ANTLR start T174
    public final void mT174() throws RecognitionException {
        try {
            int _type = T174;
            // Cubrid.g:158:6: ( '%' )
            // Cubrid.g:158:8: '%'
            {
            match('%'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T174

    // $ANTLR start DATE_FORMAT
    public final void mDATE_FORMAT() throws RecognitionException {
        try {
            int _type = DATE_FORMAT;
            // Cubrid.g:811:12: ( ( '0' .. '9' ) ( '0' .. '2' ) '/' ( '0' .. '9' ) ( '0' .. '9' ) ( '/' ( '0' .. '9' ) ( '0' .. '9' ) ( '0' .. '9' ) ( '0' .. '9' ) )? )
            // Cubrid.g:812:2: ( '0' .. '9' ) ( '0' .. '2' ) '/' ( '0' .. '9' ) ( '0' .. '9' ) ( '/' ( '0' .. '9' ) ( '0' .. '9' ) ( '0' .. '9' ) ( '0' .. '9' ) )?
            {
            // Cubrid.g:812:2: ( '0' .. '9' )
            // Cubrid.g:812:3: '0' .. '9'
            {
            matchRange('0','9'); 

            }

            // Cubrid.g:812:12: ( '0' .. '2' )
            // Cubrid.g:812:13: '0' .. '2'
            {
            matchRange('0','2'); 

            }

            match('/'); 
            // Cubrid.g:812:27: ( '0' .. '9' )
            // Cubrid.g:812:28: '0' .. '9'
            {
            matchRange('0','9'); 

            }

            // Cubrid.g:812:37: ( '0' .. '9' )
            // Cubrid.g:812:38: '0' .. '9'
            {
            matchRange('0','9'); 

            }

            // Cubrid.g:812:48: ( '/' ( '0' .. '9' ) ( '0' .. '9' ) ( '0' .. '9' ) ( '0' .. '9' ) )?
            int alt1=2;
            int LA1_0 = input.LA(1);

            if ( (LA1_0=='/') ) {
                alt1=1;
            }
            switch (alt1) {
                case 1 :
                    // Cubrid.g:812:49: '/' ( '0' .. '9' ) ( '0' .. '9' ) ( '0' .. '9' ) ( '0' .. '9' )
                    {
                    match('/'); 
                    // Cubrid.g:812:53: ( '0' .. '9' )
                    // Cubrid.g:812:54: '0' .. '9'
                    {
                    matchRange('0','9'); 

                    }

                    // Cubrid.g:812:63: ( '0' .. '9' )
                    // Cubrid.g:812:64: '0' .. '9'
                    {
                    matchRange('0','9'); 

                    }

                    // Cubrid.g:812:73: ( '0' .. '9' )
                    // Cubrid.g:812:74: '0' .. '9'
                    {
                    matchRange('0','9'); 

                    }

                    // Cubrid.g:812:83: ( '0' .. '9' )
                    // Cubrid.g:812:84: '0' .. '9'
                    {
                    matchRange('0','9'); 

                    }


                    }
                    break;

            }


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end DATE_FORMAT

    // $ANTLR start TIME_FORMAT
    public final void mTIME_FORMAT() throws RecognitionException {
        try {
            int _type = TIME_FORMAT;
            // Cubrid.g:814:12: ( ( '0' .. '9' ) ( '0' .. '2' ) ':' ( '0' .. '9' ) ( '0' .. '9' ) ( ':' ( '0' .. '9' ) ( '0' .. '9' ) )? ( 'am' | 'pm' )? )
            // Cubrid.g:815:2: ( '0' .. '9' ) ( '0' .. '2' ) ':' ( '0' .. '9' ) ( '0' .. '9' ) ( ':' ( '0' .. '9' ) ( '0' .. '9' ) )? ( 'am' | 'pm' )?
            {
            // Cubrid.g:815:2: ( '0' .. '9' )
            // Cubrid.g:815:3: '0' .. '9'
            {
            matchRange('0','9'); 

            }

            // Cubrid.g:815:12: ( '0' .. '2' )
            // Cubrid.g:815:13: '0' .. '2'
            {
            matchRange('0','2'); 

            }

            match(':'); 
            // Cubrid.g:815:27: ( '0' .. '9' )
            // Cubrid.g:815:28: '0' .. '9'
            {
            matchRange('0','9'); 

            }

            // Cubrid.g:815:37: ( '0' .. '9' )
            // Cubrid.g:815:38: '0' .. '9'
            {
            matchRange('0','9'); 

            }

            // Cubrid.g:815:48: ( ':' ( '0' .. '9' ) ( '0' .. '9' ) )?
            int alt2=2;
            int LA2_0 = input.LA(1);

            if ( (LA2_0==':') ) {
                alt2=1;
            }
            switch (alt2) {
                case 1 :
                    // Cubrid.g:815:49: ':' ( '0' .. '9' ) ( '0' .. '9' )
                    {
                    match(':'); 
                    // Cubrid.g:815:53: ( '0' .. '9' )
                    // Cubrid.g:815:54: '0' .. '9'
                    {
                    matchRange('0','9'); 

                    }

                    // Cubrid.g:815:63: ( '0' .. '9' )
                    // Cubrid.g:815:64: '0' .. '9'
                    {
                    matchRange('0','9'); 

                    }


                    }
                    break;

            }

            // Cubrid.g:815:76: ( 'am' | 'pm' )?
            int alt3=3;
            int LA3_0 = input.LA(1);

            if ( (LA3_0=='a') ) {
                alt3=1;
            }
            else if ( (LA3_0=='p') ) {
                alt3=2;
            }
            switch (alt3) {
                case 1 :
                    // Cubrid.g:815:77: 'am'
                    {
                    match("am"); 


                    }
                    break;
                case 2 :
                    // Cubrid.g:815:84: 'pm'
                    {
                    match("pm"); 


                    }
                    break;

            }


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end TIME_FORMAT

    // $ANTLR start LENGTH
    public final void mLENGTH() throws RecognitionException {
        try {
            int _type = LENGTH;
            // Cubrid.g:830:7: ( STARTBRACKET ( '0' .. '9' )+ ENDBRACKET )
            // Cubrid.g:831:2: STARTBRACKET ( '0' .. '9' )+ ENDBRACKET
            {
            mSTARTBRACKET(); 
            // Cubrid.g:831:15: ( '0' .. '9' )+
            int cnt4=0;
            loop4:
            do {
                int alt4=2;
                int LA4_0 = input.LA(1);

                if ( ((LA4_0>='0' && LA4_0<='9')) ) {
                    alt4=1;
                }


                switch (alt4) {
            	case 1 :
            	    // Cubrid.g:831:17: '0' .. '9'
            	    {
            	    matchRange('0','9'); 

            	    }
            	    break;

            	default :
            	    if ( cnt4 >= 1 ) break loop4;
                        EarlyExitException eee =
                            new EarlyExitException(4, input);
                        throw eee;
                }
                cnt4++;
            } while (true);

            mENDBRACKET(); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end LENGTH

    // $ANTLR start DECIMALLITERAL
    public final void mDECIMALLITERAL() throws RecognitionException {
        try {
            int _type = DECIMALLITERAL;
            // Cubrid.g:1107:15: ( ( '0' .. '9' )+ )
            // Cubrid.g:1108:2: ( '0' .. '9' )+
            {
            // Cubrid.g:1108:2: ( '0' .. '9' )+
            int cnt5=0;
            loop5:
            do {
                int alt5=2;
                int LA5_0 = input.LA(1);

                if ( ((LA5_0>='0' && LA5_0<='9')) ) {
                    alt5=1;
                }


                switch (alt5) {
            	case 1 :
            	    // Cubrid.g:1108:2: '0' .. '9'
            	    {
            	    matchRange('0','9'); 

            	    }
            	    break;

            	default :
            	    if ( cnt5 >= 1 ) break loop5;
                        EarlyExitException eee =
                            new EarlyExitException(5, input);
                        throw eee;
                }
                cnt5++;
            } while (true);


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end DECIMALLITERAL

    // $ANTLR start STARTBRACKET
    public final void mSTARTBRACKET() throws RecognitionException {
        try {
            int _type = STARTBRACKET;
            // Cubrid.g:1136:13: ( '(' )
            // Cubrid.g:1136:15: '('
            {
            match('('); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end STARTBRACKET

    // $ANTLR start ENDBRACKET
    public final void mENDBRACKET() throws RecognitionException {
        try {
            int _type = ENDBRACKET;
            // Cubrid.g:1138:11: ( ')' )
            // Cubrid.g:1138:13: ')'
            {
            match(')'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end ENDBRACKET

    // $ANTLR start STRING
    public final void mSTRING() throws RecognitionException {
        try {
            int _type = STRING;
            // Cubrid.g:1150:8: ( '\\'' ( 'a' .. 'z' | 'A' .. 'Z' | '_' | '0' .. '9' | ' ' | '.' | ',' | '/' | '\\\\' | ':' | '-' | MARKS | KOREA | CHINESE | JAPAN )* '\\'' )
            // Cubrid.g:1150:10: '\\'' ( 'a' .. 'z' | 'A' .. 'Z' | '_' | '0' .. '9' | ' ' | '.' | ',' | '/' | '\\\\' | ':' | '-' | MARKS | KOREA | CHINESE | JAPAN )* '\\''
            {
            match('\''); 
            // Cubrid.g:1150:15: ( 'a' .. 'z' | 'A' .. 'Z' | '_' | '0' .. '9' | ' ' | '.' | ',' | '/' | '\\\\' | ':' | '-' | MARKS | KOREA | CHINESE | JAPAN )*
            loop6:
            do {
                int alt6=2;
                int LA6_0 = input.LA(1);

                if ( (LA6_0==' '||(LA6_0>='(' && LA6_0<=')')||(LA6_0>=',' && LA6_0<=':')||(LA6_0>='A' && LA6_0<=']')||LA6_0=='_'||(LA6_0>='a' && LA6_0<='{')||LA6_0=='}'||(LA6_0>='\u1100' && LA6_0<='\u11FF')||(LA6_0>='\u3040' && LA6_0<='\u31FF')||(LA6_0>='\u4E00' && LA6_0<='\u9FA5')||(LA6_0>='\uAC00' && LA6_0<='\uD7AF')) ) {
                    alt6=1;
                }


                switch (alt6) {
            	case 1 :
            	    // Cubrid.g:
            	    {
            	    if ( input.LA(1)==' '||(input.LA(1)>='(' && input.LA(1)<=')')||(input.LA(1)>=',' && input.LA(1)<=':')||(input.LA(1)>='A' && input.LA(1)<=']')||input.LA(1)=='_'||(input.LA(1)>='a' && input.LA(1)<='{')||input.LA(1)=='}'||(input.LA(1)>='\u1100' && input.LA(1)<='\u11FF')||(input.LA(1)>='\u3040' && input.LA(1)<='\u31FF')||(input.LA(1)>='\u4E00' && input.LA(1)<='\u9FA5')||(input.LA(1)>='\uAC00' && input.LA(1)<='\uD7AF') ) {
            	        input.consume();

            	    }
            	    else {
            	        MismatchedSetException mse =
            	            new MismatchedSetException(null,input);
            	        recover(mse);    throw mse;
            	    }


            	    }
            	    break;

            	default :
            	    break loop6;
                }
            } while (true);

            match('\''); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end STRING

    // $ANTLR start MARKS
    public final void mMARKS() throws RecognitionException {
        try {
            int _type = MARKS;
            // Cubrid.g:1152:7: ( '(' | ')' | '[' | ']' | '{' | '}' )
            // Cubrid.g:
            {
            if ( (input.LA(1)>='(' && input.LA(1)<=')')||input.LA(1)=='['||input.LA(1)==']'||input.LA(1)=='{'||input.LA(1)=='}' ) {
                input.consume();

            }
            else {
                MismatchedSetException mse =
                    new MismatchedSetException(null,input);
                recover(mse);    throw mse;
            }


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end MARKS

    // $ANTLR start KOREA
    public final void mKOREA() throws RecognitionException {
        try {
            int _type = KOREA;
            // Cubrid.g:1156:7: ( '\\uAC00' .. '\\uD7AF' | '\\u1100' .. '\\u11FF' | '\\u3130' .. '\\u318F' )
            // Cubrid.g:
            {
            if ( (input.LA(1)>='\u1100' && input.LA(1)<='\u11FF')||(input.LA(1)>='\u3130' && input.LA(1)<='\u318F')||(input.LA(1)>='\uAC00' && input.LA(1)<='\uD7AF') ) {
                input.consume();

            }
            else {
                MismatchedSetException mse =
                    new MismatchedSetException(null,input);
                recover(mse);    throw mse;
            }


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end KOREA

    // $ANTLR start CHINESE
    public final void mCHINESE() throws RecognitionException {
        try {
            int _type = CHINESE;
            // Cubrid.g:1158:9: ( '\\u4E00' .. '\\u9FA5' )
            // Cubrid.g:1158:11: '\\u4E00' .. '\\u9FA5'
            {
            matchRange('\u4E00','\u9FA5'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end CHINESE

    // $ANTLR start JAPAN
    public final void mJAPAN() throws RecognitionException {
        try {
            int _type = JAPAN;
            // Cubrid.g:1160:7: ( '\\u3040' .. '\\u31FF' )
            // Cubrid.g:1160:10: '\\u3040' .. '\\u31FF'
            {
            matchRange('\u3040','\u31FF'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end JAPAN

    // $ANTLR start COLUMN
    public final void mCOLUMN() throws RecognitionException {
        try {
            int _type = COLUMN;
            // Cubrid.g:1162:8: ( '\"' ( 'a' .. 'z' | 'A' .. 'Z' | '_' | '0' .. '9' | ' ' | '.' | ',' )* '\"' )
            // Cubrid.g:1162:10: '\"' ( 'a' .. 'z' | 'A' .. 'Z' | '_' | '0' .. '9' | ' ' | '.' | ',' )* '\"'
            {
            match('\"'); 
            // Cubrid.g:1162:14: ( 'a' .. 'z' | 'A' .. 'Z' | '_' | '0' .. '9' | ' ' | '.' | ',' )*
            loop7:
            do {
                int alt7=2;
                int LA7_0 = input.LA(1);

                if ( (LA7_0==' '||LA7_0==','||LA7_0=='.'||(LA7_0>='0' && LA7_0<='9')||(LA7_0>='A' && LA7_0<='Z')||LA7_0=='_'||(LA7_0>='a' && LA7_0<='z')) ) {
                    alt7=1;
                }


                switch (alt7) {
            	case 1 :
            	    // Cubrid.g:
            	    {
            	    if ( input.LA(1)==' '||input.LA(1)==','||input.LA(1)=='.'||(input.LA(1)>='0' && input.LA(1)<='9')||(input.LA(1)>='A' && input.LA(1)<='Z')||input.LA(1)=='_'||(input.LA(1)>='a' && input.LA(1)<='z') ) {
            	        input.consume();

            	    }
            	    else {
            	        MismatchedSetException mse =
            	            new MismatchedSetException(null,input);
            	        recover(mse);    throw mse;
            	    }


            	    }
            	    break;

            	default :
            	    break loop7;
                }
            } while (true);

            match('\"'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end COLUMN

    // $ANTLR start ID
    public final void mID() throws RecognitionException {
        try {
            int _type = ID;
            // Cubrid.g:1164:3: ( ( 'A' .. 'Z' | 'a' .. 'z' | '_' | '0' .. '9' )+ )
            // Cubrid.g:1164:5: ( 'A' .. 'Z' | 'a' .. 'z' | '_' | '0' .. '9' )+
            {
            // Cubrid.g:1164:5: ( 'A' .. 'Z' | 'a' .. 'z' | '_' | '0' .. '9' )+
            int cnt8=0;
            loop8:
            do {
                int alt8=2;
                int LA8_0 = input.LA(1);

                if ( ((LA8_0>='0' && LA8_0<='9')||(LA8_0>='A' && LA8_0<='Z')||LA8_0=='_'||(LA8_0>='a' && LA8_0<='z')) ) {
                    alt8=1;
                }


                switch (alt8) {
            	case 1 :
            	    // Cubrid.g:
            	    {
            	    if ( (input.LA(1)>='0' && input.LA(1)<='9')||(input.LA(1)>='A' && input.LA(1)<='Z')||input.LA(1)=='_'||(input.LA(1)>='a' && input.LA(1)<='z') ) {
            	        input.consume();

            	    }
            	    else {
            	        MismatchedSetException mse =
            	            new MismatchedSetException(null,input);
            	        recover(mse);    throw mse;
            	    }


            	    }
            	    break;

            	default :
            	    if ( cnt8 >= 1 ) break loop8;
                        EarlyExitException eee =
                            new EarlyExitException(8, input);
                        throw eee;
                }
                cnt8++;
            } while (true);


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end ID

    // $ANTLR start PATH
    public final void mPATH() throws RecognitionException {
        try {
            int _type = PATH;
            // Cubrid.g:1166:6: ( '\"' ( 'a' .. 'z' | 'A' .. 'Z' | '_' | '0' .. '9' | ' ' | '.' | '/' | ':' | '\\\\' )* '\"' )
            // Cubrid.g:1166:8: '\"' ( 'a' .. 'z' | 'A' .. 'Z' | '_' | '0' .. '9' | ' ' | '.' | '/' | ':' | '\\\\' )* '\"'
            {
            match('\"'); 
            // Cubrid.g:1166:12: ( 'a' .. 'z' | 'A' .. 'Z' | '_' | '0' .. '9' | ' ' | '.' | '/' | ':' | '\\\\' )*
            loop9:
            do {
                int alt9=2;
                int LA9_0 = input.LA(1);

                if ( (LA9_0==' '||(LA9_0>='.' && LA9_0<=':')||(LA9_0>='A' && LA9_0<='Z')||LA9_0=='\\'||LA9_0=='_'||(LA9_0>='a' && LA9_0<='z')) ) {
                    alt9=1;
                }


                switch (alt9) {
            	case 1 :
            	    // Cubrid.g:
            	    {
            	    if ( input.LA(1)==' '||(input.LA(1)>='.' && input.LA(1)<=':')||(input.LA(1)>='A' && input.LA(1)<='Z')||input.LA(1)=='\\'||input.LA(1)=='_'||(input.LA(1)>='a' && input.LA(1)<='z') ) {
            	        input.consume();

            	    }
            	    else {
            	        MismatchedSetException mse =
            	            new MismatchedSetException(null,input);
            	        recover(mse);    throw mse;
            	    }


            	    }
            	    break;

            	default :
            	    break loop9;
                }
            } while (true);

            match('\"'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end PATH

    // $ANTLR start WS
    public final void mWS() throws RecognitionException {
        try {
            int _type = WS;
            // Cubrid.g:1168:4: ( ( ' ' | '\\n' | '\\r' | '\\t' )+ )
            // Cubrid.g:1168:6: ( ' ' | '\\n' | '\\r' | '\\t' )+
            {
            // Cubrid.g:1168:6: ( ' ' | '\\n' | '\\r' | '\\t' )+
            int cnt10=0;
            loop10:
            do {
                int alt10=2;
                int LA10_0 = input.LA(1);

                if ( ((LA10_0>='\t' && LA10_0<='\n')||LA10_0=='\r'||LA10_0==' ') ) {
                    alt10=1;
                }


                switch (alt10) {
            	case 1 :
            	    // Cubrid.g:
            	    {
            	    if ( (input.LA(1)>='\t' && input.LA(1)<='\n')||input.LA(1)=='\r'||input.LA(1)==' ' ) {
            	        input.consume();

            	    }
            	    else {
            	        MismatchedSetException mse =
            	            new MismatchedSetException(null,input);
            	        recover(mse);    throw mse;
            	    }


            	    }
            	    break;

            	default :
            	    if ( cnt10 >= 1 ) break loop10;
                        EarlyExitException eee =
                            new EarlyExitException(10, input);
                        throw eee;
                }
                cnt10++;
            } while (true);

            skip();

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end WS

    // $ANTLR start ML_COMMENT
    public final void mML_COMMENT() throws RecognitionException {
        try {
            int _type = ML_COMMENT;
            // Cubrid.g:1170:11: ( '/*' ( options {greedy=false; } : . )* '*/' | '--' ( options {greedy=false; } : . )* '\\n' )
            int alt13=2;
            int LA13_0 = input.LA(1);

            if ( (LA13_0=='/') ) {
                alt13=1;
            }
            else if ( (LA13_0=='-') ) {
                alt13=2;
            }
            else {
                NoViableAltException nvae =
                    new NoViableAltException("1170:1: ML_COMMENT : ( '/*' ( options {greedy=false; } : . )* '*/' | '--' ( options {greedy=false; } : . )* '\\n' );", 13, 0, input);

                throw nvae;
            }
            switch (alt13) {
                case 1 :
                    // Cubrid.g:1171:2: '/*' ( options {greedy=false; } : . )* '*/'
                    {
                    match("/*"); 

                    // Cubrid.g:1171:7: ( options {greedy=false; } : . )*
                    loop11:
                    do {
                        int alt11=2;
                        int LA11_0 = input.LA(1);

                        if ( (LA11_0=='*') ) {
                            int LA11_1 = input.LA(2);

                            if ( (LA11_1=='/') ) {
                                alt11=2;
                            }
                            else if ( ((LA11_1>='\u0000' && LA11_1<='.')||(LA11_1>='0' && LA11_1<='\uFFFE')) ) {
                                alt11=1;
                            }


                        }
                        else if ( ((LA11_0>='\u0000' && LA11_0<=')')||(LA11_0>='+' && LA11_0<='\uFFFE')) ) {
                            alt11=1;
                        }


                        switch (alt11) {
                    	case 1 :
                    	    // Cubrid.g:1171:35: .
                    	    {
                    	    matchAny(); 

                    	    }
                    	    break;

                    	default :
                    	    break loop11;
                        }
                    } while (true);

                    match("*/"); 

                    channel = HIDDEN;

                    }
                    break;
                case 2 :
                    // Cubrid.g:1172:7: '--' ( options {greedy=false; } : . )* '\\n'
                    {
                    match("--"); 

                    // Cubrid.g:1172:12: ( options {greedy=false; } : . )*
                    loop12:
                    do {
                        int alt12=2;
                        int LA12_0 = input.LA(1);

                        if ( (LA12_0=='\n') ) {
                            alt12=2;
                        }
                        else if ( ((LA12_0>='\u0000' && LA12_0<='\t')||(LA12_0>='\u000B' && LA12_0<='\uFFFE')) ) {
                            alt12=1;
                        }


                        switch (alt12) {
                    	case 1 :
                    	    // Cubrid.g:1172:40: .
                    	    {
                    	    matchAny(); 

                    	    }
                    	    break;

                    	default :
                    	    break loop12;
                        }
                    } while (true);

                    match('\n'); 

                        	channel = HIDDEN;
                        	//skip();
                        

                    }
                    break;

            }
            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end ML_COMMENT

    public void mTokens() throws RecognitionException {
        // Cubrid.g:1:8: ( ACTION | ADD | ALL | ALTER | AM | AND | AS | ASC | ATTRIBUTE | AUTOCOMMIT | AUTO_INCREMENT | BETWEEN | BIT | BY | CALL | CASE | CACHE | CASCADE | CHANGE | CHAR | CHARACTER | CHECK | CLASS | COMMIT | CONSTRAINT | CREATE | DATE | DECIMAL | DEFERRED | DESC | DEFAULT | DELETE | DIFFERENCE | DISTINCT | DOUBLE | DROP | ELSE | END_STRING | EXCEPT | EXISTS | FILE | FLOAT | FOREIGN | FROM | FUNCTION | GROUP | HAVING | IN | INDEX | INHERIT | INNER | INSERT | INT | INTEGER | INTERSECTION | INTO | IS | JOIN | KEY | LIKE | LIST | LEFT | METHOD | MONETARY | MULTISET | MULTISET_OF | NCHAR | NO | NOT | NULL | NUMERIC | OBJECT | OF | OFF | ON | ONLY | OPTION | OR | ORDER | OUTER | PM | PRECISION | PRIMARY | QUERY | REAL | REFERENCES | RENAME | RESTRICT | RIGHT | ROLLBACK | SELECT | SEQUENCE | SEQUENCE_OF | SET | SHARE | SMALLINT | REVERSE | STRING_STR | SUBCLASS | SUPERCLASS | TABLE | TIME | TIMESTAMP | THEN | TRIGGER | TRIGGERS | TO | VALUES | UNION | UNIQUE | UPDATE | USING | VARCHAR | VARYING | VCLASS | VIEW | WHEN | WHERE | WITH | WORK | END | COMMA | STAR | STARTBRACE | ENDBRACE | DOT | QUOTA | DBQUOTA | EQUAL | CONNECT | DOLLAR | Q_MARK | T156 | T157 | T158 | T159 | T160 | T161 | T162 | T163 | T164 | T165 | T166 | T167 | T168 | T169 | T170 | T171 | T172 | T173 | T174 | DATE_FORMAT | TIME_FORMAT | LENGTH | DECIMALLITERAL | STARTBRACKET | ENDBRACKET | STRING | MARKS | KOREA | CHINESE | JAPAN | COLUMN | ID | PATH | WS | ML_COMMENT )
        int alt14=167;
        alt14 = dfa14.predict(input);
        switch (alt14) {
            case 1 :
                // Cubrid.g:1:10: ACTION
                {
                mACTION(); 

                }
                break;
            case 2 :
                // Cubrid.g:1:17: ADD
                {
                mADD(); 

                }
                break;
            case 3 :
                // Cubrid.g:1:21: ALL
                {
                mALL(); 

                }
                break;
            case 4 :
                // Cubrid.g:1:25: ALTER
                {
                mALTER(); 

                }
                break;
            case 5 :
                // Cubrid.g:1:31: AM
                {
                mAM(); 

                }
                break;
            case 6 :
                // Cubrid.g:1:34: AND
                {
                mAND(); 

                }
                break;
            case 7 :
                // Cubrid.g:1:38: AS
                {
                mAS(); 

                }
                break;
            case 8 :
                // Cubrid.g:1:41: ASC
                {
                mASC(); 

                }
                break;
            case 9 :
                // Cubrid.g:1:45: ATTRIBUTE
                {
                mATTRIBUTE(); 

                }
                break;
            case 10 :
                // Cubrid.g:1:55: AUTOCOMMIT
                {
                mAUTOCOMMIT(); 

                }
                break;
            case 11 :
                // Cubrid.g:1:66: AUTO_INCREMENT
                {
                mAUTO_INCREMENT(); 

                }
                break;
            case 12 :
                // Cubrid.g:1:81: BETWEEN
                {
                mBETWEEN(); 

                }
                break;
            case 13 :
                // Cubrid.g:1:89: BIT
                {
                mBIT(); 

                }
                break;
            case 14 :
                // Cubrid.g:1:93: BY
                {
                mBY(); 

                }
                break;
            case 15 :
                // Cubrid.g:1:96: CALL
                {
                mCALL(); 

                }
                break;
            case 16 :
                // Cubrid.g:1:101: CASE
                {
                mCASE(); 

                }
                break;
            case 17 :
                // Cubrid.g:1:106: CACHE
                {
                mCACHE(); 

                }
                break;
            case 18 :
                // Cubrid.g:1:112: CASCADE
                {
                mCASCADE(); 

                }
                break;
            case 19 :
                // Cubrid.g:1:120: CHANGE
                {
                mCHANGE(); 

                }
                break;
            case 20 :
                // Cubrid.g:1:127: CHAR
                {
                mCHAR(); 

                }
                break;
            case 21 :
                // Cubrid.g:1:132: CHARACTER
                {
                mCHARACTER(); 

                }
                break;
            case 22 :
                // Cubrid.g:1:142: CHECK
                {
                mCHECK(); 

                }
                break;
            case 23 :
                // Cubrid.g:1:148: CLASS
                {
                mCLASS(); 

                }
                break;
            case 24 :
                // Cubrid.g:1:154: COMMIT
                {
                mCOMMIT(); 

                }
                break;
            case 25 :
                // Cubrid.g:1:161: CONSTRAINT
                {
                mCONSTRAINT(); 

                }
                break;
            case 26 :
                // Cubrid.g:1:172: CREATE
                {
                mCREATE(); 

                }
                break;
            case 27 :
                // Cubrid.g:1:179: DATE
                {
                mDATE(); 

                }
                break;
            case 28 :
                // Cubrid.g:1:184: DECIMAL
                {
                mDECIMAL(); 

                }
                break;
            case 29 :
                // Cubrid.g:1:192: DEFERRED
                {
                mDEFERRED(); 

                }
                break;
            case 30 :
                // Cubrid.g:1:201: DESC
                {
                mDESC(); 

                }
                break;
            case 31 :
                // Cubrid.g:1:206: DEFAULT
                {
                mDEFAULT(); 

                }
                break;
            case 32 :
                // Cubrid.g:1:214: DELETE
                {
                mDELETE(); 

                }
                break;
            case 33 :
                // Cubrid.g:1:221: DIFFERENCE
                {
                mDIFFERENCE(); 

                }
                break;
            case 34 :
                // Cubrid.g:1:232: DISTINCT
                {
                mDISTINCT(); 

                }
                break;
            case 35 :
                // Cubrid.g:1:241: DOUBLE
                {
                mDOUBLE(); 

                }
                break;
            case 36 :
                // Cubrid.g:1:248: DROP
                {
                mDROP(); 

                }
                break;
            case 37 :
                // Cubrid.g:1:253: ELSE
                {
                mELSE(); 

                }
                break;
            case 38 :
                // Cubrid.g:1:258: END_STRING
                {
                mEND_STRING(); 

                }
                break;
            case 39 :
                // Cubrid.g:1:269: EXCEPT
                {
                mEXCEPT(); 

                }
                break;
            case 40 :
                // Cubrid.g:1:276: EXISTS
                {
                mEXISTS(); 

                }
                break;
            case 41 :
                // Cubrid.g:1:283: FILE
                {
                mFILE(); 

                }
                break;
            case 42 :
                // Cubrid.g:1:288: FLOAT
                {
                mFLOAT(); 

                }
                break;
            case 43 :
                // Cubrid.g:1:294: FOREIGN
                {
                mFOREIGN(); 

                }
                break;
            case 44 :
                // Cubrid.g:1:302: FROM
                {
                mFROM(); 

                }
                break;
            case 45 :
                // Cubrid.g:1:307: FUNCTION
                {
                mFUNCTION(); 

                }
                break;
            case 46 :
                // Cubrid.g:1:316: GROUP
                {
                mGROUP(); 

                }
                break;
            case 47 :
                // Cubrid.g:1:322: HAVING
                {
                mHAVING(); 

                }
                break;
            case 48 :
                // Cubrid.g:1:329: IN
                {
                mIN(); 

                }
                break;
            case 49 :
                // Cubrid.g:1:332: INDEX
                {
                mINDEX(); 

                }
                break;
            case 50 :
                // Cubrid.g:1:338: INHERIT
                {
                mINHERIT(); 

                }
                break;
            case 51 :
                // Cubrid.g:1:346: INNER
                {
                mINNER(); 

                }
                break;
            case 52 :
                // Cubrid.g:1:352: INSERT
                {
                mINSERT(); 

                }
                break;
            case 53 :
                // Cubrid.g:1:359: INT
                {
                mINT(); 

                }
                break;
            case 54 :
                // Cubrid.g:1:363: INTEGER
                {
                mINTEGER(); 

                }
                break;
            case 55 :
                // Cubrid.g:1:371: INTERSECTION
                {
                mINTERSECTION(); 

                }
                break;
            case 56 :
                // Cubrid.g:1:384: INTO
                {
                mINTO(); 

                }
                break;
            case 57 :
                // Cubrid.g:1:389: IS
                {
                mIS(); 

                }
                break;
            case 58 :
                // Cubrid.g:1:392: JOIN
                {
                mJOIN(); 

                }
                break;
            case 59 :
                // Cubrid.g:1:397: KEY
                {
                mKEY(); 

                }
                break;
            case 60 :
                // Cubrid.g:1:401: LIKE
                {
                mLIKE(); 

                }
                break;
            case 61 :
                // Cubrid.g:1:406: LIST
                {
                mLIST(); 

                }
                break;
            case 62 :
                // Cubrid.g:1:411: LEFT
                {
                mLEFT(); 

                }
                break;
            case 63 :
                // Cubrid.g:1:416: METHOD
                {
                mMETHOD(); 

                }
                break;
            case 64 :
                // Cubrid.g:1:423: MONETARY
                {
                mMONETARY(); 

                }
                break;
            case 65 :
                // Cubrid.g:1:432: MULTISET
                {
                mMULTISET(); 

                }
                break;
            case 66 :
                // Cubrid.g:1:441: MULTISET_OF
                {
                mMULTISET_OF(); 

                }
                break;
            case 67 :
                // Cubrid.g:1:453: NCHAR
                {
                mNCHAR(); 

                }
                break;
            case 68 :
                // Cubrid.g:1:459: NO
                {
                mNO(); 

                }
                break;
            case 69 :
                // Cubrid.g:1:462: NOT
                {
                mNOT(); 

                }
                break;
            case 70 :
                // Cubrid.g:1:466: NULL
                {
                mNULL(); 

                }
                break;
            case 71 :
                // Cubrid.g:1:471: NUMERIC
                {
                mNUMERIC(); 

                }
                break;
            case 72 :
                // Cubrid.g:1:479: OBJECT
                {
                mOBJECT(); 

                }
                break;
            case 73 :
                // Cubrid.g:1:486: OF
                {
                mOF(); 

                }
                break;
            case 74 :
                // Cubrid.g:1:489: OFF
                {
                mOFF(); 

                }
                break;
            case 75 :
                // Cubrid.g:1:493: ON
                {
                mON(); 

                }
                break;
            case 76 :
                // Cubrid.g:1:496: ONLY
                {
                mONLY(); 

                }
                break;
            case 77 :
                // Cubrid.g:1:501: OPTION
                {
                mOPTION(); 

                }
                break;
            case 78 :
                // Cubrid.g:1:508: OR
                {
                mOR(); 

                }
                break;
            case 79 :
                // Cubrid.g:1:511: ORDER
                {
                mORDER(); 

                }
                break;
            case 80 :
                // Cubrid.g:1:517: OUTER
                {
                mOUTER(); 

                }
                break;
            case 81 :
                // Cubrid.g:1:523: PM
                {
                mPM(); 

                }
                break;
            case 82 :
                // Cubrid.g:1:526: PRECISION
                {
                mPRECISION(); 

                }
                break;
            case 83 :
                // Cubrid.g:1:536: PRIMARY
                {
                mPRIMARY(); 

                }
                break;
            case 84 :
                // Cubrid.g:1:544: QUERY
                {
                mQUERY(); 

                }
                break;
            case 85 :
                // Cubrid.g:1:550: REAL
                {
                mREAL(); 

                }
                break;
            case 86 :
                // Cubrid.g:1:555: REFERENCES
                {
                mREFERENCES(); 

                }
                break;
            case 87 :
                // Cubrid.g:1:566: RENAME
                {
                mRENAME(); 

                }
                break;
            case 88 :
                // Cubrid.g:1:573: RESTRICT
                {
                mRESTRICT(); 

                }
                break;
            case 89 :
                // Cubrid.g:1:582: RIGHT
                {
                mRIGHT(); 

                }
                break;
            case 90 :
                // Cubrid.g:1:588: ROLLBACK
                {
                mROLLBACK(); 

                }
                break;
            case 91 :
                // Cubrid.g:1:597: SELECT
                {
                mSELECT(); 

                }
                break;
            case 92 :
                // Cubrid.g:1:604: SEQUENCE
                {
                mSEQUENCE(); 

                }
                break;
            case 93 :
                // Cubrid.g:1:613: SEQUENCE_OF
                {
                mSEQUENCE_OF(); 

                }
                break;
            case 94 :
                // Cubrid.g:1:625: SET
                {
                mSET(); 

                }
                break;
            case 95 :
                // Cubrid.g:1:629: SHARE
                {
                mSHARE(); 

                }
                break;
            case 96 :
                // Cubrid.g:1:635: SMALLINT
                {
                mSMALLINT(); 

                }
                break;
            case 97 :
                // Cubrid.g:1:644: REVERSE
                {
                mREVERSE(); 

                }
                break;
            case 98 :
                // Cubrid.g:1:652: STRING_STR
                {
                mSTRING_STR(); 

                }
                break;
            case 99 :
                // Cubrid.g:1:663: SUBCLASS
                {
                mSUBCLASS(); 

                }
                break;
            case 100 :
                // Cubrid.g:1:672: SUPERCLASS
                {
                mSUPERCLASS(); 

                }
                break;
            case 101 :
                // Cubrid.g:1:683: TABLE
                {
                mTABLE(); 

                }
                break;
            case 102 :
                // Cubrid.g:1:689: TIME
                {
                mTIME(); 

                }
                break;
            case 103 :
                // Cubrid.g:1:694: TIMESTAMP
                {
                mTIMESTAMP(); 

                }
                break;
            case 104 :
                // Cubrid.g:1:704: THEN
                {
                mTHEN(); 

                }
                break;
            case 105 :
                // Cubrid.g:1:709: TRIGGER
                {
                mTRIGGER(); 

                }
                break;
            case 106 :
                // Cubrid.g:1:717: TRIGGERS
                {
                mTRIGGERS(); 

                }
                break;
            case 107 :
                // Cubrid.g:1:726: TO
                {
                mTO(); 

                }
                break;
            case 108 :
                // Cubrid.g:1:729: VALUES
                {
                mVALUES(); 

                }
                break;
            case 109 :
                // Cubrid.g:1:736: UNION
                {
                mUNION(); 

                }
                break;
            case 110 :
                // Cubrid.g:1:742: UNIQUE
                {
                mUNIQUE(); 

                }
                break;
            case 111 :
                // Cubrid.g:1:749: UPDATE
                {
                mUPDATE(); 

                }
                break;
            case 112 :
                // Cubrid.g:1:756: USING
                {
                mUSING(); 

                }
                break;
            case 113 :
                // Cubrid.g:1:762: VARCHAR
                {
                mVARCHAR(); 

                }
                break;
            case 114 :
                // Cubrid.g:1:770: VARYING
                {
                mVARYING(); 

                }
                break;
            case 115 :
                // Cubrid.g:1:778: VCLASS
                {
                mVCLASS(); 

                }
                break;
            case 116 :
                // Cubrid.g:1:785: VIEW
                {
                mVIEW(); 

                }
                break;
            case 117 :
                // Cubrid.g:1:790: WHEN
                {
                mWHEN(); 

                }
                break;
            case 118 :
                // Cubrid.g:1:795: WHERE
                {
                mWHERE(); 

                }
                break;
            case 119 :
                // Cubrid.g:1:801: WITH
                {
                mWITH(); 

                }
                break;
            case 120 :
                // Cubrid.g:1:806: WORK
                {
                mWORK(); 

                }
                break;
            case 121 :
                // Cubrid.g:1:811: END
                {
                mEND(); 

                }
                break;
            case 122 :
                // Cubrid.g:1:815: COMMA
                {
                mCOMMA(); 

                }
                break;
            case 123 :
                // Cubrid.g:1:821: STAR
                {
                mSTAR(); 

                }
                break;
            case 124 :
                // Cubrid.g:1:826: STARTBRACE
                {
                mSTARTBRACE(); 

                }
                break;
            case 125 :
                // Cubrid.g:1:837: ENDBRACE
                {
                mENDBRACE(); 

                }
                break;
            case 126 :
                // Cubrid.g:1:846: DOT
                {
                mDOT(); 

                }
                break;
            case 127 :
                // Cubrid.g:1:850: QUOTA
                {
                mQUOTA(); 

                }
                break;
            case 128 :
                // Cubrid.g:1:856: DBQUOTA
                {
                mDBQUOTA(); 

                }
                break;
            case 129 :
                // Cubrid.g:1:864: EQUAL
                {
                mEQUAL(); 

                }
                break;
            case 130 :
                // Cubrid.g:1:870: CONNECT
                {
                mCONNECT(); 

                }
                break;
            case 131 :
                // Cubrid.g:1:878: DOLLAR
                {
                mDOLLAR(); 

                }
                break;
            case 132 :
                // Cubrid.g:1:885: Q_MARK
                {
                mQ_MARK(); 

                }
                break;
            case 133 :
                // Cubrid.g:1:892: T156
                {
                mT156(); 

                }
                break;
            case 134 :
                // Cubrid.g:1:897: T157
                {
                mT157(); 

                }
                break;
            case 135 :
                // Cubrid.g:1:902: T158
                {
                mT158(); 

                }
                break;
            case 136 :
                // Cubrid.g:1:907: T159
                {
                mT159(); 

                }
                break;
            case 137 :
                // Cubrid.g:1:912: T160
                {
                mT160(); 

                }
                break;
            case 138 :
                // Cubrid.g:1:917: T161
                {
                mT161(); 

                }
                break;
            case 139 :
                // Cubrid.g:1:922: T162
                {
                mT162(); 

                }
                break;
            case 140 :
                // Cubrid.g:1:927: T163
                {
                mT163(); 

                }
                break;
            case 141 :
                // Cubrid.g:1:932: T164
                {
                mT164(); 

                }
                break;
            case 142 :
                // Cubrid.g:1:937: T165
                {
                mT165(); 

                }
                break;
            case 143 :
                // Cubrid.g:1:942: T166
                {
                mT166(); 

                }
                break;
            case 144 :
                // Cubrid.g:1:947: T167
                {
                mT167(); 

                }
                break;
            case 145 :
                // Cubrid.g:1:952: T168
                {
                mT168(); 

                }
                break;
            case 146 :
                // Cubrid.g:1:957: T169
                {
                mT169(); 

                }
                break;
            case 147 :
                // Cubrid.g:1:962: T170
                {
                mT170(); 

                }
                break;
            case 148 :
                // Cubrid.g:1:967: T171
                {
                mT171(); 

                }
                break;
            case 149 :
                // Cubrid.g:1:972: T172
                {
                mT172(); 

                }
                break;
            case 150 :
                // Cubrid.g:1:977: T173
                {
                mT173(); 

                }
                break;
            case 151 :
                // Cubrid.g:1:982: T174
                {
                mT174(); 

                }
                break;
            case 152 :
                // Cubrid.g:1:987: DATE_FORMAT
                {
                mDATE_FORMAT(); 

                }
                break;
            case 153 :
                // Cubrid.g:1:999: TIME_FORMAT
                {
                mTIME_FORMAT(); 

                }
                break;
            case 154 :
                // Cubrid.g:1:1011: LENGTH
                {
                mLENGTH(); 

                }
                break;
            case 155 :
                // Cubrid.g:1:1018: DECIMALLITERAL
                {
                mDECIMALLITERAL(); 

                }
                break;
            case 156 :
                // Cubrid.g:1:1033: STARTBRACKET
                {
                mSTARTBRACKET(); 

                }
                break;
            case 157 :
                // Cubrid.g:1:1046: ENDBRACKET
                {
                mENDBRACKET(); 

                }
                break;
            case 158 :
                // Cubrid.g:1:1057: STRING
                {
                mSTRING(); 

                }
                break;
            case 159 :
                // Cubrid.g:1:1064: MARKS
                {
                mMARKS(); 

                }
                break;
            case 160 :
                // Cubrid.g:1:1070: KOREA
                {
                mKOREA(); 

                }
                break;
            case 161 :
                // Cubrid.g:1:1076: CHINESE
                {
                mCHINESE(); 

                }
                break;
            case 162 :
                // Cubrid.g:1:1084: JAPAN
                {
                mJAPAN(); 

                }
                break;
            case 163 :
                // Cubrid.g:1:1090: COLUMN
                {
                mCOLUMN(); 

                }
                break;
            case 164 :
                // Cubrid.g:1:1097: ID
                {
                mID(); 

                }
                break;
            case 165 :
                // Cubrid.g:1:1100: PATH
                {
                mPATH(); 

                }
                break;
            case 166 :
                // Cubrid.g:1:1105: WS
                {
                mWS(); 

                }
                break;
            case 167 :
                // Cubrid.g:1:1108: ML_COMMENT
                {
                mML_COMMENT(); 

                }
                break;

        }

    }


    protected DFA14 dfa14 = new DFA14(this);
    static final String DFA14_eotS =
        "\1\uffff\27\64\2\uffff\1\u0081\3\uffff\1\u0085\1\u008a\1\uffff\1"+
        "\u008d\2\uffff\1\u008f\1\u0092\1\u0094\1\u0096\1\uffff\1\u0098\1"+
        "\u009b\1\u009d\1\u009e\1\u00a1\10\uffff\1\u00a5\1\64\1\u00a7\6\64"+
        "\1\u00af\25\64\1\u00d3\1\u00d4\11\64\1\u00e1\1\u00e3\2\64\1\u00e7"+
        "\1\64\1\u00ea\1\64\1\u00ed\11\64\1\u00fe\15\64\37\uffff\2\u009e"+
        "\3\uffff\1\u010f\1\uffff\1\u0110\1\uffff\1\u0111\1\64\1\u0113\3"+
        "\64\1\u0117\1\uffff\25\64\1\u0130\10\64\1\u013b\4\64\2\uffff\1\64"+
        "\1\u0141\11\64\1\u014b\1\uffff\1\u014c\1\uffff\3\64\1\uffff\2\64"+
        "\1\uffff\2\64\1\uffff\12\64\1\u015e\5\64\1\uffff\16\64\5\uffff\1"+
        "\64\1\uffff\3\64\1\uffff\1\64\1\u017c\6\64\1\u0183\1\64\1\u0185"+
        "\4\64\1\u018a\3\64\1\u018e\1\64\1\u0190\2\64\1\uffff\1\u0193\1\64"+
        "\1\u0195\1\64\1\u0197\4\64\1\u019d\1\uffff\4\64\1\u01a2\1\uffff"+
        "\1\u01a3\1\u01a4\1\u01a5\4\64\1\u01aa\1\64\2\uffff\4\64\1\u01b0"+
        "\6\64\1\u01b7\5\64\1\uffff\5\64\1\u01c2\2\64\1\u01c6\1\u01c7\11"+
        "\64\1\u01d1\1\u01d2\1\u01d3\1\u01d4\6\64\1\uffff\1\64\1\u01dc\2"+
        "\64\1\u01df\1\64\1\uffff\1\64\1\uffff\1\u01e2\3\64\1\uffff\3\64"+
        "\1\uffff\1\64\1\uffff\1\u01ea\1\64\1\uffff\1\u01ec\1\uffff\1\64"+
        "\1\uffff\1\64\1\u01ef\3\64\1\uffff\1\64\1\u01f4\1\64\1\u01f6\4\uffff"+
        "\3\64\1\u01fa\1\uffff\2\64\1\u01fd\1\u01fe\1\64\1\uffff\2\64\1\u0202"+
        "\3\64\1\uffff\2\64\1\u0208\3\64\1\u020c\3\64\1\uffff\1\64\1\u0211"+
        "\1\64\2\uffff\5\64\1\u0218\1\u0219\1\64\1\u021b\4\uffff\1\u021c"+
        "\5\64\1\u0222\1\uffff\1\u0223\1\64\1\uffff\1\u0225\1\64\1\uffff"+
        "\3\64\1\u022a\2\64\1\u022d\1\uffff\1\u022e\1\uffff\2\64\1\uffff"+
        "\1\u0231\3\64\1\uffff\1\u0235\1\uffff\1\u0236\2\64\1\uffff\1\64"+
        "\1\u023a\2\uffff\1\u023b\2\64\1\uffff\3\64\1\u0241\1\64\1\uffff"+
        "\1\64\1\u0244\1\64\1\uffff\1\u0246\3\64\1\uffff\1\64\1\u024b\2\64"+
        "\1\u024e\1\u024f\2\uffff\1\u0250\2\uffff\3\64\1\u0254\1\64\2\uffff"+
        "\1\64\1\uffff\1\u0257\2\64\1\u025a\1\uffff\1\64\1\u025c\2\uffff"+
        "\1\u025d\1\64\1\uffff\1\64\1\u0260\1\u0261\2\uffff\2\64\1\u0264"+
        "\2\uffff\1\64\1\u0266\1\64\1\u0268\1\64\1\uffff\2\64\1\uffff\1\64"+
        "\1\uffff\2\64\1\u0270\1\64\1\uffff\1\u0272\1\u0273\3\uffff\3\64"+
        "\1\uffff\2\64\1\uffff\1\64\1\u027a\1\uffff\1\u027b\2\uffff\1\u027c"+
        "\1\64\2\uffff\1\u027e\1\u0280\1\uffff\1\64\1\uffff\1\64\1\uffff"+
        "\1\u0283\1\u0284\1\u0286\1\u0287\1\u0288\1\64\1\u028a\1\uffff\1"+
        "\64\2\uffff\2\64\1\u028e\1\u028f\2\64\3\uffff\1\64\1\uffff\1\64"+
        "\1\uffff\1\u0294\1\64\2\uffff\1\64\3\uffff\1\64\1\uffff\1\u0298"+
        "\1\64\1\u029a\2\uffff\1\u029b\1\u029c\2\64\1\uffff\1\u029f\1\64"+
        "\1\u02a1\1\uffff\1\64\3\uffff\1\64\1\u02a4\1\uffff\1\u02a5\1\uffff"+
        "\1\64\1\u02a7\2\uffff\1\64\1\uffff\1\u02a9\1\uffff";
    static final String DFA14_eofS =
        "\u02aa\uffff";
    static final String DFA14_minS =
        "\1\11\1\103\1\105\2\101\1\114\1\111\1\122\1\101\1\116\1\117\3\105"+
        "\1\103\1\102\1\115\1\125\2\105\2\101\1\116\1\110\2\uffff\1\75\3"+
        "\uffff\2\40\1\uffff\1\75\2\uffff\1\75\1\55\1\52\1\75\1\uffff\3\75"+
        "\2\60\10\uffff\1\60\1\104\1\60\1\114\1\104\4\124\1\60\1\124\1\101"+
        "\1\115\1\101\1\105\1\103\1\106\1\103\1\117\1\125\1\124\1\103\1\104"+
        "\1\123\1\117\1\114\1\122\1\117\1\116\1\117\1\126\2\60\1\111\1\131"+
        "\1\113\1\106\1\124\1\116\1\114\1\110\1\114\2\60\1\112\1\124\1\60"+
        "\1\124\1\60\1\105\1\60\1\105\1\101\1\114\1\107\1\114\2\101\1\122"+
        "\1\102\1\60\1\105\1\111\1\102\1\115\1\105\2\114\2\111\1\104\1\105"+
        "\1\122\1\124\6\uffff\1\40\30\uffff\1\57\1\60\3\uffff\1\60\1\uffff"+
        "\1\60\1\uffff\1\60\1\105\1\60\1\111\1\117\1\122\1\60\1\uffff\1\127"+
        "\1\116\1\103\1\115\2\123\1\101\1\114\1\103\1\110\1\106\1\124\1\111"+
        "\1\103\1\105\1\101\1\120\1\102\2\105\1\123\1\60\1\105\1\101\2\105"+
        "\1\115\1\103\1\125\1\111\1\60\4\105\2\uffff\1\116\1\60\1\124\1\105"+
        "\1\124\1\110\1\105\1\124\1\101\1\114\1\105\1\60\1\uffff\1\60\1\uffff"+
        "\3\105\1\uffff\1\111\1\131\1\uffff\1\103\1\115\1\uffff\1\122\2\105"+
        "\1\124\1\114\1\101\1\114\1\110\1\125\1\105\1\60\1\114\1\122\1\111"+
        "\1\103\1\105\1\uffff\1\116\1\107\1\114\1\105\1\127\1\101\1\103\1"+
        "\125\1\117\1\116\1\101\1\116\1\113\1\110\5\uffff\1\122\1\uffff\1"+
        "\117\1\103\1\111\1\uffff\1\105\1\60\1\107\1\113\1\111\1\124\1\123"+
        "\1\124\1\60\1\101\1\60\2\105\1\111\1\115\1\60\1\124\1\122\1\125"+
        "\1\60\1\114\1\60\2\124\1\uffff\1\60\1\124\1\60\1\111\1\60\1\124"+
        "\1\120\1\116\1\107\1\60\1\uffff\3\122\1\130\1\60\1\uffff\3\60\1"+
        "\117\1\124\1\111\1\122\1\60\1\122\2\uffff\1\103\2\122\1\117\1\60"+
        "\1\111\1\101\1\131\3\122\1\60\1\115\1\102\1\124\1\105\1\103\1\uffff"+
        "\1\114\1\105\1\116\1\114\1\122\1\60\1\107\1\105\2\60\1\123\1\110"+
        "\1\111\1\105\1\125\1\116\1\107\1\124\1\105\4\60\1\116\1\111\1\117"+
        "\1\102\1\105\1\103\1\uffff\1\105\1\60\1\124\1\122\1\60\1\105\1\uffff"+
        "\1\104\1\uffff\1\60\1\122\1\116\1\101\1\uffff\1\105\1\122\1\114"+
        "\1\uffff\1\105\1\uffff\1\60\1\123\1\uffff\1\60\1\uffff\1\107\1\uffff"+
        "\1\111\1\60\1\107\1\123\1\105\1\uffff\1\111\1\60\1\124\1\60\4\uffff"+
        "\1\104\1\101\1\123\1\60\1\uffff\1\111\1\124\2\60\1\116\1\uffff\1"+
        "\123\1\122\1\60\1\105\1\123\1\111\1\uffff\1\105\1\101\1\60\1\116"+
        "\1\124\1\111\1\60\1\107\1\101\1\103\1\uffff\1\105\1\60\1\124\2\uffff"+
        "\1\123\1\101\1\116\1\123\1\105\2\60\1\105\1\60\4\uffff\1\60\1\116"+
        "\1\115\1\125\1\116\1\124\1\60\1\uffff\1\60\1\101\1\uffff\1\60\1"+
        "\105\1\uffff\1\105\1\103\1\114\1\60\1\105\1\124\1\60\1\uffff\1\60"+
        "\1\uffff\1\116\1\117\1\uffff\1\60\1\105\1\122\1\124\1\uffff\1\60"+
        "\1\uffff\1\60\1\122\1\105\1\uffff\1\103\1\60\2\uffff\1\60\1\111"+
        "\1\131\1\uffff\1\116\1\105\1\103\1\60\1\103\1\uffff\1\103\1\60\1"+
        "\116\1\uffff\1\60\1\123\1\114\1\122\1\uffff\1\101\1\60\1\122\1\107"+
        "\2\60\2\uffff\1\60\2\uffff\1\103\1\115\1\124\1\60\1\105\2\uffff"+
        "\1\111\1\uffff\1\60\1\116\1\124\1\60\1\uffff\1\104\1\60\2\uffff"+
        "\1\60\1\116\1\uffff\1\103\2\60\2\uffff\1\131\1\124\1\60\2\uffff"+
        "\1\117\1\60\1\103\1\60\1\124\1\uffff\1\113\1\105\1\uffff\1\124\1"+
        "\uffff\1\123\1\101\1\60\1\115\1\uffff\2\60\3\uffff\1\122\1\111\1"+
        "\105\1\uffff\1\122\1\116\1\uffff\1\103\1\60\1\uffff\1\60\2\uffff"+
        "\1\60\1\124\2\uffff\2\60\1\uffff\1\116\1\uffff\1\105\1\uffff\5\60"+
        "\1\123\1\60\1\uffff\1\120\2\uffff\1\105\1\124\2\60\1\124\1\105\3"+
        "\uffff\1\111\1\uffff\1\117\1\uffff\1\60\1\123\2\uffff\1\117\3\uffff"+
        "\1\123\1\uffff\1\60\1\115\1\60\2\uffff\2\60\1\117\1\106\1\uffff"+
        "\1\60\1\106\1\60\1\uffff\1\105\3\uffff\1\116\1\60\1\uffff\1\60\1"+
        "\uffff\1\116\1\60\2\uffff\1\124\1\uffff\1\60\1\uffff";
    static final String DFA14_maxS =
        "\1\ud7af\1\125\1\131\2\122\1\130\1\125\1\122\1\101\1\123\1\117\1"+
        "\105\1\111\3\125\1\122\1\125\1\117\1\125\1\122\1\111\1\123\1\117"+
        "\2\uffff\1\75\3\uffff\1\ud7af\1\172\1\uffff\1\174\2\uffff\4\75\1"+
        "\uffff\1\75\1\76\1\75\1\172\1\71\10\uffff\1\172\1\104\1\172\1\124"+
        "\1\104\4\124\1\172\1\124\1\105\1\116\1\101\1\105\3\123\1\117\1\125"+
        "\1\124\1\111\1\104\1\123\1\117\1\114\1\122\1\117\1\116\1\117\1\126"+
        "\2\172\1\111\1\131\1\123\1\106\1\124\1\116\1\114\1\110\1\115\2\172"+
        "\1\112\1\124\1\172\1\124\1\172\1\111\1\172\1\105\1\126\1\114\1\107"+
        "\1\124\2\101\1\122\1\120\1\172\1\105\1\111\1\102\1\115\1\105\1\114"+
        "\1\122\2\111\1\104\1\105\1\122\1\124\6\uffff\1\172\30\uffff\2\172"+
        "\3\uffff\1\172\1\uffff\1\172\1\uffff\1\172\1\105\1\172\1\111\1\117"+
        "\1\122\1\172\1\uffff\1\127\1\122\1\103\1\115\2\123\1\101\1\114\1"+
        "\105\1\110\1\106\1\124\1\111\1\103\2\105\1\120\1\102\2\105\1\123"+
        "\1\172\1\105\1\101\2\105\1\115\1\103\1\125\1\111\1\172\4\105\2\uffff"+
        "\1\116\1\172\1\124\1\105\1\124\1\110\1\105\1\124\1\101\1\114\1\105"+
        "\1\172\1\uffff\1\172\1\uffff\3\105\1\uffff\1\111\1\131\1\uffff\1"+
        "\103\1\115\1\uffff\1\122\2\105\1\124\1\114\1\101\1\114\1\110\1\125"+
        "\1\105\1\172\1\114\1\122\1\111\1\103\1\105\1\uffff\1\116\1\107\1"+
        "\114\1\105\1\127\1\101\1\131\1\125\1\121\1\116\1\101\1\122\1\113"+
        "\1\110\5\uffff\1\122\1\uffff\1\117\1\137\1\111\1\uffff\1\105\1\172"+
        "\1\107\1\113\1\111\1\124\1\123\1\124\1\172\1\101\1\172\2\105\1\111"+
        "\1\115\1\172\1\124\1\122\1\125\1\172\1\114\1\172\2\124\1\uffff\1"+
        "\172\1\124\1\172\1\111\1\172\1\124\1\120\1\116\1\122\1\172\1\uffff"+
        "\3\122\1\130\1\172\1\uffff\3\172\1\117\1\124\1\111\1\122\1\172\1"+
        "\122\2\uffff\1\103\2\122\1\117\1\172\1\111\1\101\1\131\3\122\1\172"+
        "\1\115\1\102\1\124\1\105\1\103\1\uffff\1\114\1\105\1\116\1\114\1"+
        "\122\1\172\1\107\1\105\2\172\1\123\1\110\1\111\1\105\1\125\1\116"+
        "\1\107\1\124\1\105\4\172\1\116\1\111\1\117\1\102\1\105\1\103\1\uffff"+
        "\1\105\1\172\1\124\1\122\1\172\1\105\1\uffff\1\104\1\uffff\1\172"+
        "\1\122\1\116\1\101\1\uffff\1\105\1\122\1\114\1\uffff\1\105\1\uffff"+
        "\1\172\1\123\1\uffff\1\172\1\uffff\1\107\1\uffff\1\111\1\172\1\107"+
        "\1\123\1\105\1\uffff\1\111\1\172\1\124\1\172\4\uffff\1\104\1\101"+
        "\1\123\1\172\1\uffff\1\111\1\124\2\172\1\116\1\uffff\1\123\1\122"+
        "\1\172\1\105\1\123\1\111\1\uffff\1\105\1\101\1\172\1\116\1\124\1"+
        "\111\1\172\1\107\1\101\1\103\1\uffff\1\105\1\172\1\124\2\uffff\1"+
        "\123\1\101\1\116\1\123\1\105\2\172\1\105\1\172\4\uffff\1\172\1\116"+
        "\1\115\1\125\1\116\1\124\1\172\1\uffff\1\172\1\101\1\uffff\1\172"+
        "\1\105\1\uffff\1\105\1\103\1\114\1\172\1\105\1\124\1\172\1\uffff"+
        "\1\172\1\uffff\1\116\1\117\1\uffff\1\172\1\105\1\122\1\124\1\uffff"+
        "\1\172\1\uffff\1\172\1\122\1\105\1\uffff\1\103\1\172\2\uffff\1\172"+
        "\1\111\1\131\1\uffff\1\116\1\105\1\103\1\172\1\103\1\uffff\1\103"+
        "\1\172\1\116\1\uffff\1\172\1\123\1\114\1\122\1\uffff\1\101\1\172"+
        "\1\122\1\107\2\172\2\uffff\1\172\2\uffff\1\103\1\115\1\124\1\172"+
        "\1\105\2\uffff\1\111\1\uffff\1\172\1\116\1\124\1\172\1\uffff\1\104"+
        "\1\172\2\uffff\1\172\1\116\1\uffff\1\103\2\172\2\uffff\1\131\1\124"+
        "\1\172\2\uffff\1\117\1\172\1\103\1\172\1\124\1\uffff\1\113\1\105"+
        "\1\uffff\1\124\1\uffff\1\123\1\101\1\172\1\115\1\uffff\2\172\3\uffff"+
        "\1\122\1\111\1\105\1\uffff\1\122\1\116\1\uffff\1\103\1\172\1\uffff"+
        "\1\172\2\uffff\1\172\1\124\2\uffff\2\172\1\uffff\1\116\1\uffff\1"+
        "\105\1\uffff\5\172\1\123\1\172\1\uffff\1\120\2\uffff\1\105\1\124"+
        "\2\172\1\124\1\105\3\uffff\1\111\1\uffff\1\117\1\uffff\1\172\1\123"+
        "\2\uffff\1\117\3\uffff\1\123\1\uffff\1\172\1\115\1\172\2\uffff\2"+
        "\172\1\117\1\106\1\uffff\1\172\1\106\1\172\1\uffff\1\105\3\uffff"+
        "\1\116\1\172\1\uffff\1\172\1\uffff\1\116\1\172\2\uffff\1\124\1\uffff"+
        "\1\172\1\uffff";
    static final String DFA14_acceptS =
        "\30\uffff\1\171\1\172\1\uffff\1\174\1\175\1\176\2\uffff\1\u0081"+
        "\1\uffff\1\u0083\1\u0084\4\uffff\1\u008b\5\uffff\1\u009d\1\u009f"+
        "\1\u00a0\1\u00a1\1\u00a0\1\u00a2\1\u00a4\1\u00a6\112\uffff\1\u0087"+
        "\1\173\1\174\1\175\1\u009e\1\177\1\uffff\2\u00a3\1\u00a5\1\u0080"+
        "\1\u008a\1\u0082\1\u008d\1\u0085\1\u008f\1\u0086\1\u00a7\1\u0095"+
        "\1\u0088\1\u0096\1\u0089\1\u008e\1\u008c\1\u0097\1\u0091\1\u0090"+
        "\1\u0093\1\u0092\1\u0094\1\u009b\2\uffff\1\u009c\1\u009a\1\u009d"+
        "\1\uffff\1\7\1\uffff\1\5\7\uffff\1\16\43\uffff\1\60\1\71\14\uffff"+
        "\1\104\1\uffff\1\111\3\uffff\1\116\2\uffff\1\113\2\uffff\1\121\20"+
        "\uffff\1\153\16\uffff\1\u0098\1\u0099\1\10\1\6\1\3\1\uffff\1\2\3"+
        "\uffff\1\15\30\uffff\1\46\12\uffff\1\65\5\uffff\1\73\11\uffff\1"+
        "\105\1\112\21\uffff\1\136\35\uffff\1\24\6\uffff\1\17\1\uffff\1\20"+
        "\4\uffff\1\36\3\uffff\1\44\1\uffff\1\33\2\uffff\1\45\1\uffff\1\51"+
        "\1\uffff\1\54\5\uffff\1\70\4\uffff\1\72\1\75\1\74\1\76\4\uffff\1"+
        "\106\5\uffff\1\114\6\uffff\1\125\12\uffff\1\150\3\uffff\1\146\1"+
        "\164\11\uffff\1\165\1\170\1\167\1\4\7\uffff\1\26\2\uffff\1\27\2"+
        "\uffff\1\21\7\uffff\1\47\1\uffff\1\52\2\uffff\1\56\4\uffff\1\63"+
        "\1\uffff\1\61\3\uffff\1\103\2\uffff\1\120\1\117\3\uffff\1\124\5"+
        "\uffff\1\131\3\uffff\1\137\4\uffff\1\145\6\uffff\1\155\1\160\1\uffff"+
        "\1\166\1\1\5\uffff\1\23\1\30\1\uffff\1\32\4\uffff\1\40\2\uffff\1"+
        "\43\1\50\2\uffff\1\57\3\uffff\1\64\1\77\3\uffff\1\110\1\115\5\uffff"+
        "\1\127\2\uffff\1\133\1\uffff\1\142\4\uffff\1\163\2\uffff\1\154\1"+
        "\156\1\157\3\uffff\1\14\2\uffff\1\22\2\uffff\1\34\1\uffff\1\37\1"+
        "\53\2\uffff\1\66\1\62\2\uffff\1\107\1\uffff\1\123\1\uffff\1\141"+
        "\7\uffff\1\151\1\uffff\1\161\1\162\6\uffff\1\42\1\35\1\55\1\uffff"+
        "\1\100\1\uffff\1\101\2\uffff\1\130\1\132\1\uffff\1\134\1\140\1\143"+
        "\1\uffff\1\152\3\uffff\1\11\1\25\4\uffff\1\122\3\uffff\1\147\1\uffff"+
        "\1\12\1\31\1\41\2\uffff\1\126\1\uffff\1\144\2\uffff\1\102\1\135"+
        "\1\uffff\1\67\1\uffff\1\13";
    static final String DFA14_specialS =
        "\u02aa\uffff}>";
    static final String[] DFA14_transitionS = {
            "\2\65\2\uffff\1\65\22\uffff\1\65\1\uffff\1\37\1\uffff\1\42\1"+
            "\51\1\47\1\36\1\55\1\56\1\32\1\44\1\31\1\45\1\35\1\46\12\54"+
            "\1\uffff\1\30\1\52\1\40\1\53\1\43\1\uffff\1\1\1\2\1\3\1\4\1"+
            "\5\1\6\1\7\1\10\1\11\1\12\1\13\1\14\1\15\1\16\1\17\1\20\1\21"+
            "\1\22\1\23\1\24\1\26\1\25\1\27\3\64\1\57\1\uffff\1\57\1\50\1"+
            "\64\1\uffff\32\64\1\33\1\41\1\34\u1082\uffff\u0100\62\u1e40"+
            "\uffff\u00f0\63\140\60\160\63\u1c00\uffff\u51a6\61\u0c5a\uffff"+
            "\u2bb0\62",
            "\1\73\1\72\7\uffff\1\71\1\70\1\67\4\uffff\1\66\1\75\1\74",
            "\1\100\3\uffff\1\76\17\uffff\1\77",
            "\1\105\6\uffff\1\101\3\uffff\1\103\2\uffff\1\102\2\uffff\1\104",
            "\1\112\3\uffff\1\107\3\uffff\1\106\5\uffff\1\111\2\uffff\1\110",
            "\1\115\1\uffff\1\114\11\uffff\1\113",
            "\1\117\2\uffff\1\116\2\uffff\1\120\2\uffff\1\121\2\uffff\1\122",
            "\1\123",
            "\1\124",
            "\1\125\4\uffff\1\126",
            "\1\127",
            "\1\130",
            "\1\132\3\uffff\1\131",
            "\1\133\11\uffff\1\134\5\uffff\1\135",
            "\1\136\13\uffff\1\140\5\uffff\1\137",
            "\1\142\3\uffff\1\141\7\uffff\1\146\1\uffff\1\145\1\uffff\1\144"+
            "\2\uffff\1\143",
            "\1\150\4\uffff\1\147",
            "\1\151",
            "\1\152\3\uffff\1\154\5\uffff\1\153",
            "\1\155\2\uffff\1\157\4\uffff\1\156\6\uffff\1\160\1\161",
            "\1\165\6\uffff\1\163\1\166\5\uffff\1\162\2\uffff\1\164",
            "\1\171\1\uffff\1\170\5\uffff\1\167",
            "\1\172\1\uffff\1\174\2\uffff\1\173",
            "\1\175\1\177\5\uffff\1\176",
            "",
            "",
            "\1\u0080",
            "",
            "",
            "",
            "\1\u0084\6\uffff\3\u0084\2\uffff\17\u0084\6\uffff\35\u0084\1"+
            "\uffff\1\u0084\1\uffff\33\u0084\1\uffff\1\u0084\u1082\uffff"+
            "\u0100\u0084\u1e40\uffff\u01c0\u0084\u1c00\uffff\u51a6\u0084"+
            "\u0c5a\uffff\u2bb0\u0084",
            "\1\u0086\1\uffff\1\u0087\11\uffff\1\u0088\1\uffff\1\u0086\1"+
            "\u0089\12\u0086\1\u0089\6\uffff\32\u0086\1\uffff\1\u0089\2\uffff"+
            "\1\u0086\1\uffff\32\u0086",
            "",
            "\1\u008b\76\uffff\1\u008c",
            "",
            "",
            "\1\u008e",
            "\1\u0091\17\uffff\1\u0090",
            "\1\u0091\22\uffff\1\u0093",
            "\1\u0095",
            "",
            "\1\u0097",
            "\1\u0099\1\u009a",
            "\1\u009c",
            "\3\u009f\7\u00a0\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\12\u00a2",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "\12\64\7\uffff\2\64\1\u00a4\27\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u00a6",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u00a8\7\uffff\1\u00a9",
            "\1\u00aa",
            "\1\u00ab",
            "\1\u00ac",
            "\1\u00ad",
            "\1\u00ae",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u00b0",
            "\1\u00b1\3\uffff\1\u00b2",
            "\1\u00b3\1\u00b4",
            "\1\u00b5",
            "\1\u00b6",
            "\1\u00b9\10\uffff\1\u00b7\6\uffff\1\u00b8",
            "\1\u00ba\14\uffff\1\u00bb",
            "\1\u00bc\2\uffff\1\u00bf\5\uffff\1\u00be\6\uffff\1\u00bd",
            "\1\u00c0",
            "\1\u00c1",
            "\1\u00c2",
            "\1\u00c3\5\uffff\1\u00c4",
            "\1\u00c5",
            "\1\u00c6",
            "\1\u00c7",
            "\1\u00c8",
            "\1\u00c9",
            "\1\u00ca",
            "\1\u00cb",
            "\1\u00cc",
            "\1\u00cd",
            "\12\64\7\uffff\3\64\1\u00d2\3\64\1\u00cf\5\64\1\u00d0\4\64\1"+
            "\u00d1\1\u00ce\6\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u00d5",
            "\1\u00d6",
            "\1\u00d8\7\uffff\1\u00d7",
            "\1\u00d9",
            "\1\u00da",
            "\1\u00db",
            "\1\u00dc",
            "\1\u00dd",
            "\1\u00de\1\u00df",
            "\12\64\7\uffff\23\64\1\u00e0\6\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\5\64\1\u00e2\24\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u00e4",
            "\1\u00e5",
            "\12\64\7\uffff\3\64\1\u00e6\26\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u00e8",
            "\12\64\7\uffff\13\64\1\u00e9\16\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u00eb\3\uffff\1\u00ec",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u00ee",
            "\1\u00f2\4\uffff\1\u00ef\7\uffff\1\u00f3\4\uffff\1\u00f1\2\uffff"+
            "\1\u00f0",
            "\1\u00f4",
            "\1\u00f5",
            "\1\u00f7\4\uffff\1\u00f6\2\uffff\1\u00f8",
            "\1\u00f9",
            "\1\u00fa",
            "\1\u00fb",
            "\1\u00fc\15\uffff\1\u00fd",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u00ff",
            "\1\u0100",
            "\1\u0101",
            "\1\u0102",
            "\1\u0103",
            "\1\u0104",
            "\1\u0106\5\uffff\1\u0105",
            "\1\u0107",
            "\1\u0108",
            "\1\u0109",
            "\1\u010a",
            "\1\u010b",
            "\1\u010c",
            "",
            "",
            "",
            "",
            "",
            "",
            "\1\u0086\1\uffff\1\u0087\11\uffff\1\u0088\1\uffff\1\u0086\1"+
            "\u0089\12\u0086\1\u0089\6\uffff\32\u0086\1\uffff\1\u0089\2\uffff"+
            "\1\u0086\1\uffff\32\u0086",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "\1\u010d\12\u00a0\1\u010e\6\uffff\32\64\4\uffff\1\64\1\uffff"+
            "\32\64",
            "\12\u00a0\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0112",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0114",
            "\1\u0115",
            "\1\u0116",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\1\u0118",
            "\1\u011a\3\uffff\1\u0119",
            "\1\u011b",
            "\1\u011c",
            "\1\u011d",
            "\1\u011e",
            "\1\u011f",
            "\1\u0120",
            "\1\u0121\1\uffff\1\u0122",
            "\1\u0123",
            "\1\u0124",
            "\1\u0125",
            "\1\u0126",
            "\1\u0127",
            "\1\u0128",
            "\1\u012a\3\uffff\1\u0129",
            "\1\u012b",
            "\1\u012c",
            "\1\u012d",
            "\1\u012e",
            "\1\u012f",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0131",
            "\1\u0132",
            "\1\u0133",
            "\1\u0134",
            "\1\u0135",
            "\1\u0136",
            "\1\u0137",
            "\1\u0138",
            "\12\64\7\uffff\4\64\1\u0139\11\64\1\u013a\13\64\4\uffff\1\64"+
            "\1\uffff\32\64",
            "\1\u013c",
            "\1\u013d",
            "\1\u013e",
            "\1\u013f",
            "",
            "",
            "\1\u0140",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0142",
            "\1\u0143",
            "\1\u0144",
            "\1\u0145",
            "\1\u0146",
            "\1\u0147",
            "\1\u0148",
            "\1\u0149",
            "\1\u014a",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\1\u014d",
            "\1\u014e",
            "\1\u014f",
            "",
            "\1\u0150",
            "\1\u0151",
            "",
            "\1\u0152",
            "\1\u0153",
            "",
            "\1\u0154",
            "\1\u0155",
            "\1\u0156",
            "\1\u0157",
            "\1\u0158",
            "\1\u0159",
            "\1\u015a",
            "\1\u015b",
            "\1\u015c",
            "\1\u015d",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u015f",
            "\1\u0160",
            "\1\u0161",
            "\1\u0162",
            "\1\u0163",
            "",
            "\1\u0164",
            "\1\u0165",
            "\1\u0166",
            "\1\u0167",
            "\1\u0168",
            "\1\u0169",
            "\1\u016a\25\uffff\1\u016b",
            "\1\u016c",
            "\1\u016e\1\uffff\1\u016d",
            "\1\u016f",
            "\1\u0170",
            "\1\u0172\3\uffff\1\u0171",
            "\1\u0173",
            "\1\u0174",
            "",
            "",
            "",
            "",
            "",
            "\1\u0175",
            "",
            "\1\u0176",
            "\1\u0178\33\uffff\1\u0177",
            "\1\u0179",
            "",
            "\1\u017a",
            "\12\64\7\uffff\1\u017b\31\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u017d",
            "\1\u017e",
            "\1\u017f",
            "\1\u0180",
            "\1\u0181",
            "\1\u0182",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0184",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0186",
            "\1\u0187",
            "\1\u0188",
            "\1\u0189",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u018b",
            "\1\u018c",
            "\1\u018d",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u018f",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0191",
            "\1\u0192",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0194",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0196",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0198",
            "\1\u0199",
            "\1\u019a",
            "\1\u019c\12\uffff\1\u019b",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\1\u019e",
            "\1\u019f",
            "\1\u01a0",
            "\1\u01a1",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u01a6",
            "\1\u01a7",
            "\1\u01a8",
            "\1\u01a9",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u01ab",
            "",
            "",
            "\1\u01ac",
            "\1\u01ad",
            "\1\u01ae",
            "\1\u01af",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u01b1",
            "\1\u01b2",
            "\1\u01b3",
            "\1\u01b4",
            "\1\u01b5",
            "\1\u01b6",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u01b8",
            "\1\u01b9",
            "\1\u01ba",
            "\1\u01bb",
            "\1\u01bc",
            "",
            "\1\u01bd",
            "\1\u01be",
            "\1\u01bf",
            "\1\u01c0",
            "\1\u01c1",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u01c3",
            "\1\u01c4",
            "\12\64\7\uffff\22\64\1\u01c5\7\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u01c8",
            "\1\u01c9",
            "\1\u01ca",
            "\1\u01cb",
            "\1\u01cc",
            "\1\u01cd",
            "\1\u01ce",
            "\1\u01cf",
            "\1\u01d0",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u01d5",
            "\1\u01d6",
            "\1\u01d7",
            "\1\u01d8",
            "\1\u01d9",
            "\1\u01da",
            "",
            "\1\u01db",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u01dd",
            "\1\u01de",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u01e0",
            "",
            "\1\u01e1",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u01e3",
            "\1\u01e4",
            "\1\u01e5",
            "",
            "\1\u01e6",
            "\1\u01e7",
            "\1\u01e8",
            "",
            "\1\u01e9",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u01eb",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\1\u01ed",
            "",
            "\1\u01ee",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u01f0",
            "\1\u01f1",
            "\1\u01f2",
            "",
            "\1\u01f3",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u01f5",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "",
            "",
            "",
            "\1\u01f7",
            "\1\u01f8",
            "\1\u01f9",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\1\u01fb",
            "\1\u01fc",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u01ff",
            "",
            "\1\u0200",
            "\1\u0201",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0203",
            "\1\u0204",
            "\1\u0205",
            "",
            "\1\u0206",
            "\1\u0207",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0209",
            "\1\u020a",
            "\1\u020b",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u020d",
            "\1\u020e",
            "\1\u020f",
            "",
            "\1\u0210",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0212",
            "",
            "",
            "\1\u0213",
            "\1\u0214",
            "\1\u0215",
            "\1\u0216",
            "\1\u0217",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u021a",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "",
            "",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u021d",
            "\1\u021e",
            "\1\u021f",
            "\1\u0220",
            "\1\u0221",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0224",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0226",
            "",
            "\1\u0227",
            "\1\u0228",
            "\1\u0229",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u022b",
            "\1\u022c",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\1\u022f",
            "\1\u0230",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0232",
            "\1\u0233",
            "\1\u0234",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0237",
            "\1\u0238",
            "",
            "\1\u0239",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u023c",
            "\1\u023d",
            "",
            "\1\u023e",
            "\1\u023f",
            "\1\u0240",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0242",
            "",
            "\1\u0243",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0245",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0247",
            "\1\u0248",
            "\1\u0249",
            "",
            "\1\u024a",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u024c",
            "\1\u024d",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "",
            "\1\u0251",
            "\1\u0252",
            "\1\u0253",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0255",
            "",
            "",
            "\1\u0256",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0258",
            "\1\u0259",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\1\u025b",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u025e",
            "",
            "\1\u025f",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "",
            "\1\u0262",
            "\1\u0263",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "",
            "\1\u0265",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0267",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0269",
            "",
            "\1\u026a",
            "\1\u026b",
            "",
            "\1\u026c",
            "",
            "\1\u026d",
            "\1\u026e",
            "\12\64\7\uffff\22\64\1\u026f\7\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0271",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "",
            "",
            "\1\u0274",
            "\1\u0275",
            "\1\u0276",
            "",
            "\1\u0277",
            "\1\u0278",
            "",
            "\1\u0279",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u027d",
            "",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\u027f\1\uffff\32\64",
            "",
            "\1\u0281",
            "",
            "\1\u0282",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\u0285\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0289",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\1\u028b",
            "",
            "",
            "\1\u028c",
            "\1\u028d",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0290",
            "\1\u0291",
            "",
            "",
            "",
            "\1\u0292",
            "",
            "\1\u0293",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0295",
            "",
            "",
            "\1\u0296",
            "",
            "",
            "",
            "\1\u0297",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u0299",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u029d",
            "\1\u029e",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "\1\u02a0",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\1\u02a2",
            "",
            "",
            "",
            "\1\u02a3",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "\1\u02a6",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            "",
            "",
            "\1\u02a8",
            "",
            "\12\64\7\uffff\32\64\4\uffff\1\64\1\uffff\32\64",
            ""
    };

    static final short[] DFA14_eot = DFA.unpackEncodedString(DFA14_eotS);
    static final short[] DFA14_eof = DFA.unpackEncodedString(DFA14_eofS);
    static final char[] DFA14_min = DFA.unpackEncodedStringToUnsignedChars(DFA14_minS);
    static final char[] DFA14_max = DFA.unpackEncodedStringToUnsignedChars(DFA14_maxS);
    static final short[] DFA14_accept = DFA.unpackEncodedString(DFA14_acceptS);
    static final short[] DFA14_special = DFA.unpackEncodedString(DFA14_specialS);
    static final short[][] DFA14_transition;

    static {
        int numStates = DFA14_transitionS.length;
        DFA14_transition = new short[numStates][];
        for (int i=0; i<numStates; i++) {
            DFA14_transition[i] = DFA.unpackEncodedString(DFA14_transitionS[i]);
        }
    }

    class DFA14 extends DFA {

        public DFA14(BaseRecognizer recognizer) {
            this.recognizer = recognizer;
            this.decisionNumber = 14;
            this.eot = DFA14_eot;
            this.eof = DFA14_eof;
            this.min = DFA14_min;
            this.max = DFA14_max;
            this.accept = DFA14_accept;
            this.special = DFA14_special;
            this.transition = DFA14_transition;
        }
        public String getDescription() {
            return "1:1: Tokens : ( ACTION | ADD | ALL | ALTER | AM | AND | AS | ASC | ATTRIBUTE | AUTOCOMMIT | AUTO_INCREMENT | BETWEEN | BIT | BY | CALL | CASE | CACHE | CASCADE | CHANGE | CHAR | CHARACTER | CHECK | CLASS | COMMIT | CONSTRAINT | CREATE | DATE | DECIMAL | DEFERRED | DESC | DEFAULT | DELETE | DIFFERENCE | DISTINCT | DOUBLE | DROP | ELSE | END_STRING | EXCEPT | EXISTS | FILE | FLOAT | FOREIGN | FROM | FUNCTION | GROUP | HAVING | IN | INDEX | INHERIT | INNER | INSERT | INT | INTEGER | INTERSECTION | INTO | IS | JOIN | KEY | LIKE | LIST | LEFT | METHOD | MONETARY | MULTISET | MULTISET_OF | NCHAR | NO | NOT | NULL | NUMERIC | OBJECT | OF | OFF | ON | ONLY | OPTION | OR | ORDER | OUTER | PM | PRECISION | PRIMARY | QUERY | REAL | REFERENCES | RENAME | RESTRICT | RIGHT | ROLLBACK | SELECT | SEQUENCE | SEQUENCE_OF | SET | SHARE | SMALLINT | REVERSE | STRING_STR | SUBCLASS | SUPERCLASS | TABLE | TIME | TIMESTAMP | THEN | TRIGGER | TRIGGERS | TO | VALUES | UNION | UNIQUE | UPDATE | USING | VARCHAR | VARYING | VCLASS | VIEW | WHEN | WHERE | WITH | WORK | END | COMMA | STAR | STARTBRACE | ENDBRACE | DOT | QUOTA | DBQUOTA | EQUAL | CONNECT | DOLLAR | Q_MARK | T156 | T157 | T158 | T159 | T160 | T161 | T162 | T163 | T164 | T165 | T166 | T167 | T168 | T169 | T170 | T171 | T172 | T173 | T174 | DATE_FORMAT | TIME_FORMAT | LENGTH | DECIMALLITERAL | STARTBRACKET | ENDBRACKET | STRING | MARKS | KOREA | CHINESE | JAPAN | COLUMN | ID | PATH | WS | ML_COMMENT );";
        }
    }
 

}