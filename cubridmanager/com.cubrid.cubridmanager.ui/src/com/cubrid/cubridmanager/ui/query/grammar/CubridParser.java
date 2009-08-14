// $ANTLR 3.0.1 Cubrid.g 2009-07-10 16:31:45
package com.cubrid.cubridmanager.ui.query.grammar;

import org.antlr.runtime.BaseRecognizer;
import org.antlr.runtime.BitSet;
import org.antlr.runtime.DFA;
import org.antlr.runtime.EarlyExitException;
import org.antlr.runtime.IntStream;
import org.antlr.runtime.MismatchedSetException;
import org.antlr.runtime.NoViableAltException;
import org.antlr.runtime.Parser;
import org.antlr.runtime.ParserRuleReturnScope;
import org.antlr.runtime.RecognitionException;
import org.antlr.runtime.Token;
import org.antlr.runtime.TokenStream;
import org.antlr.runtime.tree.CommonTreeAdaptor;
import org.antlr.runtime.tree.RewriteEarlyExitException;
import org.antlr.runtime.tree.RewriteRuleSubtreeStream;
import org.antlr.runtime.tree.RewriteRuleTokenStream;
import org.antlr.runtime.tree.TreeAdaptor;

public class CubridParser extends Parser {
    public static final String[] tokenNames = new String[] {
        "<invalid>", "<EOR>", "<DOWN>", "<UP>", "ACTION", "ADD", "ALL", "ALTER", "AM", "AND", "AS", "ASC", "ATTRIBUTE", "AUTOCOMMIT", "AUTO_INCREMENT", "BETWEEN", "BIT", "BY", "CALL", "CASE", "CACHE", "CASCADE", "CHANGE", "CHAR", "CHARACTER", "CHECK", "CLASS", "COMMIT", "CONSTRAINT", "CREATE", "DATE", "DECIMAL", "DEFERRED", "DESC", "DEFAULT", "DELETE", "DIFFERENCE", "DISTINCT", "DOUBLE", "DROP", "ELSE", "END_STRING", "EXCEPT", "EXISTS", "FILE", "FLOAT", "FOREIGN", "FROM", "FUNCTION", "GROUP", "HAVING", "IN", "INDEX", "INHERIT", "INNER", "INSERT", "INT", "INTEGER", "INTERSECTION", "INTO", "IS", "JOIN", "KEY", "LIKE", "LIST", "LEFT", "METHOD", "MONETARY", "MULTISET", "MULTISET_OF", "NCHAR", "NO", "NOT", "NULL", "NUMERIC", "OBJECT", "OF", "OFF", "ON", "ONLY", "OPTION", "OR", "ORDER", "OUTER", "PM", "PRECISION", "PRIMARY", "QUERY", "REAL", "REFERENCES", "RENAME", "RESTRICT", "RIGHT", "ROLLBACK", "SELECT", "SEQUENCE", "SEQUENCE_OF", "SET", "SHARE", "SMALLINT", "REVERSE", "STRING_STR", "SUBCLASS", "SUPERCLASS", "TABLE", "TIME", "TIMESTAMP", "THEN", "TRIGGER", "TRIGGERS", "TO", "VALUES", "UNION", "UNIQUE", "UPDATE", "USING", "VARCHAR", "VARYING", "VCLASS", "VIEW", "WHEN", "WHERE", "WITH", "WORK", "ENTER", "TAB", "UNTAB", "CLEAR", "END", "COMMA", "STAR", "STARTBRACE", "ENDBRACE", "DOT", "QUOTA", "DBQUOTA", "EQUAL", "CONNECT", "DOLLAR", "Q_MARK", "PATH", "ID", "COLUMN", "STARTBRACKET", "ENDBRACKET", "DECIMALLITERAL", "LENGTH", "DATE_FORMAT", "TIME_FORMAT", "STRING", "MARKS", "KOREA", "CHINESE", "JAPAN", "WS", "ML_COMMENT", "'+='", "'-='", "'*='", "'/='", "'&='", "'|='", "'^='", "'%='", "'|'", "'&'", "'+'", "'<>'", "'<='", "'>='", "'<'", "'>'", "'-'", "'/'", "'%'"
    };
    public static final int DECIMALLITERAL=145;
    public static final int FUNCTION=48;
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
    public static final int CACHE=20;
    public static final int EOF=-1;
    public static final int FOREIGN=46;
    public static final int CHARACTER=24;
    public static final int ACTION=4;
    public static final int CLEAR=127;
    public static final int CREATE=29;
    public static final int QUOTA=134;
    public static final int INSERT=55;
    public static final int USING=115;
    public static final int STRING_STR=101;
    public static final int CONNECT=137;
    public static final int DATE_FORMAT=147;
    public static final int PATH=140;
    public static final int INTERSECTION=58;
    public static final int OFF=77;
    public static final int AUTO_INCREMENT=14;
    public static final int DOUBLE=38;
    public static final int CHINESE=152;
    public static final int SHARE=98;
    public static final int WORK=123;
    public static final int SELECT=94;
    public static final int DBQUOTA=135;
    public static final int INTO=59;
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
    public static final int TIME_FORMAT=148;
    public static final int NCHAR=70;
    public static final int ORDER=82;
    public static final int ONLY=79;
    public static final int STARTBRACKET=143;
    public static final int ATTRIBUTE=12;
    public static final int UPDATE=114;
    public static final int TABLE=104;
    public static final int VARCHAR=116;
    public static final int DEFERRED=32;
    public static final int FLOAT=45;
    public static final int ID=141;
    public static final int AND=9;
    public static final int MONETARY=67;
    public static final int LENGTH=146;
    public static final int END_STRING=41;
    public static final int ML_COMMENT=155;
    public static final int TIME=105;
    public static final int AS=10;
    public static final int INDEX=52;
    public static final int THEN=107;
    public static final int IN=51;
    public static final int JAPAN=153;
    public static final int OBJECT=75;
    public static final int COMMA=129;
    public static final int REFERENCES=89;
    public static final int IS=60;
    public static final int LEFT=65;
    public static final int EQUAL=136;
    public static final int ALL=6;
    public static final int COLUMN=142;
    public static final int VCLASS=118;
    public static final int SUPERCLASS=103;
    public static final int SEQUENCE_OF=96;
    public static final int EXISTS=43;
    public static final int DOT=133;
    public static final int DIFFERENCE=36;
    public static final int WITH=122;
    public static final int AM=8;
    public static final int LIKE=63;
    public static final int ADD=5;
    public static final int INTEGER=57;
    public static final int OUTER=83;
    public static final int BY=17;
    public static final int TO=110;
    public static final int INHERIT=53;
    public static final int DEFAULT=34;
    public static final int VALUES=111;
    public static final int TAB=125;
    public static final int SUBCLASS=102;
    public static final int SET=97;
    public static final int RIGHT=92;
    public static final int HAVING=50;
    public static final int JOIN=61;
    public static final int UNION=112;
    public static final int CHANGE=22;
    public static final int COMMIT=27;
    public static final int DECIMAL=31;
    public static final int DROP=39;
    public static final int WHEN=120;
    public static final int ENTER=124;
    public static final int BIT=16;
    public static final int TRIGGERS=109;
    public static final int DESC=33;
    public static final int DATE=30;
    public static final int BETWEEN=15;
    public static final int METHOD=66;
    public static final int KOREA=151;
    public static final int STRING=149;
    public static final int SMALLINT=99;

        public CubridParser(TokenStream input) {
            super(input);
        }
        
    protected TreeAdaptor adaptor = new CommonTreeAdaptor();

    public void setTreeAdaptor(TreeAdaptor adaptor) {
        this.adaptor = adaptor;
    }
    public TreeAdaptor getTreeAdaptor() {
        return adaptor;
    }

    public String[] getTokenNames() { return tokenNames; }
    public String getGrammarFileName() { return "Cubrid.g"; }




    	String temp = "";

    protected void mismatch(IntStream input, int ttype, BitSet follow) throws RecognitionException {
    	throw new RecognitionException();
    	}

    public void recoverFromMismatchedSet(IntStream input,
                                         RecognitionException e,
                                         BitSet follow)
        throws RecognitionException
    {
        throw e;
    }


    public static class execute_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start execute
    // Cubrid.g:181:1: execute : ( ( PATH | select_statement | insert | update | delete | create | create_virtual_class | alter | drop | call | autocommit | rollback | commit ) end )* ;
    public final execute_return execute() throws RecognitionException {
        execute_return retval = new execute_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token PATH1=null;
        select_statement_return select_statement2 = null;

        insert_return insert3 = null;

        update_return update4 = null;

        delete_return delete5 = null;

        create_return create6 = null;

        create_virtual_class_return create_virtual_class7 = null;

        alter_return alter8 = null;

        drop_return drop9 = null;

        call_return call10 = null;

        autocommit_return autocommit11 = null;

        rollback_return rollback12 = null;

        commit_return commit13 = null;

        end_return end14 = null;


        Object PATH1_tree=null;

        try {
            // Cubrid.g:181:9: ( ( ( PATH | select_statement | insert | update | delete | create | create_virtual_class | alter | drop | call | autocommit | rollback | commit ) end )* )
            // Cubrid.g:182:2: ( ( PATH | select_statement | insert | update | delete | create | create_virtual_class | alter | drop | call | autocommit | rollback | commit ) end )*
            {
            root_0 = (Object)adaptor.nil();

            // Cubrid.g:182:2: ( ( PATH | select_statement | insert | update | delete | create | create_virtual_class | alter | drop | call | autocommit | rollback | commit ) end )*
            loop2:
            do {
                int alt2=2;
                int LA2_0 = input.LA(1);

                if ( (LA2_0==ALTER||LA2_0==AUTOCOMMIT||LA2_0==CALL||LA2_0==COMMIT||LA2_0==CREATE||LA2_0==DELETE||LA2_0==DROP||LA2_0==INSERT||(LA2_0>=ROLLBACK && LA2_0<=SELECT)||LA2_0==UPDATE||LA2_0==PATH) ) {
                    alt2=1;
                }


                switch (alt2) {
            	case 1 :
            	    // Cubrid.g:182:3: ( PATH | select_statement | insert | update | delete | create | create_virtual_class | alter | drop | call | autocommit | rollback | commit ) end
            	    {
            	    // Cubrid.g:182:3: ( PATH | select_statement | insert | update | delete | create | create_virtual_class | alter | drop | call | autocommit | rollback | commit )
            	    int alt1=13;
            	    switch ( input.LA(1) ) {
            	    case PATH:
            	        {
            	        alt1=1;
            	        }
            	        break;
            	    case SELECT:
            	        {
            	        alt1=2;
            	        }
            	        break;
            	    case INSERT:
            	        {
            	        alt1=3;
            	        }
            	        break;
            	    case UPDATE:
            	        {
            	        alt1=4;
            	        }
            	        break;
            	    case DELETE:
            	        {
            	        alt1=5;
            	        }
            	        break;
            	    case CREATE:
            	        {
            	        int LA1_6 = input.LA(2);

            	        if ( (LA1_6==CLASS||LA1_6==TABLE) ) {
            	            alt1=6;
            	        }
            	        else if ( ((LA1_6>=VCLASS && LA1_6<=VIEW)) ) {
            	            alt1=7;
            	        }
            	        else {
            	            NoViableAltException nvae =
            	                new NoViableAltException("182:3: ( PATH | select_statement | insert | update | delete | create | create_virtual_class | alter | drop | call | autocommit | rollback | commit )", 1, 6, input);

            	            throw nvae;
            	        }
            	        }
            	        break;
            	    case ALTER:
            	        {
            	        alt1=8;
            	        }
            	        break;
            	    case DROP:
            	        {
            	        alt1=9;
            	        }
            	        break;
            	    case CALL:
            	        {
            	        alt1=10;
            	        }
            	        break;
            	    case AUTOCOMMIT:
            	        {
            	        alt1=11;
            	        }
            	        break;
            	    case ROLLBACK:
            	        {
            	        alt1=12;
            	        }
            	        break;
            	    case COMMIT:
            	        {
            	        alt1=13;
            	        }
            	        break;
            	    default:
            	        NoViableAltException nvae =
            	            new NoViableAltException("182:3: ( PATH | select_statement | insert | update | delete | create | create_virtual_class | alter | drop | call | autocommit | rollback | commit )", 1, 0, input);

            	        throw nvae;
            	    }

            	    switch (alt1) {
            	        case 1 :
            	            // Cubrid.g:182:4: PATH
            	            {
            	            PATH1=(Token)input.LT(1);
            	            match(input,PATH,FOLLOW_PATH_in_execute1153); 
            	            PATH1_tree = (Object)adaptor.create(PATH1);
            	            adaptor.addChild(root_0, PATH1_tree);


            	            }
            	            break;
            	        case 2 :
            	            // Cubrid.g:182:10: select_statement
            	            {
            	            pushFollow(FOLLOW_select_statement_in_execute1156);
            	            select_statement2=select_statement();
            	            _fsp--;

            	            adaptor.addChild(root_0, select_statement2.getTree());

            	            }
            	            break;
            	        case 3 :
            	            // Cubrid.g:182:29: insert
            	            {
            	            pushFollow(FOLLOW_insert_in_execute1160);
            	            insert3=insert();
            	            _fsp--;

            	            adaptor.addChild(root_0, insert3.getTree());

            	            }
            	            break;
            	        case 4 :
            	            // Cubrid.g:182:39: update
            	            {
            	            pushFollow(FOLLOW_update_in_execute1165);
            	            update4=update();
            	            _fsp--;

            	            adaptor.addChild(root_0, update4.getTree());

            	            }
            	            break;
            	        case 5 :
            	            // Cubrid.g:182:48: delete
            	            {
            	            pushFollow(FOLLOW_delete_in_execute1169);
            	            delete5=delete();
            	            _fsp--;

            	            adaptor.addChild(root_0, delete5.getTree());

            	            }
            	            break;
            	        case 6 :
            	            // Cubrid.g:182:57: create
            	            {
            	            pushFollow(FOLLOW_create_in_execute1173);
            	            create6=create();
            	            _fsp--;

            	            adaptor.addChild(root_0, create6.getTree());

            	            }
            	            break;
            	        case 7 :
            	            // Cubrid.g:182:66: create_virtual_class
            	            {
            	            pushFollow(FOLLOW_create_virtual_class_in_execute1177);
            	            create_virtual_class7=create_virtual_class();
            	            _fsp--;

            	            adaptor.addChild(root_0, create_virtual_class7.getTree());

            	            }
            	            break;
            	        case 8 :
            	            // Cubrid.g:182:89: alter
            	            {
            	            pushFollow(FOLLOW_alter_in_execute1181);
            	            alter8=alter();
            	            _fsp--;

            	            adaptor.addChild(root_0, alter8.getTree());

            	            }
            	            break;
            	        case 9 :
            	            // Cubrid.g:182:97: drop
            	            {
            	            pushFollow(FOLLOW_drop_in_execute1185);
            	            drop9=drop();
            	            _fsp--;

            	            adaptor.addChild(root_0, drop9.getTree());

            	            }
            	            break;
            	        case 10 :
            	            // Cubrid.g:182:104: call
            	            {
            	            pushFollow(FOLLOW_call_in_execute1189);
            	            call10=call();
            	            _fsp--;

            	            adaptor.addChild(root_0, call10.getTree());

            	            }
            	            break;
            	        case 11 :
            	            // Cubrid.g:182:111: autocommit
            	            {
            	            pushFollow(FOLLOW_autocommit_in_execute1193);
            	            autocommit11=autocommit();
            	            _fsp--;

            	            adaptor.addChild(root_0, autocommit11.getTree());

            	            }
            	            break;
            	        case 12 :
            	            // Cubrid.g:182:123: rollback
            	            {
            	            pushFollow(FOLLOW_rollback_in_execute1196);
            	            rollback12=rollback();
            	            _fsp--;

            	            adaptor.addChild(root_0, rollback12.getTree());

            	            }
            	            break;
            	        case 13 :
            	            // Cubrid.g:182:134: commit
            	            {
            	            pushFollow(FOLLOW_commit_in_execute1200);
            	            commit13=commit();
            	            _fsp--;

            	            adaptor.addChild(root_0, commit13.getTree());

            	            }
            	            break;

            	    }

            	    pushFollow(FOLLOW_end_in_execute1203);
            	    end14=end();
            	    _fsp--;

            	    adaptor.addChild(root_0, end14.getTree());

            	    }
            	    break;

            	default :
            	    break loop2;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end execute

    public static class autocommit_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start autocommit
    // Cubrid.g:185:1: autocommit : AUTOCOMMIT ( OFF | ON ) ;
    public final autocommit_return autocommit() throws RecognitionException {
        autocommit_return retval = new autocommit_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token AUTOCOMMIT15=null;
        Token set16=null;

        Object AUTOCOMMIT15_tree=null;
        Object set16_tree=null;

        try {
            // Cubrid.g:185:12: ( AUTOCOMMIT ( OFF | ON ) )
            // Cubrid.g:186:2: AUTOCOMMIT ( OFF | ON )
            {
            root_0 = (Object)adaptor.nil();

            AUTOCOMMIT15=(Token)input.LT(1);
            match(input,AUTOCOMMIT,FOLLOW_AUTOCOMMIT_in_autocommit1218); 
            AUTOCOMMIT15_tree = (Object)adaptor.create(AUTOCOMMIT15);
            adaptor.addChild(root_0, AUTOCOMMIT15_tree);

            set16=(Token)input.LT(1);
            if ( (input.LA(1)>=OFF && input.LA(1)<=ON) ) {
                input.consume();
                adaptor.addChild(root_0, adaptor.create(set16));
                errorRecovery=false;
            }
            else {
                MismatchedSetException mse =
                    new MismatchedSetException(null,input);
                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_autocommit1220);    throw mse;
            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end autocommit

    public static class rollback_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start rollback
    // Cubrid.g:188:1: rollback : ROLLBACK ;
    public final rollback_return rollback() throws RecognitionException {
        rollback_return retval = new rollback_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ROLLBACK17=null;

        Object ROLLBACK17_tree=null;

        try {
            // Cubrid.g:188:9: ( ROLLBACK )
            // Cubrid.g:189:2: ROLLBACK
            {
            root_0 = (Object)adaptor.nil();

            ROLLBACK17=(Token)input.LT(1);
            match(input,ROLLBACK,FOLLOW_ROLLBACK_in_rollback1235); 
            ROLLBACK17_tree = (Object)adaptor.create(ROLLBACK17);
            adaptor.addChild(root_0, ROLLBACK17_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end rollback

    public static class commit_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start commit
    // Cubrid.g:192:1: commit : COMMIT WORK ;
    public final commit_return commit() throws RecognitionException {
        commit_return retval = new commit_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMIT18=null;
        Token WORK19=null;

        Object COMMIT18_tree=null;
        Object WORK19_tree=null;

        try {
            // Cubrid.g:192:7: ( COMMIT WORK )
            // Cubrid.g:193:2: COMMIT WORK
            {
            root_0 = (Object)adaptor.nil();

            COMMIT18=(Token)input.LT(1);
            match(input,COMMIT,FOLLOW_COMMIT_in_commit1247); 
            COMMIT18_tree = (Object)adaptor.create(COMMIT18);
            adaptor.addChild(root_0, COMMIT18_tree);

            WORK19=(Token)input.LT(1);
            match(input,WORK,FOLLOW_WORK_in_commit1249); 
            WORK19_tree = (Object)adaptor.create(WORK19);
            adaptor.addChild(root_0, WORK19_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end commit

    public static class select_statement_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start select_statement
    // Cubrid.g:196:1: select_statement : query_specification ( UNION ( ALL )? query_specification )* ( ORDER BY sort_specification_comma_list )? ;
    public final select_statement_return select_statement() throws RecognitionException {
        select_statement_return retval = new select_statement_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token UNION21=null;
        Token ALL22=null;
        Token ORDER24=null;
        Token BY25=null;
        query_specification_return query_specification20 = null;

        query_specification_return query_specification23 = null;

        sort_specification_comma_list_return sort_specification_comma_list26 = null;


        Object UNION21_tree=null;
        Object ALL22_tree=null;
        Object ORDER24_tree=null;
        Object BY25_tree=null;

        try {
            // Cubrid.g:196:17: ( query_specification ( UNION ( ALL )? query_specification )* ( ORDER BY sort_specification_comma_list )? )
            // Cubrid.g:197:2: query_specification ( UNION ( ALL )? query_specification )* ( ORDER BY sort_specification_comma_list )?
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_query_specification_in_select_statement1260);
            query_specification20=query_specification();
            _fsp--;

            adaptor.addChild(root_0, query_specification20.getTree());
            // Cubrid.g:197:22: ( UNION ( ALL )? query_specification )*
            loop4:
            do {
                int alt4=2;
                int LA4_0 = input.LA(1);

                if ( (LA4_0==UNION) ) {
                    alt4=1;
                }


                switch (alt4) {
            	case 1 :
            	    // Cubrid.g:197:23: UNION ( ALL )? query_specification
            	    {
            	    UNION21=(Token)input.LT(1);
            	    match(input,UNION,FOLLOW_UNION_in_select_statement1263); 
            	    UNION21_tree = (Object)adaptor.create(UNION21);
            	    adaptor.addChild(root_0, UNION21_tree);

            	    // Cubrid.g:197:29: ( ALL )?
            	    int alt3=2;
            	    int LA3_0 = input.LA(1);

            	    if ( (LA3_0==ALL) ) {
            	        alt3=1;
            	    }
            	    switch (alt3) {
            	        case 1 :
            	            // Cubrid.g:197:29: ALL
            	            {
            	            ALL22=(Token)input.LT(1);
            	            match(input,ALL,FOLLOW_ALL_in_select_statement1265); 
            	            ALL22_tree = (Object)adaptor.create(ALL22);
            	            adaptor.addChild(root_0, ALL22_tree);


            	            }
            	            break;

            	    }

            	    pushFollow(FOLLOW_query_specification_in_select_statement1268);
            	    query_specification23=query_specification();
            	    _fsp--;

            	    adaptor.addChild(root_0, query_specification23.getTree());

            	    }
            	    break;

            	default :
            	    break loop4;
                }
            } while (true);

            // Cubrid.g:198:2: ( ORDER BY sort_specification_comma_list )?
            int alt5=2;
            int LA5_0 = input.LA(1);

            if ( (LA5_0==ORDER) ) {
                alt5=1;
            }
            switch (alt5) {
                case 1 :
                    // Cubrid.g:198:3: ORDER BY sort_specification_comma_list
                    {
                    ORDER24=(Token)input.LT(1);
                    match(input,ORDER,FOLLOW_ORDER_in_select_statement1274); 
                    ORDER24_tree = (Object)adaptor.create(ORDER24);
                    adaptor.addChild(root_0, ORDER24_tree);

                    BY25=(Token)input.LT(1);
                    match(input,BY,FOLLOW_BY_in_select_statement1276); 
                    BY25_tree = (Object)adaptor.create(BY25);
                    adaptor.addChild(root_0, BY25_tree);

                    pushFollow(FOLLOW_sort_specification_comma_list_in_select_statement1278);
                    sort_specification_comma_list26=sort_specification_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, sort_specification_comma_list26.getTree());

                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end select_statement

    public static class query_specification_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start query_specification
    // Cubrid.g:201:1: query_specification : select ( ( qualifier )? ) select_expressions ( ( TO | INTO ) variable_comma_list )? from table_specification_comma_list where_clause ( USING INDEX index_comma_list )? ( group_by path_expression_comma_list )? ( HAVING search_condition )? ;
    public final query_specification_return query_specification() throws RecognitionException {
        query_specification_return retval = new query_specification_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set30=null;
        Token USING35=null;
        Token INDEX36=null;
        Token HAVING40=null;
        select_return select27 = null;

        qualifier_return qualifier28 = null;

        select_expressions_return select_expressions29 = null;

        variable_comma_list_return variable_comma_list31 = null;

        from_return from32 = null;

        table_specification_comma_list_return table_specification_comma_list33 = null;

        where_clause_return where_clause34 = null;

        index_comma_list_return index_comma_list37 = null;

        group_by_return group_by38 = null;

        path_expression_comma_list_return path_expression_comma_list39 = null;

        search_condition_return search_condition41 = null;


        Object set30_tree=null;
        Object USING35_tree=null;
        Object INDEX36_tree=null;
        Object HAVING40_tree=null;

        try {
            // Cubrid.g:201:20: ( select ( ( qualifier )? ) select_expressions ( ( TO | INTO ) variable_comma_list )? from table_specification_comma_list where_clause ( USING INDEX index_comma_list )? ( group_by path_expression_comma_list )? ( HAVING search_condition )? )
            // Cubrid.g:202:2: select ( ( qualifier )? ) select_expressions ( ( TO | INTO ) variable_comma_list )? from table_specification_comma_list where_clause ( USING INDEX index_comma_list )? ( group_by path_expression_comma_list )? ( HAVING search_condition )?
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_select_in_query_specification1291);
            select27=select();
            _fsp--;

            adaptor.addChild(root_0, select27.getTree());
            // Cubrid.g:202:9: ( ( qualifier )? )
            // Cubrid.g:202:10: ( qualifier )?
            {
            // Cubrid.g:202:10: ( qualifier )?
            int alt6=2;
            int LA6_0 = input.LA(1);

            if ( (LA6_0==ALL||LA6_0==DISTINCT||LA6_0==UNIQUE) ) {
                alt6=1;
            }
            switch (alt6) {
                case 1 :
                    // Cubrid.g:202:10: qualifier
                    {
                    pushFollow(FOLLOW_qualifier_in_query_specification1294);
                    qualifier28=qualifier();
                    _fsp--;

                    adaptor.addChild(root_0, qualifier28.getTree());

                    }
                    break;

            }


            }

            pushFollow(FOLLOW_select_expressions_in_query_specification1298);
            select_expressions29=select_expressions();
            _fsp--;

            adaptor.addChild(root_0, select_expressions29.getTree());
            // Cubrid.g:203:2: ( ( TO | INTO ) variable_comma_list )?
            int alt7=2;
            int LA7_0 = input.LA(1);

            if ( (LA7_0==INTO||LA7_0==TO) ) {
                alt7=1;
            }
            switch (alt7) {
                case 1 :
                    // Cubrid.g:203:4: ( TO | INTO ) variable_comma_list
                    {
                    set30=(Token)input.LT(1);
                    if ( input.LA(1)==INTO||input.LA(1)==TO ) {
                        input.consume();
                        adaptor.addChild(root_0, adaptor.create(set30));
                        errorRecovery=false;
                    }
                    else {
                        MismatchedSetException mse =
                            new MismatchedSetException(null,input);
                        recoverFromMismatchedSet(input,mse,FOLLOW_set_in_query_specification1304);    throw mse;
                    }

                    pushFollow(FOLLOW_variable_comma_list_in_query_specification1312);
                    variable_comma_list31=variable_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, variable_comma_list31.getTree());

                    }
                    break;

            }

            pushFollow(FOLLOW_from_in_query_specification1317);
            from32=from();
            _fsp--;

            adaptor.addChild(root_0, from32.getTree());
            pushFollow(FOLLOW_table_specification_comma_list_in_query_specification1319);
            table_specification_comma_list33=table_specification_comma_list();
            _fsp--;

            adaptor.addChild(root_0, table_specification_comma_list33.getTree());
            pushFollow(FOLLOW_where_clause_in_query_specification1322);
            where_clause34=where_clause();
            _fsp--;

            adaptor.addChild(root_0, where_clause34.getTree());
            // Cubrid.g:206:2: ( USING INDEX index_comma_list )?
            int alt8=2;
            int LA8_0 = input.LA(1);

            if ( (LA8_0==USING) ) {
                alt8=1;
            }
            switch (alt8) {
                case 1 :
                    // Cubrid.g:206:3: USING INDEX index_comma_list
                    {
                    USING35=(Token)input.LT(1);
                    match(input,USING,FOLLOW_USING_in_query_specification1327); 
                    USING35_tree = (Object)adaptor.create(USING35);
                    adaptor.addChild(root_0, USING35_tree);

                    INDEX36=(Token)input.LT(1);
                    match(input,INDEX,FOLLOW_INDEX_in_query_specification1329); 
                    INDEX36_tree = (Object)adaptor.create(INDEX36);
                    adaptor.addChild(root_0, INDEX36_tree);

                    pushFollow(FOLLOW_index_comma_list_in_query_specification1331);
                    index_comma_list37=index_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, index_comma_list37.getTree());

                    }
                    break;

            }

            // Cubrid.g:207:2: ( group_by path_expression_comma_list )?
            int alt9=2;
            int LA9_0 = input.LA(1);

            if ( (LA9_0==GROUP) ) {
                alt9=1;
            }
            switch (alt9) {
                case 1 :
                    // Cubrid.g:207:3: group_by path_expression_comma_list
                    {
                    pushFollow(FOLLOW_group_by_in_query_specification1337);
                    group_by38=group_by();
                    _fsp--;

                    adaptor.addChild(root_0, group_by38.getTree());
                    pushFollow(FOLLOW_path_expression_comma_list_in_query_specification1339);
                    path_expression_comma_list39=path_expression_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, path_expression_comma_list39.getTree());

                    }
                    break;

            }

            // Cubrid.g:208:2: ( HAVING search_condition )?
            int alt10=2;
            int LA10_0 = input.LA(1);

            if ( (LA10_0==HAVING) ) {
                alt10=1;
            }
            switch (alt10) {
                case 1 :
                    // Cubrid.g:208:3: HAVING search_condition
                    {
                    HAVING40=(Token)input.LT(1);
                    match(input,HAVING,FOLLOW_HAVING_in_query_specification1345); 
                    HAVING40_tree = (Object)adaptor.create(HAVING40);
                    adaptor.addChild(root_0, HAVING40_tree);

                    pushFollow(FOLLOW_search_condition_in_query_specification1347);
                    search_condition41=search_condition();
                    _fsp--;

                    adaptor.addChild(root_0, search_condition41.getTree());

                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end query_specification

    public static class qualifier_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start qualifier
    // Cubrid.g:211:1: qualifier : ( ALL | DISTINCT | UNIQUE );
    public final qualifier_return qualifier() throws RecognitionException {
        qualifier_return retval = new qualifier_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set42=null;

        Object set42_tree=null;

        try {
            // Cubrid.g:211:10: ( ALL | DISTINCT | UNIQUE )
            // Cubrid.g:
            {
            root_0 = (Object)adaptor.nil();

            set42=(Token)input.LT(1);
            if ( input.LA(1)==ALL||input.LA(1)==DISTINCT||input.LA(1)==UNIQUE ) {
                input.consume();
                adaptor.addChild(root_0, adaptor.create(set42));
                errorRecovery=false;
            }
            else {
                MismatchedSetException mse =
                    new MismatchedSetException(null,input);
                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_qualifier0);    throw mse;
            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end qualifier

    public static class select_expressions_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start select_expressions
    // Cubrid.g:215:1: select_expressions : expression_comma_list ;
    public final select_expressions_return select_expressions() throws RecognitionException {
        select_expressions_return retval = new select_expressions_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        expression_comma_list_return expression_comma_list43 = null;



        try {
            // Cubrid.g:215:19: ( expression_comma_list )
            // Cubrid.g:216:2: expression_comma_list
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_expression_comma_list_in_select_expressions1379);
            expression_comma_list43=expression_comma_list();
            _fsp--;

            adaptor.addChild(root_0, expression_comma_list43.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end select_expressions

    public static class expression_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start expression_comma_list
    // Cubrid.g:220:1: expression_comma_list : expression_co ( COMMA expression_co )* -> expression_co ( COMMA ENTER expression_co )* ENTER ;
    public final expression_comma_list_return expression_comma_list() throws RecognitionException {
        expression_comma_list_return retval = new expression_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA45=null;
        expression_co_return expression_co44 = null;

        expression_co_return expression_co46 = null;


        Object COMMA45_tree=null;
        RewriteRuleTokenStream stream_COMMA=new RewriteRuleTokenStream(adaptor,"token COMMA");
        RewriteRuleSubtreeStream stream_expression_co=new RewriteRuleSubtreeStream(adaptor,"rule expression_co");
        try {
            // Cubrid.g:220:22: ( expression_co ( COMMA expression_co )* -> expression_co ( COMMA ENTER expression_co )* ENTER )
            // Cubrid.g:221:2: expression_co ( COMMA expression_co )*
            {
            pushFollow(FOLLOW_expression_co_in_expression_comma_list1392);
            expression_co44=expression_co();
            _fsp--;

            stream_expression_co.add(expression_co44.getTree());
            // Cubrid.g:221:16: ( COMMA expression_co )*
            loop11:
            do {
                int alt11=2;
                int LA11_0 = input.LA(1);

                if ( (LA11_0==COMMA) ) {
                    alt11=1;
                }


                switch (alt11) {
            	case 1 :
            	    // Cubrid.g:221:17: COMMA expression_co
            	    {
            	    COMMA45=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_expression_comma_list1395); 
            	    stream_COMMA.add(COMMA45);

            	    pushFollow(FOLLOW_expression_co_in_expression_comma_list1397);
            	    expression_co46=expression_co();
            	    _fsp--;

            	    stream_expression_co.add(expression_co46.getTree());

            	    }
            	    break;

            	default :
            	    break loop11;
                }
            } while (true);


            // AST REWRITE
            // elements: COMMA, expression_co, expression_co
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 221:39: -> expression_co ( COMMA ENTER expression_co )* ENTER
            {
                adaptor.addChild(root_0, stream_expression_co.next());
                // Cubrid.g:221:56: ( COMMA ENTER expression_co )*
                while ( stream_COMMA.hasNext()||stream_expression_co.hasNext() ) {
                    adaptor.addChild(root_0, stream_COMMA.next());
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, stream_expression_co.next());

                }
                stream_COMMA.reset();
                stream_expression_co.reset();
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end expression_comma_list

    public static class expression_co_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start expression_co
    // Cubrid.g:224:1: expression_co : expression ( ( correlation )? ) ;
    public final expression_co_return expression_co() throws RecognitionException {
        expression_co_return retval = new expression_co_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        expression_return expression47 = null;

        correlation_return correlation48 = null;



        try {
            // Cubrid.g:224:15: ( expression ( ( correlation )? ) )
            // Cubrid.g:225:2: expression ( ( correlation )? )
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_expression_in_expression_co1426);
            expression47=expression();
            _fsp--;

            adaptor.addChild(root_0, expression47.getTree());
            // Cubrid.g:225:13: ( ( correlation )? )
            // Cubrid.g:225:14: ( correlation )?
            {
            // Cubrid.g:225:14: ( correlation )?
            int alt12=2;
            int LA12_0 = input.LA(1);

            if ( (LA12_0==AS||LA12_0==ID) ) {
                alt12=1;
            }
            switch (alt12) {
                case 1 :
                    // Cubrid.g:225:14: correlation
                    {
                    pushFollow(FOLLOW_correlation_in_expression_co1429);
                    correlation48=correlation();
                    _fsp--;

                    adaptor.addChild(root_0, correlation48.getTree());

                    }
                    break;

            }


            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end expression_co

    public static class attribute_name_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start attribute_name
    // Cubrid.g:230:1: attribute_name : ( ID ( DOT ( ID | COLUMN ) )? | COLUMN );
    public final attribute_name_return attribute_name() throws RecognitionException {
        attribute_name_return retval = new attribute_name_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ID49=null;
        Token DOT50=null;
        Token set51=null;
        Token COLUMN52=null;

        Object ID49_tree=null;
        Object DOT50_tree=null;
        Object set51_tree=null;
        Object COLUMN52_tree=null;

        try {
            // Cubrid.g:230:15: ( ID ( DOT ( ID | COLUMN ) )? | COLUMN )
            int alt14=2;
            int LA14_0 = input.LA(1);

            if ( (LA14_0==ID) ) {
                alt14=1;
            }
            else if ( (LA14_0==COLUMN) ) {
                alt14=2;
            }
            else {
                NoViableAltException nvae =
                    new NoViableAltException("230:1: attribute_name : ( ID ( DOT ( ID | COLUMN ) )? | COLUMN );", 14, 0, input);

                throw nvae;
            }
            switch (alt14) {
                case 1 :
                    // Cubrid.g:231:2: ID ( DOT ( ID | COLUMN ) )?
                    {
                    root_0 = (Object)adaptor.nil();

                    ID49=(Token)input.LT(1);
                    match(input,ID,FOLLOW_ID_in_attribute_name1446); 
                    ID49_tree = (Object)adaptor.create(ID49);
                    adaptor.addChild(root_0, ID49_tree);

                    // Cubrid.g:231:5: ( DOT ( ID | COLUMN ) )?
                    int alt13=2;
                    int LA13_0 = input.LA(1);

                    if ( (LA13_0==DOT) ) {
                        alt13=1;
                    }
                    switch (alt13) {
                        case 1 :
                            // Cubrid.g:231:6: DOT ( ID | COLUMN )
                            {
                            DOT50=(Token)input.LT(1);
                            match(input,DOT,FOLLOW_DOT_in_attribute_name1449); 
                            DOT50_tree = (Object)adaptor.create(DOT50);
                            adaptor.addChild(root_0, DOT50_tree);

                            set51=(Token)input.LT(1);
                            if ( (input.LA(1)>=ID && input.LA(1)<=COLUMN) ) {
                                input.consume();
                                adaptor.addChild(root_0, adaptor.create(set51));
                                errorRecovery=false;
                            }
                            else {
                                MismatchedSetException mse =
                                    new MismatchedSetException(null,input);
                                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_attribute_name1452);    throw mse;
                            }


                            }
                            break;

                    }


                    }
                    break;
                case 2 :
                    // Cubrid.g:232:4: COLUMN
                    {
                    root_0 = (Object)adaptor.nil();

                    COLUMN52=(Token)input.LT(1);
                    match(input,COLUMN,FOLLOW_COLUMN_in_attribute_name1465); 
                    COLUMN52_tree = (Object)adaptor.create(COLUMN52);
                    adaptor.addChild(root_0, COLUMN52_tree);


                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end attribute_name

    public static class attribute_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start attribute_comma_list
    // Cubrid.g:235:1: attribute_comma_list : attribute_name ( COMMA attribute_name )* -> attribute_name ( COMMA ENTER attribute_name )* ;
    public final attribute_comma_list_return attribute_comma_list() throws RecognitionException {
        attribute_comma_list_return retval = new attribute_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA54=null;
        attribute_name_return attribute_name53 = null;

        attribute_name_return attribute_name55 = null;


        Object COMMA54_tree=null;
        RewriteRuleTokenStream stream_COMMA=new RewriteRuleTokenStream(adaptor,"token COMMA");
        RewriteRuleSubtreeStream stream_attribute_name=new RewriteRuleSubtreeStream(adaptor,"rule attribute_name");
        try {
            // Cubrid.g:235:21: ( attribute_name ( COMMA attribute_name )* -> attribute_name ( COMMA ENTER attribute_name )* )
            // Cubrid.g:236:2: attribute_name ( COMMA attribute_name )*
            {
            pushFollow(FOLLOW_attribute_name_in_attribute_comma_list1477);
            attribute_name53=attribute_name();
            _fsp--;

            stream_attribute_name.add(attribute_name53.getTree());
            // Cubrid.g:236:17: ( COMMA attribute_name )*
            loop15:
            do {
                int alt15=2;
                int LA15_0 = input.LA(1);

                if ( (LA15_0==COMMA) ) {
                    alt15=1;
                }


                switch (alt15) {
            	case 1 :
            	    // Cubrid.g:236:18: COMMA attribute_name
            	    {
            	    COMMA54=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_attribute_comma_list1480); 
            	    stream_COMMA.add(COMMA54);

            	    pushFollow(FOLLOW_attribute_name_in_attribute_comma_list1482);
            	    attribute_name55=attribute_name();
            	    _fsp--;

            	    stream_attribute_name.add(attribute_name55.getTree());

            	    }
            	    break;

            	default :
            	    break loop15;
                }
            } while (true);


            // AST REWRITE
            // elements: attribute_name, COMMA, attribute_name
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 236:41: -> attribute_name ( COMMA ENTER attribute_name )*
            {
                adaptor.addChild(root_0, stream_attribute_name.next());
                // Cubrid.g:236:59: ( COMMA ENTER attribute_name )*
                while ( stream_COMMA.hasNext()||stream_attribute_name.hasNext() ) {
                    adaptor.addChild(root_0, stream_COMMA.next());
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, stream_attribute_name.next());

                }
                stream_COMMA.reset();
                stream_attribute_name.reset();

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end attribute_comma_list

    public static class attribute_comma_list_part_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start attribute_comma_list_part
    // Cubrid.g:239:1: attribute_comma_list_part : STARTBRACKET attribute_comma_list ENDBRACKET -> ENTER STARTBRACKET ENTER TAB attribute_comma_list ENTER UNTAB ENDBRACKET ;
    public final attribute_comma_list_part_return attribute_comma_list_part() throws RecognitionException {
        attribute_comma_list_part_return retval = new attribute_comma_list_part_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token STARTBRACKET56=null;
        Token ENDBRACKET58=null;
        attribute_comma_list_return attribute_comma_list57 = null;


        Object STARTBRACKET56_tree=null;
        Object ENDBRACKET58_tree=null;
        RewriteRuleTokenStream stream_ENDBRACKET=new RewriteRuleTokenStream(adaptor,"token ENDBRACKET");
        RewriteRuleTokenStream stream_STARTBRACKET=new RewriteRuleTokenStream(adaptor,"token STARTBRACKET");
        RewriteRuleSubtreeStream stream_attribute_comma_list=new RewriteRuleSubtreeStream(adaptor,"rule attribute_comma_list");
        try {
            // Cubrid.g:239:26: ( STARTBRACKET attribute_comma_list ENDBRACKET -> ENTER STARTBRACKET ENTER TAB attribute_comma_list ENTER UNTAB ENDBRACKET )
            // Cubrid.g:240:2: STARTBRACKET attribute_comma_list ENDBRACKET
            {
            STARTBRACKET56=(Token)input.LT(1);
            match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_attribute_comma_list_part1510); 
            stream_STARTBRACKET.add(STARTBRACKET56);

            pushFollow(FOLLOW_attribute_comma_list_in_attribute_comma_list_part1512);
            attribute_comma_list57=attribute_comma_list();
            _fsp--;

            stream_attribute_comma_list.add(attribute_comma_list57.getTree());
            ENDBRACKET58=(Token)input.LT(1);
            match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_attribute_comma_list_part1514); 
            stream_ENDBRACKET.add(ENDBRACKET58);


            // AST REWRITE
            // elements: attribute_comma_list, STARTBRACKET, ENDBRACKET
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 241:2: -> ENTER STARTBRACKET ENTER TAB attribute_comma_list ENTER UNTAB ENDBRACKET
            {
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, stream_STARTBRACKET.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));
                adaptor.addChild(root_0, stream_attribute_comma_list.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));
                adaptor.addChild(root_0, stream_ENDBRACKET.next());

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end attribute_comma_list_part

    public static class left_expression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start left_expression
    // Cubrid.g:244:1: left_expression : ( ID ( DOT ID )? ) ;
    public final left_expression_return left_expression() throws RecognitionException {
        left_expression_return retval = new left_expression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ID59=null;
        Token DOT60=null;
        Token ID61=null;

        Object ID59_tree=null;
        Object DOT60_tree=null;
        Object ID61_tree=null;

        try {
            // Cubrid.g:244:16: ( ( ID ( DOT ID )? ) )
            // Cubrid.g:245:2: ( ID ( DOT ID )? )
            {
            root_0 = (Object)adaptor.nil();

            // Cubrid.g:245:2: ( ID ( DOT ID )? )
            // Cubrid.g:245:3: ID ( DOT ID )?
            {
            ID59=(Token)input.LT(1);
            match(input,ID,FOLLOW_ID_in_left_expression1546); 
            ID59_tree = (Object)adaptor.create(ID59);
            adaptor.addChild(root_0, ID59_tree);

            // Cubrid.g:245:6: ( DOT ID )?
            int alt16=2;
            int LA16_0 = input.LA(1);

            if ( (LA16_0==DOT) ) {
                alt16=1;
            }
            switch (alt16) {
                case 1 :
                    // Cubrid.g:245:7: DOT ID
                    {
                    DOT60=(Token)input.LT(1);
                    match(input,DOT,FOLLOW_DOT_in_left_expression1549); 
                    DOT60_tree = (Object)adaptor.create(DOT60);
                    adaptor.addChild(root_0, DOT60_tree);

                    ID61=(Token)input.LT(1);
                    match(input,ID,FOLLOW_ID_in_left_expression1551); 
                    ID61_tree = (Object)adaptor.create(ID61);
                    adaptor.addChild(root_0, ID61_tree);


                    }
                    break;

            }


            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end left_expression

    public static class right_expression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start right_expression
    // Cubrid.g:248:1: right_expression : expression ;
    public final right_expression_return right_expression() throws RecognitionException {
        right_expression_return retval = new right_expression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        expression_return expression62 = null;



        try {
            // Cubrid.g:248:17: ( expression )
            // Cubrid.g:249:2: expression
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_expression_in_right_expression1565);
            expression62=expression();
            _fsp--;

            adaptor.addChild(root_0, expression62.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end right_expression

    public static class set_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start set
    // Cubrid.g:252:1: set : STARTBRACKET value_comma_list ENDBRACKET ;
    public final set_return set() throws RecognitionException {
        set_return retval = new set_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token STARTBRACKET63=null;
        Token ENDBRACKET65=null;
        value_comma_list_return value_comma_list64 = null;


        Object STARTBRACKET63_tree=null;
        Object ENDBRACKET65_tree=null;

        try {
            // Cubrid.g:252:4: ( STARTBRACKET value_comma_list ENDBRACKET )
            // Cubrid.g:253:2: STARTBRACKET value_comma_list ENDBRACKET
            {
            root_0 = (Object)adaptor.nil();

            STARTBRACKET63=(Token)input.LT(1);
            match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_set1576); 
            STARTBRACKET63_tree = (Object)adaptor.create(STARTBRACKET63);
            adaptor.addChild(root_0, STARTBRACKET63_tree);

            pushFollow(FOLLOW_value_comma_list_in_set1578);
            value_comma_list64=value_comma_list();
            _fsp--;

            adaptor.addChild(root_0, value_comma_list64.getTree());
            ENDBRACKET65=(Token)input.LT(1);
            match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_set1580); 
            ENDBRACKET65_tree = (Object)adaptor.create(ENDBRACKET65);
            adaptor.addChild(root_0, ENDBRACKET65_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end set

    public static class variable_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start variable_comma_list
    // Cubrid.g:255:1: variable_comma_list : variable ( COMMA variable )* ;
    public final variable_comma_list_return variable_comma_list() throws RecognitionException {
        variable_comma_list_return retval = new variable_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA67=null;
        variable_return variable66 = null;

        variable_return variable68 = null;


        Object COMMA67_tree=null;

        try {
            // Cubrid.g:255:20: ( variable ( COMMA variable )* )
            // Cubrid.g:256:2: variable ( COMMA variable )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_variable_in_variable_comma_list1590);
            variable66=variable();
            _fsp--;

            adaptor.addChild(root_0, variable66.getTree());
            // Cubrid.g:256:11: ( COMMA variable )*
            loop17:
            do {
                int alt17=2;
                int LA17_0 = input.LA(1);

                if ( (LA17_0==COMMA) ) {
                    alt17=1;
                }


                switch (alt17) {
            	case 1 :
            	    // Cubrid.g:256:12: COMMA variable
            	    {
            	    COMMA67=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_variable_comma_list1593); 
            	    COMMA67_tree = (Object)adaptor.create(COMMA67);
            	    adaptor.addChild(root_0, COMMA67_tree);

            	    pushFollow(FOLLOW_variable_in_variable_comma_list1595);
            	    variable68=variable();
            	    _fsp--;

            	    adaptor.addChild(root_0, variable68.getTree());

            	    }
            	    break;

            	default :
            	    break loop17;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end variable_comma_list

    public static class variable_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start variable
    // Cubrid.g:259:1: variable : ( ID | value );
    public final variable_return variable() throws RecognitionException {
        variable_return retval = new variable_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ID69=null;
        value_return value70 = null;


        Object ID69_tree=null;

        try {
            // Cubrid.g:259:9: ( ID | value )
            int alt18=2;
            int LA18_0 = input.LA(1);

            if ( (LA18_0==ID) ) {
                alt18=1;
            }
            else if ( (LA18_0==QUOTA||(LA18_0>=DOLLAR && LA18_0<=Q_MARK)||LA18_0==DECIMALLITERAL||LA18_0==STRING||LA18_0==172) ) {
                alt18=2;
            }
            else {
                NoViableAltException nvae =
                    new NoViableAltException("259:1: variable : ( ID | value );", 18, 0, input);

                throw nvae;
            }
            switch (alt18) {
                case 1 :
                    // Cubrid.g:260:2: ID
                    {
                    root_0 = (Object)adaptor.nil();

                    ID69=(Token)input.LT(1);
                    match(input,ID,FOLLOW_ID_in_variable1608); 
                    ID69_tree = (Object)adaptor.create(ID69);
                    adaptor.addChild(root_0, ID69_tree);


                    }
                    break;
                case 2 :
                    // Cubrid.g:261:3: value
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_value_in_variable1612);
                    value70=value();
                    _fsp--;

                    adaptor.addChild(root_0, value70.getTree());

                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end variable

    public static class class_name_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start class_name
    // Cubrid.g:264:1: class_name : ( ID | COLUMN );
    public final class_name_return class_name() throws RecognitionException {
        class_name_return retval = new class_name_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set71=null;

        Object set71_tree=null;

        try {
            // Cubrid.g:264:11: ( ID | COLUMN )
            // Cubrid.g:
            {
            root_0 = (Object)adaptor.nil();

            set71=(Token)input.LT(1);
            if ( (input.LA(1)>=ID && input.LA(1)<=COLUMN) ) {
                input.consume();
                adaptor.addChild(root_0, adaptor.create(set71));
                errorRecovery=false;
            }
            else {
                MismatchedSetException mse =
                    new MismatchedSetException(null,input);
                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_class_name0);    throw mse;
            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end class_name

    public static class table_specification_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start table_specification_comma_list
    // Cubrid.g:268:1: table_specification_comma_list : table_specification ( comma_join_spec )* -> table_specification ( comma_join_spec )* ENTER ;
    public final table_specification_comma_list_return table_specification_comma_list() throws RecognitionException {
        table_specification_comma_list_return retval = new table_specification_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        table_specification_return table_specification72 = null;

        comma_join_spec_return comma_join_spec73 = null;


        RewriteRuleSubtreeStream stream_table_specification=new RewriteRuleSubtreeStream(adaptor,"rule table_specification");
        RewriteRuleSubtreeStream stream_comma_join_spec=new RewriteRuleSubtreeStream(adaptor,"rule comma_join_spec");
        try {
            // Cubrid.g:268:31: ( table_specification ( comma_join_spec )* -> table_specification ( comma_join_spec )* ENTER )
            // Cubrid.g:269:2: table_specification ( comma_join_spec )*
            {
            pushFollow(FOLLOW_table_specification_in_table_specification_comma_list1637);
            table_specification72=table_specification();
            _fsp--;

            stream_table_specification.add(table_specification72.getTree());
            // Cubrid.g:269:22: ( comma_join_spec )*
            loop19:
            do {
                int alt19=2;
                int LA19_0 = input.LA(1);

                if ( (LA19_0==LEFT||LA19_0==RIGHT||LA19_0==COMMA) ) {
                    alt19=1;
                }


                switch (alt19) {
            	case 1 :
            	    // Cubrid.g:269:23: comma_join_spec
            	    {
            	    pushFollow(FOLLOW_comma_join_spec_in_table_specification_comma_list1640);
            	    comma_join_spec73=comma_join_spec();
            	    _fsp--;

            	    stream_comma_join_spec.add(comma_join_spec73.getTree());

            	    }
            	    break;

            	default :
            	    break loop19;
                }
            } while (true);


            // AST REWRITE
            // elements: table_specification, comma_join_spec
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 269:41: -> table_specification ( comma_join_spec )* ENTER
            {
                adaptor.addChild(root_0, stream_table_specification.next());
                // Cubrid.g:269:64: ( comma_join_spec )*
                while ( stream_comma_join_spec.hasNext() ) {
                    adaptor.addChild(root_0, stream_comma_join_spec.next());

                }
                stream_comma_join_spec.reset();
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end table_specification_comma_list

    public static class comma_join_spec_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start comma_join_spec
    // Cubrid.g:275:1: comma_join_spec : ( COMMA table_specification -> COMMA ENTER table_specification | qualified_join_specification );
    public final comma_join_spec_return comma_join_spec() throws RecognitionException {
        comma_join_spec_return retval = new comma_join_spec_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA74=null;
        table_specification_return table_specification75 = null;

        qualified_join_specification_return qualified_join_specification76 = null;


        Object COMMA74_tree=null;
        RewriteRuleTokenStream stream_COMMA=new RewriteRuleTokenStream(adaptor,"token COMMA");
        RewriteRuleSubtreeStream stream_table_specification=new RewriteRuleSubtreeStream(adaptor,"rule table_specification");
        try {
            // Cubrid.g:275:16: ( COMMA table_specification -> COMMA ENTER table_specification | qualified_join_specification )
            int alt20=2;
            int LA20_0 = input.LA(1);

            if ( (LA20_0==COMMA) ) {
                alt20=1;
            }
            else if ( (LA20_0==LEFT||LA20_0==RIGHT) ) {
                alt20=2;
            }
            else {
                NoViableAltException nvae =
                    new NoViableAltException("275:1: comma_join_spec : ( COMMA table_specification -> COMMA ENTER table_specification | qualified_join_specification );", 20, 0, input);

                throw nvae;
            }
            switch (alt20) {
                case 1 :
                    // Cubrid.g:276:2: COMMA table_specification
                    {
                    COMMA74=(Token)input.LT(1);
                    match(input,COMMA,FOLLOW_COMMA_in_comma_join_spec1671); 
                    stream_COMMA.add(COMMA74);

                    pushFollow(FOLLOW_table_specification_in_comma_join_spec1673);
                    table_specification75=table_specification();
                    _fsp--;

                    stream_table_specification.add(table_specification75.getTree());

                    // AST REWRITE
                    // elements: COMMA, table_specification
                    // token labels: 
                    // rule labels: retval
                    // token list labels: 
                    // rule list labels: 
                    retval.tree = root_0;
                    RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

                    root_0 = (Object)adaptor.nil();
                    // 276:28: -> COMMA ENTER table_specification
                    {
                        adaptor.addChild(root_0, stream_COMMA.next());
                        adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                        adaptor.addChild(root_0, stream_table_specification.next());

                    }



                    }
                    break;
                case 2 :
                    // Cubrid.g:277:4: qualified_join_specification
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_qualified_join_specification_in_comma_join_spec1687);
                    qualified_join_specification76=qualified_join_specification();
                    _fsp--;

                    adaptor.addChild(root_0, qualified_join_specification76.getTree());

                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end comma_join_spec

    public static class qualified_join_specification_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start qualified_join_specification
    // Cubrid.g:280:1: qualified_join_specification : ( LEFT | RIGHT ) ( OUTER )? JOIN table_specification join_condition ;
    public final qualified_join_specification_return qualified_join_specification() throws RecognitionException {
        qualified_join_specification_return retval = new qualified_join_specification_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set77=null;
        Token OUTER78=null;
        Token JOIN79=null;
        table_specification_return table_specification80 = null;

        join_condition_return join_condition81 = null;


        Object set77_tree=null;
        Object OUTER78_tree=null;
        Object JOIN79_tree=null;

        try {
            // Cubrid.g:280:29: ( ( LEFT | RIGHT ) ( OUTER )? JOIN table_specification join_condition )
            // Cubrid.g:281:2: ( LEFT | RIGHT ) ( OUTER )? JOIN table_specification join_condition
            {
            root_0 = (Object)adaptor.nil();

            set77=(Token)input.LT(1);
            if ( input.LA(1)==LEFT||input.LA(1)==RIGHT ) {
                input.consume();
                adaptor.addChild(root_0, adaptor.create(set77));
                errorRecovery=false;
            }
            else {
                MismatchedSetException mse =
                    new MismatchedSetException(null,input);
                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_qualified_join_specification1698);    throw mse;
            }

            // Cubrid.g:281:17: ( OUTER )?
            int alt21=2;
            int LA21_0 = input.LA(1);

            if ( (LA21_0==OUTER) ) {
                alt21=1;
            }
            switch (alt21) {
                case 1 :
                    // Cubrid.g:281:17: OUTER
                    {
                    OUTER78=(Token)input.LT(1);
                    match(input,OUTER,FOLLOW_OUTER_in_qualified_join_specification1706); 
                    OUTER78_tree = (Object)adaptor.create(OUTER78);
                    adaptor.addChild(root_0, OUTER78_tree);


                    }
                    break;

            }

            JOIN79=(Token)input.LT(1);
            match(input,JOIN,FOLLOW_JOIN_in_qualified_join_specification1709); 
            JOIN79_tree = (Object)adaptor.create(JOIN79);
            adaptor.addChild(root_0, JOIN79_tree);

            pushFollow(FOLLOW_table_specification_in_qualified_join_specification1711);
            table_specification80=table_specification();
            _fsp--;

            adaptor.addChild(root_0, table_specification80.getTree());
            pushFollow(FOLLOW_join_condition_in_qualified_join_specification1713);
            join_condition81=join_condition();
            _fsp--;

            adaptor.addChild(root_0, join_condition81.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end qualified_join_specification

    public static class join_condition_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start join_condition
    // Cubrid.g:283:1: join_condition : ON search_condition ;
    public final join_condition_return join_condition() throws RecognitionException {
        join_condition_return retval = new join_condition_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ON82=null;
        search_condition_return search_condition83 = null;


        Object ON82_tree=null;

        try {
            // Cubrid.g:283:15: ( ON search_condition )
            // Cubrid.g:284:2: ON search_condition
            {
            root_0 = (Object)adaptor.nil();

            ON82=(Token)input.LT(1);
            match(input,ON,FOLLOW_ON_in_join_condition1723); 
            ON82_tree = (Object)adaptor.create(ON82);
            adaptor.addChild(root_0, ON82_tree);

            pushFollow(FOLLOW_search_condition_in_join_condition1725);
            search_condition83=search_condition();
            _fsp--;

            adaptor.addChild(root_0, search_condition83.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end join_condition

    public static class table_specification_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start table_specification
    // Cubrid.g:287:1: table_specification : ( class_specification ( ( correlation )? ) | metaclass_specification ( ( correlation )? ) | subquery ( ( correlation )? ) | TABLE expression ( ( correlation )? ) );
    public final table_specification_return table_specification() throws RecognitionException {
        table_specification_return retval = new table_specification_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token TABLE90=null;
        class_specification_return class_specification84 = null;

        correlation_return correlation85 = null;

        metaclass_specification_return metaclass_specification86 = null;

        correlation_return correlation87 = null;

        subquery_return subquery88 = null;

        correlation_return correlation89 = null;

        expression_return expression91 = null;

        correlation_return correlation92 = null;


        Object TABLE90_tree=null;

        try {
            // Cubrid.g:287:20: ( class_specification ( ( correlation )? ) | metaclass_specification ( ( correlation )? ) | subquery ( ( correlation )? ) | TABLE expression ( ( correlation )? ) )
            int alt26=4;
            switch ( input.LA(1) ) {
            case ALL:
            case ONLY:
            case ID:
            case COLUMN:
                {
                alt26=1;
                }
                break;
            case STARTBRACKET:
                {
                int LA26_2 = input.LA(2);

                if ( (LA26_2==ALL||LA26_2==ONLY||(LA26_2>=ID && LA26_2<=COLUMN)) ) {
                    alt26=1;
                }
                else if ( (LA26_2==SELECT) ) {
                    alt26=3;
                }
                else {
                    NoViableAltException nvae =
                        new NoViableAltException("287:1: table_specification : ( class_specification ( ( correlation )? ) | metaclass_specification ( ( correlation )? ) | subquery ( ( correlation )? ) | TABLE expression ( ( correlation )? ) );", 26, 2, input);

                    throw nvae;
                }
                }
                break;
            case CLASS:
                {
                alt26=2;
                }
                break;
            case TABLE:
                {
                alt26=4;
                }
                break;
            default:
                NoViableAltException nvae =
                    new NoViableAltException("287:1: table_specification : ( class_specification ( ( correlation )? ) | metaclass_specification ( ( correlation )? ) | subquery ( ( correlation )? ) | TABLE expression ( ( correlation )? ) );", 26, 0, input);

                throw nvae;
            }

            switch (alt26) {
                case 1 :
                    // Cubrid.g:288:3: class_specification ( ( correlation )? )
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_class_specification_in_table_specification1736);
                    class_specification84=class_specification();
                    _fsp--;

                    adaptor.addChild(root_0, class_specification84.getTree());
                    // Cubrid.g:288:23: ( ( correlation )? )
                    // Cubrid.g:288:24: ( correlation )?
                    {
                    // Cubrid.g:288:24: ( correlation )?
                    int alt22=2;
                    int LA22_0 = input.LA(1);

                    if ( (LA22_0==AS||LA22_0==ID) ) {
                        alt22=1;
                    }
                    switch (alt22) {
                        case 1 :
                            // Cubrid.g:288:24: correlation
                            {
                            pushFollow(FOLLOW_correlation_in_table_specification1739);
                            correlation85=correlation();
                            _fsp--;

                            adaptor.addChild(root_0, correlation85.getTree());

                            }
                            break;

                    }


                    }


                    }
                    break;
                case 2 :
                    // Cubrid.g:289:5: metaclass_specification ( ( correlation )? )
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_metaclass_specification_in_table_specification1747);
                    metaclass_specification86=metaclass_specification();
                    _fsp--;

                    adaptor.addChild(root_0, metaclass_specification86.getTree());
                    // Cubrid.g:289:29: ( ( correlation )? )
                    // Cubrid.g:289:30: ( correlation )?
                    {
                    // Cubrid.g:289:30: ( correlation )?
                    int alt23=2;
                    int LA23_0 = input.LA(1);

                    if ( (LA23_0==AS||LA23_0==ID) ) {
                        alt23=1;
                    }
                    switch (alt23) {
                        case 1 :
                            // Cubrid.g:289:30: correlation
                            {
                            pushFollow(FOLLOW_correlation_in_table_specification1750);
                            correlation87=correlation();
                            _fsp--;

                            adaptor.addChild(root_0, correlation87.getTree());

                            }
                            break;

                    }


                    }


                    }
                    break;
                case 3 :
                    // Cubrid.g:290:5: subquery ( ( correlation )? )
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_subquery_in_table_specification1758);
                    subquery88=subquery();
                    _fsp--;

                    adaptor.addChild(root_0, subquery88.getTree());
                    // Cubrid.g:290:14: ( ( correlation )? )
                    // Cubrid.g:290:15: ( correlation )?
                    {
                    // Cubrid.g:290:15: ( correlation )?
                    int alt24=2;
                    int LA24_0 = input.LA(1);

                    if ( (LA24_0==AS||LA24_0==ID) ) {
                        alt24=1;
                    }
                    switch (alt24) {
                        case 1 :
                            // Cubrid.g:290:15: correlation
                            {
                            pushFollow(FOLLOW_correlation_in_table_specification1761);
                            correlation89=correlation();
                            _fsp--;

                            adaptor.addChild(root_0, correlation89.getTree());

                            }
                            break;

                    }


                    }


                    }
                    break;
                case 4 :
                    // Cubrid.g:291:5: TABLE expression ( ( correlation )? )
                    {
                    root_0 = (Object)adaptor.nil();

                    TABLE90=(Token)input.LT(1);
                    match(input,TABLE,FOLLOW_TABLE_in_table_specification1770); 
                    TABLE90_tree = (Object)adaptor.create(TABLE90);
                    adaptor.addChild(root_0, TABLE90_tree);

                    pushFollow(FOLLOW_expression_in_table_specification1772);
                    expression91=expression();
                    _fsp--;

                    adaptor.addChild(root_0, expression91.getTree());
                    // Cubrid.g:291:22: ( ( correlation )? )
                    // Cubrid.g:291:23: ( correlation )?
                    {
                    // Cubrid.g:291:23: ( correlation )?
                    int alt25=2;
                    int LA25_0 = input.LA(1);

                    if ( (LA25_0==AS||LA25_0==ID) ) {
                        alt25=1;
                    }
                    switch (alt25) {
                        case 1 :
                            // Cubrid.g:291:23: correlation
                            {
                            pushFollow(FOLLOW_correlation_in_table_specification1775);
                            correlation92=correlation();
                            _fsp--;

                            adaptor.addChild(root_0, correlation92.getTree());

                            }
                            break;

                    }


                    }


                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end table_specification

    public static class correlation_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start correlation
    // Cubrid.g:294:1: correlation : ( AS )? ID ( STARTBRACKET id_comma_list ENDBRACKET )? ;
    public final correlation_return correlation() throws RecognitionException {
        correlation_return retval = new correlation_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token AS93=null;
        Token ID94=null;
        Token STARTBRACKET95=null;
        Token ENDBRACKET97=null;
        id_comma_list_return id_comma_list96 = null;


        Object AS93_tree=null;
        Object ID94_tree=null;
        Object STARTBRACKET95_tree=null;
        Object ENDBRACKET97_tree=null;

        try {
            // Cubrid.g:294:12: ( ( AS )? ID ( STARTBRACKET id_comma_list ENDBRACKET )? )
            // Cubrid.g:295:2: ( AS )? ID ( STARTBRACKET id_comma_list ENDBRACKET )?
            {
            root_0 = (Object)adaptor.nil();

            // Cubrid.g:295:2: ( AS )?
            int alt27=2;
            int LA27_0 = input.LA(1);

            if ( (LA27_0==AS) ) {
                alt27=1;
            }
            switch (alt27) {
                case 1 :
                    // Cubrid.g:295:2: AS
                    {
                    AS93=(Token)input.LT(1);
                    match(input,AS,FOLLOW_AS_in_correlation1788); 
                    AS93_tree = (Object)adaptor.create(AS93);
                    adaptor.addChild(root_0, AS93_tree);


                    }
                    break;

            }

            ID94=(Token)input.LT(1);
            match(input,ID,FOLLOW_ID_in_correlation1791); 
            ID94_tree = (Object)adaptor.create(ID94);
            adaptor.addChild(root_0, ID94_tree);

            // Cubrid.g:295:9: ( STARTBRACKET id_comma_list ENDBRACKET )?
            int alt28=2;
            int LA28_0 = input.LA(1);

            if ( (LA28_0==STARTBRACKET) ) {
                alt28=1;
            }
            switch (alt28) {
                case 1 :
                    // Cubrid.g:295:10: STARTBRACKET id_comma_list ENDBRACKET
                    {
                    STARTBRACKET95=(Token)input.LT(1);
                    match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_correlation1794); 
                    STARTBRACKET95_tree = (Object)adaptor.create(STARTBRACKET95);
                    adaptor.addChild(root_0, STARTBRACKET95_tree);

                    pushFollow(FOLLOW_id_comma_list_in_correlation1796);
                    id_comma_list96=id_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, id_comma_list96.getTree());
                    ENDBRACKET97=(Token)input.LT(1);
                    match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_correlation1798); 
                    ENDBRACKET97_tree = (Object)adaptor.create(ENDBRACKET97);
                    adaptor.addChild(root_0, ENDBRACKET97_tree);


                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end correlation

    public static class id_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start id_comma_list
    // Cubrid.g:298:1: id_comma_list : ID ( COMMA ID )* ;
    public final id_comma_list_return id_comma_list() throws RecognitionException {
        id_comma_list_return retval = new id_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ID98=null;
        Token COMMA99=null;
        Token ID100=null;

        Object ID98_tree=null;
        Object COMMA99_tree=null;
        Object ID100_tree=null;

        try {
            // Cubrid.g:298:14: ( ID ( COMMA ID )* )
            // Cubrid.g:299:2: ID ( COMMA ID )*
            {
            root_0 = (Object)adaptor.nil();

            ID98=(Token)input.LT(1);
            match(input,ID,FOLLOW_ID_in_id_comma_list1811); 
            ID98_tree = (Object)adaptor.create(ID98);
            adaptor.addChild(root_0, ID98_tree);

            // Cubrid.g:299:5: ( COMMA ID )*
            loop29:
            do {
                int alt29=2;
                int LA29_0 = input.LA(1);

                if ( (LA29_0==COMMA) ) {
                    alt29=1;
                }


                switch (alt29) {
            	case 1 :
            	    // Cubrid.g:299:6: COMMA ID
            	    {
            	    COMMA99=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_id_comma_list1814); 
            	    COMMA99_tree = (Object)adaptor.create(COMMA99);
            	    adaptor.addChild(root_0, COMMA99_tree);

            	    ID100=(Token)input.LT(1);
            	    match(input,ID,FOLLOW_ID_in_id_comma_list1816); 
            	    ID100_tree = (Object)adaptor.create(ID100);
            	    adaptor.addChild(root_0, ID100_tree);


            	    }
            	    break;

            	default :
            	    break loop29;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end id_comma_list

    public static class class_specification_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start class_specification
    // Cubrid.g:302:1: class_specification : ( class_hierarchy | STARTBRACKET class_hierarchy_comma_list ENDBRACKET );
    public final class_specification_return class_specification() throws RecognitionException {
        class_specification_return retval = new class_specification_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token STARTBRACKET102=null;
        Token ENDBRACKET104=null;
        class_hierarchy_return class_hierarchy101 = null;

        class_hierarchy_comma_list_return class_hierarchy_comma_list103 = null;


        Object STARTBRACKET102_tree=null;
        Object ENDBRACKET104_tree=null;

        try {
            // Cubrid.g:302:20: ( class_hierarchy | STARTBRACKET class_hierarchy_comma_list ENDBRACKET )
            int alt30=2;
            int LA30_0 = input.LA(1);

            if ( (LA30_0==ALL||LA30_0==ONLY||(LA30_0>=ID && LA30_0<=COLUMN)) ) {
                alt30=1;
            }
            else if ( (LA30_0==STARTBRACKET) ) {
                alt30=2;
            }
            else {
                NoViableAltException nvae =
                    new NoViableAltException("302:1: class_specification : ( class_hierarchy | STARTBRACKET class_hierarchy_comma_list ENDBRACKET );", 30, 0, input);

                throw nvae;
            }
            switch (alt30) {
                case 1 :
                    // Cubrid.g:303:2: class_hierarchy
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_class_hierarchy_in_class_specification1831);
                    class_hierarchy101=class_hierarchy();
                    _fsp--;

                    adaptor.addChild(root_0, class_hierarchy101.getTree());

                    }
                    break;
                case 2 :
                    // Cubrid.g:304:4: STARTBRACKET class_hierarchy_comma_list ENDBRACKET
                    {
                    root_0 = (Object)adaptor.nil();

                    STARTBRACKET102=(Token)input.LT(1);
                    match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_class_specification1836); 
                    STARTBRACKET102_tree = (Object)adaptor.create(STARTBRACKET102);
                    adaptor.addChild(root_0, STARTBRACKET102_tree);

                    pushFollow(FOLLOW_class_hierarchy_comma_list_in_class_specification1838);
                    class_hierarchy_comma_list103=class_hierarchy_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, class_hierarchy_comma_list103.getTree());
                    ENDBRACKET104=(Token)input.LT(1);
                    match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_class_specification1840); 
                    ENDBRACKET104_tree = (Object)adaptor.create(ENDBRACKET104);
                    adaptor.addChild(root_0, ENDBRACKET104_tree);


                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end class_specification

    public static class class_hierarchy_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start class_hierarchy
    // Cubrid.g:307:1: class_hierarchy : ( ( ONLY )? class_name | ALL class_name ( EXCEPT class_specification )? );
    public final class_hierarchy_return class_hierarchy() throws RecognitionException {
        class_hierarchy_return retval = new class_hierarchy_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ONLY105=null;
        Token ALL107=null;
        Token EXCEPT109=null;
        class_name_return class_name106 = null;

        class_name_return class_name108 = null;

        class_specification_return class_specification110 = null;


        Object ONLY105_tree=null;
        Object ALL107_tree=null;
        Object EXCEPT109_tree=null;

        try {
            // Cubrid.g:307:16: ( ( ONLY )? class_name | ALL class_name ( EXCEPT class_specification )? )
            int alt33=2;
            int LA33_0 = input.LA(1);

            if ( (LA33_0==ONLY||(LA33_0>=ID && LA33_0<=COLUMN)) ) {
                alt33=1;
            }
            else if ( (LA33_0==ALL) ) {
                alt33=2;
            }
            else {
                NoViableAltException nvae =
                    new NoViableAltException("307:1: class_hierarchy : ( ( ONLY )? class_name | ALL class_name ( EXCEPT class_specification )? );", 33, 0, input);

                throw nvae;
            }
            switch (alt33) {
                case 1 :
                    // Cubrid.g:308:2: ( ONLY )? class_name
                    {
                    root_0 = (Object)adaptor.nil();

                    // Cubrid.g:308:2: ( ONLY )?
                    int alt31=2;
                    int LA31_0 = input.LA(1);

                    if ( (LA31_0==ONLY) ) {
                        alt31=1;
                    }
                    switch (alt31) {
                        case 1 :
                            // Cubrid.g:308:2: ONLY
                            {
                            ONLY105=(Token)input.LT(1);
                            match(input,ONLY,FOLLOW_ONLY_in_class_hierarchy1851); 
                            ONLY105_tree = (Object)adaptor.create(ONLY105);
                            adaptor.addChild(root_0, ONLY105_tree);


                            }
                            break;

                    }

                    pushFollow(FOLLOW_class_name_in_class_hierarchy1854);
                    class_name106=class_name();
                    _fsp--;

                    adaptor.addChild(root_0, class_name106.getTree());

                    }
                    break;
                case 2 :
                    // Cubrid.g:309:3: ALL class_name ( EXCEPT class_specification )?
                    {
                    root_0 = (Object)adaptor.nil();

                    ALL107=(Token)input.LT(1);
                    match(input,ALL,FOLLOW_ALL_in_class_hierarchy1858); 
                    ALL107_tree = (Object)adaptor.create(ALL107);
                    adaptor.addChild(root_0, ALL107_tree);

                    pushFollow(FOLLOW_class_name_in_class_hierarchy1860);
                    class_name108=class_name();
                    _fsp--;

                    adaptor.addChild(root_0, class_name108.getTree());
                    // Cubrid.g:309:18: ( EXCEPT class_specification )?
                    int alt32=2;
                    int LA32_0 = input.LA(1);

                    if ( (LA32_0==EXCEPT) ) {
                        alt32=1;
                    }
                    switch (alt32) {
                        case 1 :
                            // Cubrid.g:309:19: EXCEPT class_specification
                            {
                            EXCEPT109=(Token)input.LT(1);
                            match(input,EXCEPT,FOLLOW_EXCEPT_in_class_hierarchy1863); 
                            EXCEPT109_tree = (Object)adaptor.create(EXCEPT109);
                            adaptor.addChild(root_0, EXCEPT109_tree);

                            pushFollow(FOLLOW_class_specification_in_class_hierarchy1865);
                            class_specification110=class_specification();
                            _fsp--;

                            adaptor.addChild(root_0, class_specification110.getTree());

                            }
                            break;

                    }


                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end class_hierarchy

    public static class class_hierarchy_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start class_hierarchy_comma_list
    // Cubrid.g:312:1: class_hierarchy_comma_list : class_hierarchy ( COMMA class_hierarchy )* ;
    public final class_hierarchy_comma_list_return class_hierarchy_comma_list() throws RecognitionException {
        class_hierarchy_comma_list_return retval = new class_hierarchy_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA112=null;
        class_hierarchy_return class_hierarchy111 = null;

        class_hierarchy_return class_hierarchy113 = null;


        Object COMMA112_tree=null;

        try {
            // Cubrid.g:312:27: ( class_hierarchy ( COMMA class_hierarchy )* )
            // Cubrid.g:313:2: class_hierarchy ( COMMA class_hierarchy )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_class_hierarchy_in_class_hierarchy_comma_list1879);
            class_hierarchy111=class_hierarchy();
            _fsp--;

            adaptor.addChild(root_0, class_hierarchy111.getTree());
            // Cubrid.g:313:18: ( COMMA class_hierarchy )*
            loop34:
            do {
                int alt34=2;
                int LA34_0 = input.LA(1);

                if ( (LA34_0==COMMA) ) {
                    alt34=1;
                }


                switch (alt34) {
            	case 1 :
            	    // Cubrid.g:313:19: COMMA class_hierarchy
            	    {
            	    COMMA112=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_class_hierarchy_comma_list1882); 
            	    COMMA112_tree = (Object)adaptor.create(COMMA112);
            	    adaptor.addChild(root_0, COMMA112_tree);

            	    pushFollow(FOLLOW_class_hierarchy_in_class_hierarchy_comma_list1884);
            	    class_hierarchy113=class_hierarchy();
            	    _fsp--;

            	    adaptor.addChild(root_0, class_hierarchy113.getTree());

            	    }
            	    break;

            	default :
            	    break loop34;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end class_hierarchy_comma_list

    public static class metaclass_specification_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start metaclass_specification
    // Cubrid.g:316:1: metaclass_specification : CLASS class_name ;
    public final metaclass_specification_return metaclass_specification() throws RecognitionException {
        metaclass_specification_return retval = new metaclass_specification_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token CLASS114=null;
        class_name_return class_name115 = null;


        Object CLASS114_tree=null;

        try {
            // Cubrid.g:316:24: ( CLASS class_name )
            // Cubrid.g:317:2: CLASS class_name
            {
            root_0 = (Object)adaptor.nil();

            CLASS114=(Token)input.LT(1);
            match(input,CLASS,FOLLOW_CLASS_in_metaclass_specification1897); 
            CLASS114_tree = (Object)adaptor.create(CLASS114);
            adaptor.addChild(root_0, CLASS114_tree);

            pushFollow(FOLLOW_class_name_in_metaclass_specification1899);
            class_name115=class_name();
            _fsp--;

            adaptor.addChild(root_0, class_name115.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end metaclass_specification

    public static class query_statement_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start query_statement
    // Cubrid.g:320:1: query_statement : query_expression ( ORDER BY sort_specification_comma_list )? ;
    public final query_statement_return query_statement() throws RecognitionException {
        query_statement_return retval = new query_statement_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ORDER117=null;
        Token BY118=null;
        query_expression_return query_expression116 = null;

        sort_specification_comma_list_return sort_specification_comma_list119 = null;


        Object ORDER117_tree=null;
        Object BY118_tree=null;

        try {
            // Cubrid.g:320:16: ( query_expression ( ORDER BY sort_specification_comma_list )? )
            // Cubrid.g:321:2: query_expression ( ORDER BY sort_specification_comma_list )?
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_query_expression_in_query_statement1909);
            query_expression116=query_expression();
            _fsp--;

            adaptor.addChild(root_0, query_expression116.getTree());
            // Cubrid.g:322:2: ( ORDER BY sort_specification_comma_list )?
            int alt35=2;
            int LA35_0 = input.LA(1);

            if ( (LA35_0==ORDER) ) {
                alt35=1;
            }
            switch (alt35) {
                case 1 :
                    // Cubrid.g:322:3: ORDER BY sort_specification_comma_list
                    {
                    ORDER117=(Token)input.LT(1);
                    match(input,ORDER,FOLLOW_ORDER_in_query_statement1913); 
                    ORDER117_tree = (Object)adaptor.create(ORDER117);
                    adaptor.addChild(root_0, ORDER117_tree);

                    BY118=(Token)input.LT(1);
                    match(input,BY,FOLLOW_BY_in_query_statement1915); 
                    BY118_tree = (Object)adaptor.create(BY118);
                    adaptor.addChild(root_0, BY118_tree);

                    pushFollow(FOLLOW_sort_specification_comma_list_in_query_statement1917);
                    sort_specification_comma_list119=sort_specification_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, sort_specification_comma_list119.getTree());

                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end query_statement

    public static class sort_specification_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start sort_specification_comma_list
    // Cubrid.g:325:1: sort_specification_comma_list : sort_specification ( COMMA sort_specification )* ;
    public final sort_specification_comma_list_return sort_specification_comma_list() throws RecognitionException {
        sort_specification_comma_list_return retval = new sort_specification_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA121=null;
        sort_specification_return sort_specification120 = null;

        sort_specification_return sort_specification122 = null;


        Object COMMA121_tree=null;

        try {
            // Cubrid.g:325:30: ( sort_specification ( COMMA sort_specification )* )
            // Cubrid.g:326:2: sort_specification ( COMMA sort_specification )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_sort_specification_in_sort_specification_comma_list1931);
            sort_specification120=sort_specification();
            _fsp--;

            adaptor.addChild(root_0, sort_specification120.getTree());
            // Cubrid.g:326:21: ( COMMA sort_specification )*
            loop36:
            do {
                int alt36=2;
                int LA36_0 = input.LA(1);

                if ( (LA36_0==COMMA) ) {
                    alt36=1;
                }


                switch (alt36) {
            	case 1 :
            	    // Cubrid.g:326:22: COMMA sort_specification
            	    {
            	    COMMA121=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_sort_specification_comma_list1934); 
            	    COMMA121_tree = (Object)adaptor.create(COMMA121);
            	    adaptor.addChild(root_0, COMMA121_tree);

            	    pushFollow(FOLLOW_sort_specification_in_sort_specification_comma_list1936);
            	    sort_specification122=sort_specification();
            	    _fsp--;

            	    adaptor.addChild(root_0, sort_specification122.getTree());

            	    }
            	    break;

            	default :
            	    break loop36;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end sort_specification_comma_list

    public static class sort_specification_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start sort_specification
    // Cubrid.g:329:1: sort_specification : ( ( path_expression ( ASC | DESC )? ) | ( unsigned_integer_literal ( ASC | DESC )? ) );
    public final sort_specification_return sort_specification() throws RecognitionException {
        sort_specification_return retval = new sort_specification_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set124=null;
        Token set126=null;
        path_expression_return path_expression123 = null;

        unsigned_integer_literal_return unsigned_integer_literal125 = null;


        Object set124_tree=null;
        Object set126_tree=null;

        try {
            // Cubrid.g:329:19: ( ( path_expression ( ASC | DESC )? ) | ( unsigned_integer_literal ( ASC | DESC )? ) )
            int alt39=2;
            int LA39_0 = input.LA(1);

            if ( ((LA39_0>=ID && LA39_0<=COLUMN)) ) {
                alt39=1;
            }
            else if ( (LA39_0==DECIMALLITERAL) ) {
                alt39=2;
            }
            else {
                NoViableAltException nvae =
                    new NoViableAltException("329:1: sort_specification : ( ( path_expression ( ASC | DESC )? ) | ( unsigned_integer_literal ( ASC | DESC )? ) );", 39, 0, input);

                throw nvae;
            }
            switch (alt39) {
                case 1 :
                    // Cubrid.g:330:2: ( path_expression ( ASC | DESC )? )
                    {
                    root_0 = (Object)adaptor.nil();

                    // Cubrid.g:330:2: ( path_expression ( ASC | DESC )? )
                    // Cubrid.g:330:3: path_expression ( ASC | DESC )?
                    {
                    pushFollow(FOLLOW_path_expression_in_sort_specification1950);
                    path_expression123=path_expression();
                    _fsp--;

                    adaptor.addChild(root_0, path_expression123.getTree());
                    // Cubrid.g:330:19: ( ASC | DESC )?
                    int alt37=2;
                    int LA37_0 = input.LA(1);

                    if ( (LA37_0==ASC||LA37_0==DESC) ) {
                        alt37=1;
                    }
                    switch (alt37) {
                        case 1 :
                            // Cubrid.g:
                            {
                            set124=(Token)input.LT(1);
                            if ( input.LA(1)==ASC||input.LA(1)==DESC ) {
                                input.consume();
                                adaptor.addChild(root_0, adaptor.create(set124));
                                errorRecovery=false;
                            }
                            else {
                                MismatchedSetException mse =
                                    new MismatchedSetException(null,input);
                                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_sort_specification1952);    throw mse;
                            }


                            }
                            break;

                    }


                    }


                    }
                    break;
                case 2 :
                    // Cubrid.g:331:4: ( unsigned_integer_literal ( ASC | DESC )? )
                    {
                    root_0 = (Object)adaptor.nil();

                    // Cubrid.g:331:4: ( unsigned_integer_literal ( ASC | DESC )? )
                    // Cubrid.g:331:5: unsigned_integer_literal ( ASC | DESC )?
                    {
                    pushFollow(FOLLOW_unsigned_integer_literal_in_sort_specification1966);
                    unsigned_integer_literal125=unsigned_integer_literal();
                    _fsp--;

                    adaptor.addChild(root_0, unsigned_integer_literal125.getTree());
                    // Cubrid.g:331:31: ( ASC | DESC )?
                    int alt38=2;
                    int LA38_0 = input.LA(1);

                    if ( (LA38_0==ASC||LA38_0==DESC) ) {
                        alt38=1;
                    }
                    switch (alt38) {
                        case 1 :
                            // Cubrid.g:
                            {
                            set126=(Token)input.LT(1);
                            if ( input.LA(1)==ASC||input.LA(1)==DESC ) {
                                input.consume();
                                adaptor.addChild(root_0, adaptor.create(set126));
                                errorRecovery=false;
                            }
                            else {
                                MismatchedSetException mse =
                                    new MismatchedSetException(null,input);
                                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_sort_specification1969);    throw mse;
                            }


                            }
                            break;

                    }


                    }


                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end sort_specification

    public static class path_expression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start path_expression
    // Cubrid.g:334:1: path_expression : attribute_name ;
    public final path_expression_return path_expression() throws RecognitionException {
        path_expression_return retval = new path_expression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        attribute_name_return attribute_name127 = null;



        try {
            // Cubrid.g:334:16: ( attribute_name )
            // Cubrid.g:335:2: attribute_name
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_attribute_name_in_path_expression1987);
            attribute_name127=attribute_name();
            _fsp--;

            adaptor.addChild(root_0, attribute_name127.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end path_expression

    public static class query_expression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start query_expression
    // Cubrid.g:338:1: query_expression : query_term ( table_connect query_term )* -> query_term ( table_connect ENTER query_term )* ENTER ;
    public final query_expression_return query_expression() throws RecognitionException {
        query_expression_return retval = new query_expression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        query_term_return query_term128 = null;

        table_connect_return table_connect129 = null;

        query_term_return query_term130 = null;


        RewriteRuleSubtreeStream stream_table_connect=new RewriteRuleSubtreeStream(adaptor,"rule table_connect");
        RewriteRuleSubtreeStream stream_query_term=new RewriteRuleSubtreeStream(adaptor,"rule query_term");
        try {
            // Cubrid.g:338:17: ( query_term ( table_connect query_term )* -> query_term ( table_connect ENTER query_term )* ENTER )
            // Cubrid.g:339:2: query_term ( table_connect query_term )*
            {
            pushFollow(FOLLOW_query_term_in_query_expression1999);
            query_term128=query_term();
            _fsp--;

            stream_query_term.add(query_term128.getTree());
            // Cubrid.g:339:13: ( table_connect query_term )*
            loop40:
            do {
                int alt40=2;
                int LA40_0 = input.LA(1);

                if ( (LA40_0==DIFFERENCE||LA40_0==INTERSECTION||LA40_0==UNION) ) {
                    alt40=1;
                }


                switch (alt40) {
            	case 1 :
            	    // Cubrid.g:339:14: table_connect query_term
            	    {
            	    pushFollow(FOLLOW_table_connect_in_query_expression2002);
            	    table_connect129=table_connect();
            	    _fsp--;

            	    stream_table_connect.add(table_connect129.getTree());
            	    pushFollow(FOLLOW_query_term_in_query_expression2004);
            	    query_term130=query_term();
            	    _fsp--;

            	    stream_query_term.add(query_term130.getTree());

            	    }
            	    break;

            	default :
            	    break loop40;
                }
            } while (true);


            // AST REWRITE
            // elements: query_term, query_term, table_connect
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 340:2: -> query_term ( table_connect ENTER query_term )* ENTER
            {
                adaptor.addChild(root_0, stream_query_term.next());
                // Cubrid.g:340:17: ( table_connect ENTER query_term )*
                while ( stream_query_term.hasNext()||stream_table_connect.hasNext() ) {
                    adaptor.addChild(root_0, stream_table_connect.next());
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, stream_query_term.next());

                }
                stream_query_term.reset();
                stream_table_connect.reset();
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end query_expression

    public static class table_connect_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start table_connect
    // Cubrid.g:342:1: table_connect : table_operator ( ( qualifier )? ) ;
    public final table_connect_return table_connect() throws RecognitionException {
        table_connect_return retval = new table_connect_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        table_operator_return table_operator131 = null;

        qualifier_return qualifier132 = null;



        try {
            // Cubrid.g:342:14: ( table_operator ( ( qualifier )? ) )
            // Cubrid.g:343:2: table_operator ( ( qualifier )? )
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_table_operator_in_table_connect2033);
            table_operator131=table_operator();
            _fsp--;

            adaptor.addChild(root_0, table_operator131.getTree());
            // Cubrid.g:343:17: ( ( qualifier )? )
            // Cubrid.g:343:18: ( qualifier )?
            {
            // Cubrid.g:343:18: ( qualifier )?
            int alt41=2;
            int LA41_0 = input.LA(1);

            if ( (LA41_0==ALL||LA41_0==DISTINCT||LA41_0==UNIQUE) ) {
                alt41=1;
            }
            switch (alt41) {
                case 1 :
                    // Cubrid.g:343:18: qualifier
                    {
                    pushFollow(FOLLOW_qualifier_in_table_connect2036);
                    qualifier132=qualifier();
                    _fsp--;

                    adaptor.addChild(root_0, qualifier132.getTree());

                    }
                    break;

            }


            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end table_connect

    public static class query_term_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start query_term
    // Cubrid.g:345:1: query_term : query_specification ;
    public final query_term_return query_term() throws RecognitionException {
        query_term_return retval = new query_term_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        query_specification_return query_specification133 = null;



        try {
            // Cubrid.g:345:11: ( query_specification )
            // Cubrid.g:346:2: query_specification
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_query_specification_in_query_term2048);
            query_specification133=query_specification();
            _fsp--;

            adaptor.addChild(root_0, query_specification133.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end query_term

    public static class subquery_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start subquery
    // Cubrid.g:349:1: subquery : STARTBRACKET query_statement ENDBRACKET -> STARTBRACKET ENTER TAB query_statement ENTER UNTAB ENDBRACKET ENTER ;
    public final subquery_return subquery() throws RecognitionException {
        subquery_return retval = new subquery_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token STARTBRACKET134=null;
        Token ENDBRACKET136=null;
        query_statement_return query_statement135 = null;


        Object STARTBRACKET134_tree=null;
        Object ENDBRACKET136_tree=null;
        RewriteRuleTokenStream stream_ENDBRACKET=new RewriteRuleTokenStream(adaptor,"token ENDBRACKET");
        RewriteRuleTokenStream stream_STARTBRACKET=new RewriteRuleTokenStream(adaptor,"token STARTBRACKET");
        RewriteRuleSubtreeStream stream_query_statement=new RewriteRuleSubtreeStream(adaptor,"rule query_statement");
        try {
            // Cubrid.g:349:9: ( STARTBRACKET query_statement ENDBRACKET -> STARTBRACKET ENTER TAB query_statement ENTER UNTAB ENDBRACKET ENTER )
            // Cubrid.g:350:2: STARTBRACKET query_statement ENDBRACKET
            {
            STARTBRACKET134=(Token)input.LT(1);
            match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_subquery2059); 
            stream_STARTBRACKET.add(STARTBRACKET134);

            pushFollow(FOLLOW_query_statement_in_subquery2061);
            query_statement135=query_statement();
            _fsp--;

            stream_query_statement.add(query_statement135.getTree());
            ENDBRACKET136=(Token)input.LT(1);
            match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_subquery2063); 
            stream_ENDBRACKET.add(ENDBRACKET136);


            // AST REWRITE
            // elements: ENDBRACKET, query_statement, STARTBRACKET
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 351:2: -> STARTBRACKET ENTER TAB query_statement ENTER UNTAB ENDBRACKET ENTER
            {
                adaptor.addChild(root_0, stream_STARTBRACKET.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));
                adaptor.addChild(root_0, stream_query_statement.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));
                adaptor.addChild(root_0, stream_ENDBRACKET.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end subquery

    public static class path_expression_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start path_expression_comma_list
    // Cubrid.g:354:1: path_expression_comma_list : path_expression ( COMMA path_expression )* -> path_expression ( COMMA ENTER path_expression )* ENTER ;
    public final path_expression_comma_list_return path_expression_comma_list() throws RecognitionException {
        path_expression_comma_list_return retval = new path_expression_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA138=null;
        path_expression_return path_expression137 = null;

        path_expression_return path_expression139 = null;


        Object COMMA138_tree=null;
        RewriteRuleTokenStream stream_COMMA=new RewriteRuleTokenStream(adaptor,"token COMMA");
        RewriteRuleSubtreeStream stream_path_expression=new RewriteRuleSubtreeStream(adaptor,"rule path_expression");
        try {
            // Cubrid.g:354:27: ( path_expression ( COMMA path_expression )* -> path_expression ( COMMA ENTER path_expression )* ENTER )
            // Cubrid.g:355:2: path_expression ( COMMA path_expression )*
            {
            pushFollow(FOLLOW_path_expression_in_path_expression_comma_list2092);
            path_expression137=path_expression();
            _fsp--;

            stream_path_expression.add(path_expression137.getTree());
            // Cubrid.g:355:18: ( COMMA path_expression )*
            loop42:
            do {
                int alt42=2;
                int LA42_0 = input.LA(1);

                if ( (LA42_0==COMMA) ) {
                    alt42=1;
                }


                switch (alt42) {
            	case 1 :
            	    // Cubrid.g:355:19: COMMA path_expression
            	    {
            	    COMMA138=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_path_expression_comma_list2095); 
            	    stream_COMMA.add(COMMA138);

            	    pushFollow(FOLLOW_path_expression_in_path_expression_comma_list2097);
            	    path_expression139=path_expression();
            	    _fsp--;

            	    stream_path_expression.add(path_expression139.getTree());

            	    }
            	    break;

            	default :
            	    break loop42;
                }
            } while (true);


            // AST REWRITE
            // elements: path_expression, COMMA, path_expression
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 355:43: -> path_expression ( COMMA ENTER path_expression )* ENTER
            {
                adaptor.addChild(root_0, stream_path_expression.next());
                // Cubrid.g:355:62: ( COMMA ENTER path_expression )*
                while ( stream_path_expression.hasNext()||stream_COMMA.hasNext() ) {
                    adaptor.addChild(root_0, stream_COMMA.next());
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, stream_path_expression.next());

                }
                stream_path_expression.reset();
                stream_COMMA.reset();
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end path_expression_comma_list

    public static class table_operator_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start table_operator
    // Cubrid.g:358:1: table_operator : ( UNION | DIFFERENCE | INTERSECTION );
    public final table_operator_return table_operator() throws RecognitionException {
        table_operator_return retval = new table_operator_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set140=null;

        Object set140_tree=null;

        try {
            // Cubrid.g:358:15: ( UNION | DIFFERENCE | INTERSECTION )
            // Cubrid.g:
            {
            root_0 = (Object)adaptor.nil();

            set140=(Token)input.LT(1);
            if ( input.LA(1)==DIFFERENCE||input.LA(1)==INTERSECTION||input.LA(1)==UNION ) {
                input.consume();
                adaptor.addChild(root_0, adaptor.create(set140));
                errorRecovery=false;
            }
            else {
                MismatchedSetException mse =
                    new MismatchedSetException(null,input);
                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_table_operator0);    throw mse;
            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end table_operator

    public static class unsigned_integer_literal_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start unsigned_integer_literal
    // Cubrid.g:362:1: unsigned_integer_literal : DECIMALLITERAL ;
    public final unsigned_integer_literal_return unsigned_integer_literal() throws RecognitionException {
        unsigned_integer_literal_return retval = new unsigned_integer_literal_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token DECIMALLITERAL141=null;

        Object DECIMALLITERAL141_tree=null;

        try {
            // Cubrid.g:362:25: ( DECIMALLITERAL )
            // Cubrid.g:363:2: DECIMALLITERAL
            {
            root_0 = (Object)adaptor.nil();

            DECIMALLITERAL141=(Token)input.LT(1);
            match(input,DECIMALLITERAL,FOLLOW_DECIMALLITERAL_in_unsigned_integer_literal2144); 
            DECIMALLITERAL141_tree = (Object)adaptor.create(DECIMALLITERAL141);
            adaptor.addChild(root_0, DECIMALLITERAL141_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end unsigned_integer_literal

    public static class search_condition_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start search_condition
    // Cubrid.g:366:1: search_condition : condition -> condition ENTER UNTAB ;
    public final search_condition_return search_condition() throws RecognitionException {
        search_condition_return retval = new search_condition_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        condition_return condition142 = null;


        RewriteRuleSubtreeStream stream_condition=new RewriteRuleSubtreeStream(adaptor,"rule condition");
        try {
            // Cubrid.g:366:17: ( condition -> condition ENTER UNTAB )
            // Cubrid.g:367:2: condition
            {
            pushFollow(FOLLOW_condition_in_search_condition2155);
            condition142=condition();
            _fsp--;

            stream_condition.add(condition142.getTree());

            // AST REWRITE
            // elements: condition
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 367:12: -> condition ENTER UNTAB
            {
                adaptor.addChild(root_0, stream_condition.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end search_condition

    public static class condition_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start condition
    // Cubrid.g:372:1: condition : expression ;
    public final condition_return condition() throws RecognitionException {
        condition_return retval = new condition_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        expression_return expression143 = null;



        try {
            // Cubrid.g:372:10: ( expression )
            // Cubrid.g:373:2: expression
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_expression_in_condition2176);
            expression143=expression();
            _fsp--;

            adaptor.addChild(root_0, expression143.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end condition

    public static class parExpression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start parExpression
    // Cubrid.g:376:1: parExpression : STARTBRACKET expression ENDBRACKET ;
    public final parExpression_return parExpression() throws RecognitionException {
        parExpression_return retval = new parExpression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token STARTBRACKET144=null;
        Token ENDBRACKET146=null;
        expression_return expression145 = null;


        Object STARTBRACKET144_tree=null;
        Object ENDBRACKET146_tree=null;

        try {
            // Cubrid.g:376:14: ( STARTBRACKET expression ENDBRACKET )
            // Cubrid.g:377:2: STARTBRACKET expression ENDBRACKET
            {
            root_0 = (Object)adaptor.nil();

            STARTBRACKET144=(Token)input.LT(1);
            match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_parExpression2186); 
            STARTBRACKET144_tree = (Object)adaptor.create(STARTBRACKET144);
            adaptor.addChild(root_0, STARTBRACKET144_tree);

            pushFollow(FOLLOW_expression_in_parExpression2188);
            expression145=expression();
            _fsp--;

            adaptor.addChild(root_0, expression145.getTree());
            ENDBRACKET146=(Token)input.LT(1);
            match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_parExpression2190); 
            ENDBRACKET146_tree = (Object)adaptor.create(ENDBRACKET146);
            adaptor.addChild(root_0, ENDBRACKET146_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end parExpression

    public static class expression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start expression
    // Cubrid.g:380:1: expression : conditionalOrExpression ( assignmentOperator expression )? ;
    public final expression_return expression() throws RecognitionException {
        expression_return retval = new expression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        conditionalOrExpression_return conditionalOrExpression147 = null;

        assignmentOperator_return assignmentOperator148 = null;

        expression_return expression149 = null;



        try {
            // Cubrid.g:380:11: ( conditionalOrExpression ( assignmentOperator expression )? )
            // Cubrid.g:381:2: conditionalOrExpression ( assignmentOperator expression )?
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_conditionalOrExpression_in_expression2203);
            conditionalOrExpression147=conditionalOrExpression();
            _fsp--;

            adaptor.addChild(root_0, conditionalOrExpression147.getTree());
            // Cubrid.g:381:26: ( assignmentOperator expression )?
            int alt43=2;
            int LA43_0 = input.LA(1);

            if ( ((LA43_0>=156 && LA43_0<=163)) ) {
                alt43=1;
            }
            switch (alt43) {
                case 1 :
                    // Cubrid.g:381:27: assignmentOperator expression
                    {
                    pushFollow(FOLLOW_assignmentOperator_in_expression2206);
                    assignmentOperator148=assignmentOperator();
                    _fsp--;

                    adaptor.addChild(root_0, assignmentOperator148.getTree());
                    pushFollow(FOLLOW_expression_in_expression2208);
                    expression149=expression();
                    _fsp--;

                    adaptor.addChild(root_0, expression149.getTree());

                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end expression

    public static class assignmentOperator_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start assignmentOperator
    // Cubrid.g:384:1: assignmentOperator : ( '+=' | '-=' | '*=' | '/=' | '&=' | '|=' | '^=' | '%=' );
    public final assignmentOperator_return assignmentOperator() throws RecognitionException {
        assignmentOperator_return retval = new assignmentOperator_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set150=null;

        Object set150_tree=null;

        try {
            // Cubrid.g:384:19: ( '+=' | '-=' | '*=' | '/=' | '&=' | '|=' | '^=' | '%=' )
            // Cubrid.g:
            {
            root_0 = (Object)adaptor.nil();

            set150=(Token)input.LT(1);
            if ( (input.LA(1)>=156 && input.LA(1)<=163) ) {
                input.consume();
                adaptor.addChild(root_0, adaptor.create(set150));
                errorRecovery=false;
            }
            else {
                MismatchedSetException mse =
                    new MismatchedSetException(null,input);
                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_assignmentOperator0);    throw mse;
            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end assignmentOperator

    public static class conditionalOrExpression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start conditionalOrExpression
    // Cubrid.g:395:1: conditionalOrExpression : conditionalAndExpression ( OR conditionalAndExpression )* -> conditionalAndExpression ( ENTER OR conditionalAndExpression )* ;
    public final conditionalOrExpression_return conditionalOrExpression() throws RecognitionException {
        conditionalOrExpression_return retval = new conditionalOrExpression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token OR152=null;
        conditionalAndExpression_return conditionalAndExpression151 = null;

        conditionalAndExpression_return conditionalAndExpression153 = null;


        Object OR152_tree=null;
        RewriteRuleTokenStream stream_OR=new RewriteRuleTokenStream(adaptor,"token OR");
        RewriteRuleSubtreeStream stream_conditionalAndExpression=new RewriteRuleSubtreeStream(adaptor,"rule conditionalAndExpression");
        try {
            // Cubrid.g:396:5: ( conditionalAndExpression ( OR conditionalAndExpression )* -> conditionalAndExpression ( ENTER OR conditionalAndExpression )* )
            // Cubrid.g:396:9: conditionalAndExpression ( OR conditionalAndExpression )*
            {
            pushFollow(FOLLOW_conditionalAndExpression_in_conditionalOrExpression2317);
            conditionalAndExpression151=conditionalAndExpression();
            _fsp--;

            stream_conditionalAndExpression.add(conditionalAndExpression151.getTree());
            // Cubrid.g:396:34: ( OR conditionalAndExpression )*
            loop44:
            do {
                int alt44=2;
                int LA44_0 = input.LA(1);

                if ( (LA44_0==OR) ) {
                    alt44=1;
                }


                switch (alt44) {
            	case 1 :
            	    // Cubrid.g:396:36: OR conditionalAndExpression
            	    {
            	    OR152=(Token)input.LT(1);
            	    match(input,OR,FOLLOW_OR_in_conditionalOrExpression2321); 
            	    stream_OR.add(OR152);

            	    pushFollow(FOLLOW_conditionalAndExpression_in_conditionalOrExpression2323);
            	    conditionalAndExpression153=conditionalAndExpression();
            	    _fsp--;

            	    stream_conditionalAndExpression.add(conditionalAndExpression153.getTree());

            	    }
            	    break;

            	default :
            	    break loop44;
                }
            } while (true);


            // AST REWRITE
            // elements: conditionalAndExpression, OR, conditionalAndExpression
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 397:5: -> conditionalAndExpression ( ENTER OR conditionalAndExpression )*
            {
                adaptor.addChild(root_0, stream_conditionalAndExpression.next());
                // Cubrid.g:397:34: ( ENTER OR conditionalAndExpression )*
                while ( stream_conditionalAndExpression.hasNext()||stream_OR.hasNext() ) {
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, stream_OR.next());
                    adaptor.addChild(root_0, stream_conditionalAndExpression.next());

                }
                stream_conditionalAndExpression.reset();
                stream_OR.reset();

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end conditionalOrExpression

    public static class conditionalAndExpression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start conditionalAndExpression
    // Cubrid.g:400:1: conditionalAndExpression : inclusiveOrExpression ( AND inclusiveOrExpression )* -> inclusiveOrExpression ( ENTER AND inclusiveOrExpression )* ;
    public final conditionalAndExpression_return conditionalAndExpression() throws RecognitionException {
        conditionalAndExpression_return retval = new conditionalAndExpression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token AND155=null;
        inclusiveOrExpression_return inclusiveOrExpression154 = null;

        inclusiveOrExpression_return inclusiveOrExpression156 = null;


        Object AND155_tree=null;
        RewriteRuleTokenStream stream_AND=new RewriteRuleTokenStream(adaptor,"token AND");
        RewriteRuleSubtreeStream stream_inclusiveOrExpression=new RewriteRuleSubtreeStream(adaptor,"rule inclusiveOrExpression");
        try {
            // Cubrid.g:401:5: ( inclusiveOrExpression ( AND inclusiveOrExpression )* -> inclusiveOrExpression ( ENTER AND inclusiveOrExpression )* )
            // Cubrid.g:401:9: inclusiveOrExpression ( AND inclusiveOrExpression )*
            {
            pushFollow(FOLLOW_inclusiveOrExpression_in_conditionalAndExpression2365);
            inclusiveOrExpression154=inclusiveOrExpression();
            _fsp--;

            stream_inclusiveOrExpression.add(inclusiveOrExpression154.getTree());
            // Cubrid.g:401:31: ( AND inclusiveOrExpression )*
            loop45:
            do {
                int alt45=2;
                int LA45_0 = input.LA(1);

                if ( (LA45_0==AND) ) {
                    alt45=1;
                }


                switch (alt45) {
            	case 1 :
            	    // Cubrid.g:401:33: AND inclusiveOrExpression
            	    {
            	    AND155=(Token)input.LT(1);
            	    match(input,AND,FOLLOW_AND_in_conditionalAndExpression2369); 
            	    stream_AND.add(AND155);

            	    pushFollow(FOLLOW_inclusiveOrExpression_in_conditionalAndExpression2371);
            	    inclusiveOrExpression156=inclusiveOrExpression();
            	    _fsp--;

            	    stream_inclusiveOrExpression.add(inclusiveOrExpression156.getTree());

            	    }
            	    break;

            	default :
            	    break loop45;
                }
            } while (true);


            // AST REWRITE
            // elements: inclusiveOrExpression, AND, inclusiveOrExpression
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 402:5: -> inclusiveOrExpression ( ENTER AND inclusiveOrExpression )*
            {
                adaptor.addChild(root_0, stream_inclusiveOrExpression.next());
                // Cubrid.g:402:32: ( ENTER AND inclusiveOrExpression )*
                while ( stream_AND.hasNext()||stream_inclusiveOrExpression.hasNext() ) {
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, stream_AND.next());
                    adaptor.addChild(root_0, stream_inclusiveOrExpression.next());

                }
                stream_AND.reset();
                stream_inclusiveOrExpression.reset();

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end conditionalAndExpression

    public static class inclusiveOrExpression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start inclusiveOrExpression
    // Cubrid.g:406:1: inclusiveOrExpression : connectExpression ( '|' connectExpression )* ;
    public final inclusiveOrExpression_return inclusiveOrExpression() throws RecognitionException {
        inclusiveOrExpression_return retval = new inclusiveOrExpression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token char_literal158=null;
        connectExpression_return connectExpression157 = null;

        connectExpression_return connectExpression159 = null;


        Object char_literal158_tree=null;

        try {
            // Cubrid.g:407:5: ( connectExpression ( '|' connectExpression )* )
            // Cubrid.g:407:9: connectExpression ( '|' connectExpression )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_connectExpression_in_inclusiveOrExpression2419);
            connectExpression157=connectExpression();
            _fsp--;

            adaptor.addChild(root_0, connectExpression157.getTree());
            // Cubrid.g:407:27: ( '|' connectExpression )*
            loop46:
            do {
                int alt46=2;
                int LA46_0 = input.LA(1);

                if ( (LA46_0==164) ) {
                    alt46=1;
                }


                switch (alt46) {
            	case 1 :
            	    // Cubrid.g:407:29: '|' connectExpression
            	    {
            	    char_literal158=(Token)input.LT(1);
            	    match(input,164,FOLLOW_164_in_inclusiveOrExpression2423); 
            	    char_literal158_tree = (Object)adaptor.create(char_literal158);
            	    adaptor.addChild(root_0, char_literal158_tree);

            	    pushFollow(FOLLOW_connectExpression_in_inclusiveOrExpression2425);
            	    connectExpression159=connectExpression();
            	    _fsp--;

            	    adaptor.addChild(root_0, connectExpression159.getTree());

            	    }
            	    break;

            	default :
            	    break loop46;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end inclusiveOrExpression

    public static class connectExpression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start connectExpression
    // Cubrid.g:410:1: connectExpression : andExpression ( CONNECT andExpression )* ;
    public final connectExpression_return connectExpression() throws RecognitionException {
        connectExpression_return retval = new connectExpression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token CONNECT161=null;
        andExpression_return andExpression160 = null;

        andExpression_return andExpression162 = null;


        Object CONNECT161_tree=null;

        try {
            // Cubrid.g:411:5: ( andExpression ( CONNECT andExpression )* )
            // Cubrid.g:411:9: andExpression ( CONNECT andExpression )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_andExpression_in_connectExpression2447);
            andExpression160=andExpression();
            _fsp--;

            adaptor.addChild(root_0, andExpression160.getTree());
            // Cubrid.g:411:23: ( CONNECT andExpression )*
            loop47:
            do {
                int alt47=2;
                int LA47_0 = input.LA(1);

                if ( (LA47_0==CONNECT) ) {
                    alt47=1;
                }


                switch (alt47) {
            	case 1 :
            	    // Cubrid.g:411:25: CONNECT andExpression
            	    {
            	    CONNECT161=(Token)input.LT(1);
            	    match(input,CONNECT,FOLLOW_CONNECT_in_connectExpression2451); 
            	    CONNECT161_tree = (Object)adaptor.create(CONNECT161);
            	    adaptor.addChild(root_0, CONNECT161_tree);

            	    pushFollow(FOLLOW_andExpression_in_connectExpression2453);
            	    andExpression162=andExpression();
            	    _fsp--;

            	    adaptor.addChild(root_0, andExpression162.getTree());

            	    }
            	    break;

            	default :
            	    break loop47;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end connectExpression

    public static class andExpression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start andExpression
    // Cubrid.g:414:1: andExpression : equalityExpression ( '&' equalityExpression )* ;
    public final andExpression_return andExpression() throws RecognitionException {
        andExpression_return retval = new andExpression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token char_literal164=null;
        equalityExpression_return equalityExpression163 = null;

        equalityExpression_return equalityExpression165 = null;


        Object char_literal164_tree=null;

        try {
            // Cubrid.g:415:5: ( equalityExpression ( '&' equalityExpression )* )
            // Cubrid.g:415:9: equalityExpression ( '&' equalityExpression )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_equalityExpression_in_andExpression2475);
            equalityExpression163=equalityExpression();
            _fsp--;

            adaptor.addChild(root_0, equalityExpression163.getTree());
            // Cubrid.g:415:28: ( '&' equalityExpression )*
            loop48:
            do {
                int alt48=2;
                int LA48_0 = input.LA(1);

                if ( (LA48_0==165) ) {
                    alt48=1;
                }


                switch (alt48) {
            	case 1 :
            	    // Cubrid.g:415:30: '&' equalityExpression
            	    {
            	    char_literal164=(Token)input.LT(1);
            	    match(input,165,FOLLOW_165_in_andExpression2479); 
            	    char_literal164_tree = (Object)adaptor.create(char_literal164);
            	    adaptor.addChild(root_0, char_literal164_tree);

            	    pushFollow(FOLLOW_equalityExpression_in_andExpression2481);
            	    equalityExpression165=equalityExpression();
            	    _fsp--;

            	    adaptor.addChild(root_0, equalityExpression165.getTree());

            	    }
            	    break;

            	default :
            	    break loop48;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end andExpression

    public static class equalityExpression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start equalityExpression
    // Cubrid.g:418:1: equalityExpression : additiveExpression ( relationalOp additiveExpression )* ;
    public final equalityExpression_return equalityExpression() throws RecognitionException {
        equalityExpression_return retval = new equalityExpression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        additiveExpression_return additiveExpression166 = null;

        relationalOp_return relationalOp167 = null;

        additiveExpression_return additiveExpression168 = null;



        try {
            // Cubrid.g:419:5: ( additiveExpression ( relationalOp additiveExpression )* )
            // Cubrid.g:419:9: additiveExpression ( relationalOp additiveExpression )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_additiveExpression_in_equalityExpression2503);
            additiveExpression166=additiveExpression();
            _fsp--;

            adaptor.addChild(root_0, additiveExpression166.getTree());
            // Cubrid.g:419:28: ( relationalOp additiveExpression )*
            loop49:
            do {
                int alt49=2;
                int LA49_0 = input.LA(1);

                if ( (LA49_0==IN||LA49_0==IS||LA49_0==LIKE||LA49_0==NOT||LA49_0==EQUAL||(LA49_0>=167 && LA49_0<=171)) ) {
                    alt49=1;
                }


                switch (alt49) {
            	case 1 :
            	    // Cubrid.g:419:29: relationalOp additiveExpression
            	    {
            	    pushFollow(FOLLOW_relationalOp_in_equalityExpression2506);
            	    relationalOp167=relationalOp();
            	    _fsp--;

            	    adaptor.addChild(root_0, relationalOp167.getTree());
            	    pushFollow(FOLLOW_additiveExpression_in_equalityExpression2508);
            	    additiveExpression168=additiveExpression();
            	    _fsp--;

            	    adaptor.addChild(root_0, additiveExpression168.getTree());

            	    }
            	    break;

            	default :
            	    break loop49;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end equalityExpression

    public static class outer_join_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start outer_join
    // Cubrid.g:422:1: outer_join : STARTBRACE '+' ENDBRACKET ;
    public final outer_join_return outer_join() throws RecognitionException {
        outer_join_return retval = new outer_join_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token STARTBRACE169=null;
        Token char_literal170=null;
        Token ENDBRACKET171=null;

        Object STARTBRACE169_tree=null;
        Object char_literal170_tree=null;
        Object ENDBRACKET171_tree=null;

        try {
            // Cubrid.g:422:11: ( STARTBRACE '+' ENDBRACKET )
            // Cubrid.g:423:2: STARTBRACE '+' ENDBRACKET
            {
            root_0 = (Object)adaptor.nil();

            STARTBRACE169=(Token)input.LT(1);
            match(input,STARTBRACE,FOLLOW_STARTBRACE_in_outer_join2528); 
            STARTBRACE169_tree = (Object)adaptor.create(STARTBRACE169);
            adaptor.addChild(root_0, STARTBRACE169_tree);

            char_literal170=(Token)input.LT(1);
            match(input,166,FOLLOW_166_in_outer_join2530); 
            char_literal170_tree = (Object)adaptor.create(char_literal170);
            adaptor.addChild(root_0, char_literal170_tree);

            ENDBRACKET171=(Token)input.LT(1);
            match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_outer_join2532); 
            ENDBRACKET171_tree = (Object)adaptor.create(ENDBRACKET171);
            adaptor.addChild(root_0, ENDBRACKET171_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end outer_join

    public static class relationalOp_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start relationalOp
    // Cubrid.g:426:1: relationalOp : ( '=' | IS | LIKE | ( ( NOT )? IN ) | '<>' | ( '<=' ) | ( '>=' ) | '<' | '>' );
    public final relationalOp_return relationalOp() throws RecognitionException {
        relationalOp_return retval = new relationalOp_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token char_literal172=null;
        Token IS173=null;
        Token LIKE174=null;
        Token NOT175=null;
        Token IN176=null;
        Token string_literal177=null;
        Token string_literal178=null;
        Token string_literal179=null;
        Token char_literal180=null;
        Token char_literal181=null;

        Object char_literal172_tree=null;
        Object IS173_tree=null;
        Object LIKE174_tree=null;
        Object NOT175_tree=null;
        Object IN176_tree=null;
        Object string_literal177_tree=null;
        Object string_literal178_tree=null;
        Object string_literal179_tree=null;
        Object char_literal180_tree=null;
        Object char_literal181_tree=null;

        try {
            // Cubrid.g:426:13: ( '=' | IS | LIKE | ( ( NOT )? IN ) | '<>' | ( '<=' ) | ( '>=' ) | '<' | '>' )
            int alt51=9;
            switch ( input.LA(1) ) {
            case EQUAL:
                {
                alt51=1;
                }
                break;
            case IS:
                {
                alt51=2;
                }
                break;
            case LIKE:
                {
                alt51=3;
                }
                break;
            case IN:
            case NOT:
                {
                alt51=4;
                }
                break;
            case 167:
                {
                alt51=5;
                }
                break;
            case 168:
                {
                alt51=6;
                }
                break;
            case 169:
                {
                alt51=7;
                }
                break;
            case 170:
                {
                alt51=8;
                }
                break;
            case 171:
                {
                alt51=9;
                }
                break;
            default:
                NoViableAltException nvae =
                    new NoViableAltException("426:1: relationalOp : ( '=' | IS | LIKE | ( ( NOT )? IN ) | '<>' | ( '<=' ) | ( '>=' ) | '<' | '>' );", 51, 0, input);

                throw nvae;
            }

            switch (alt51) {
                case 1 :
                    // Cubrid.g:427:2: '='
                    {
                    root_0 = (Object)adaptor.nil();

                    char_literal172=(Token)input.LT(1);
                    match(input,EQUAL,FOLLOW_EQUAL_in_relationalOp2542); 
                    char_literal172_tree = (Object)adaptor.create(char_literal172);
                    adaptor.addChild(root_0, char_literal172_tree);


                    }
                    break;
                case 2 :
                    // Cubrid.g:428:4: IS
                    {
                    root_0 = (Object)adaptor.nil();

                    IS173=(Token)input.LT(1);
                    match(input,IS,FOLLOW_IS_in_relationalOp2547); 
                    IS173_tree = (Object)adaptor.create(IS173);
                    adaptor.addChild(root_0, IS173_tree);


                    }
                    break;
                case 3 :
                    // Cubrid.g:429:4: LIKE
                    {
                    root_0 = (Object)adaptor.nil();

                    LIKE174=(Token)input.LT(1);
                    match(input,LIKE,FOLLOW_LIKE_in_relationalOp2552); 
                    LIKE174_tree = (Object)adaptor.create(LIKE174);
                    adaptor.addChild(root_0, LIKE174_tree);


                    }
                    break;
                case 4 :
                    // Cubrid.g:430:4: ( ( NOT )? IN )
                    {
                    root_0 = (Object)adaptor.nil();

                    // Cubrid.g:430:4: ( ( NOT )? IN )
                    // Cubrid.g:430:5: ( NOT )? IN
                    {
                    // Cubrid.g:430:5: ( NOT )?
                    int alt50=2;
                    int LA50_0 = input.LA(1);

                    if ( (LA50_0==NOT) ) {
                        alt50=1;
                    }
                    switch (alt50) {
                        case 1 :
                            // Cubrid.g:430:5: NOT
                            {
                            NOT175=(Token)input.LT(1);
                            match(input,NOT,FOLLOW_NOT_in_relationalOp2558); 
                            NOT175_tree = (Object)adaptor.create(NOT175);
                            adaptor.addChild(root_0, NOT175_tree);


                            }
                            break;

                    }

                    IN176=(Token)input.LT(1);
                    match(input,IN,FOLLOW_IN_in_relationalOp2561); 
                    IN176_tree = (Object)adaptor.create(IN176);
                    adaptor.addChild(root_0, IN176_tree);


                    }


                    }
                    break;
                case 5 :
                    // Cubrid.g:431:4: '<>'
                    {
                    root_0 = (Object)adaptor.nil();

                    string_literal177=(Token)input.LT(1);
                    match(input,167,FOLLOW_167_in_relationalOp2567); 
                    string_literal177_tree = (Object)adaptor.create(string_literal177);
                    adaptor.addChild(root_0, string_literal177_tree);


                    }
                    break;
                case 6 :
                    // Cubrid.g:432:4: ( '<=' )
                    {
                    root_0 = (Object)adaptor.nil();

                    // Cubrid.g:432:4: ( '<=' )
                    // Cubrid.g:432:5: '<='
                    {
                    string_literal178=(Token)input.LT(1);
                    match(input,168,FOLLOW_168_in_relationalOp2573); 
                    string_literal178_tree = (Object)adaptor.create(string_literal178);
                    adaptor.addChild(root_0, string_literal178_tree);


                    }


                    }
                    break;
                case 7 :
                    // Cubrid.g:433:9: ( '>=' )
                    {
                    root_0 = (Object)adaptor.nil();

                    // Cubrid.g:433:9: ( '>=' )
                    // Cubrid.g:433:10: '>='
                    {
                    string_literal179=(Token)input.LT(1);
                    match(input,169,FOLLOW_169_in_relationalOp2585); 
                    string_literal179_tree = (Object)adaptor.create(string_literal179);
                    adaptor.addChild(root_0, string_literal179_tree);


                    }


                    }
                    break;
                case 8 :
                    // Cubrid.g:434:9: '<'
                    {
                    root_0 = (Object)adaptor.nil();

                    char_literal180=(Token)input.LT(1);
                    match(input,170,FOLLOW_170_in_relationalOp2596); 
                    char_literal180_tree = (Object)adaptor.create(char_literal180);
                    adaptor.addChild(root_0, char_literal180_tree);


                    }
                    break;
                case 9 :
                    // Cubrid.g:435:9: '>'
                    {
                    root_0 = (Object)adaptor.nil();

                    char_literal181=(Token)input.LT(1);
                    match(input,171,FOLLOW_171_in_relationalOp2607); 
                    char_literal181_tree = (Object)adaptor.create(char_literal181);
                    adaptor.addChild(root_0, char_literal181_tree);


                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end relationalOp

    public static class additiveExpression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start additiveExpression
    // Cubrid.g:439:1: additiveExpression : multiplicativeExpression ( ( '+' | '-' ) multiplicativeExpression )* ;
    public final additiveExpression_return additiveExpression() throws RecognitionException {
        additiveExpression_return retval = new additiveExpression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set183=null;
        multiplicativeExpression_return multiplicativeExpression182 = null;

        multiplicativeExpression_return multiplicativeExpression184 = null;


        Object set183_tree=null;

        try {
            // Cubrid.g:440:5: ( multiplicativeExpression ( ( '+' | '-' ) multiplicativeExpression )* )
            // Cubrid.g:440:9: multiplicativeExpression ( ( '+' | '-' ) multiplicativeExpression )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_multiplicativeExpression_in_additiveExpression2625);
            multiplicativeExpression182=multiplicativeExpression();
            _fsp--;

            adaptor.addChild(root_0, multiplicativeExpression182.getTree());
            // Cubrid.g:440:34: ( ( '+' | '-' ) multiplicativeExpression )*
            loop52:
            do {
                int alt52=2;
                int LA52_0 = input.LA(1);

                if ( (LA52_0==166||LA52_0==172) ) {
                    alt52=1;
                }


                switch (alt52) {
            	case 1 :
            	    // Cubrid.g:440:36: ( '+' | '-' ) multiplicativeExpression
            	    {
            	    set183=(Token)input.LT(1);
            	    if ( input.LA(1)==166||input.LA(1)==172 ) {
            	        input.consume();
            	        adaptor.addChild(root_0, adaptor.create(set183));
            	        errorRecovery=false;
            	    }
            	    else {
            	        MismatchedSetException mse =
            	            new MismatchedSetException(null,input);
            	        recoverFromMismatchedSet(input,mse,FOLLOW_set_in_additiveExpression2629);    throw mse;
            	    }

            	    pushFollow(FOLLOW_multiplicativeExpression_in_additiveExpression2637);
            	    multiplicativeExpression184=multiplicativeExpression();
            	    _fsp--;

            	    adaptor.addChild(root_0, multiplicativeExpression184.getTree());

            	    }
            	    break;

            	default :
            	    break loop52;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end additiveExpression

    public static class multiplicativeExpression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start multiplicativeExpression
    // Cubrid.g:443:1: multiplicativeExpression : between_expression ( ( '*' | '/' | '%' ) between_expression )* ;
    public final multiplicativeExpression_return multiplicativeExpression() throws RecognitionException {
        multiplicativeExpression_return retval = new multiplicativeExpression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set186=null;
        between_expression_return between_expression185 = null;

        between_expression_return between_expression187 = null;


        Object set186_tree=null;

        try {
            // Cubrid.g:444:5: ( between_expression ( ( '*' | '/' | '%' ) between_expression )* )
            // Cubrid.g:444:9: between_expression ( ( '*' | '/' | '%' ) between_expression )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_between_expression_in_multiplicativeExpression2659);
            between_expression185=between_expression();
            _fsp--;

            adaptor.addChild(root_0, between_expression185.getTree());
            // Cubrid.g:444:28: ( ( '*' | '/' | '%' ) between_expression )*
            loop53:
            do {
                int alt53=2;
                int LA53_0 = input.LA(1);

                if ( (LA53_0==STAR||(LA53_0>=173 && LA53_0<=174)) ) {
                    alt53=1;
                }


                switch (alt53) {
            	case 1 :
            	    // Cubrid.g:444:30: ( '*' | '/' | '%' ) between_expression
            	    {
            	    set186=(Token)input.LT(1);
            	    if ( input.LA(1)==STAR||(input.LA(1)>=173 && input.LA(1)<=174) ) {
            	        input.consume();
            	        adaptor.addChild(root_0, adaptor.create(set186));
            	        errorRecovery=false;
            	    }
            	    else {
            	        MismatchedSetException mse =
            	            new MismatchedSetException(null,input);
            	        recoverFromMismatchedSet(input,mse,FOLLOW_set_in_multiplicativeExpression2663);    throw mse;
            	    }

            	    pushFollow(FOLLOW_between_expression_in_multiplicativeExpression2677);
            	    between_expression187=between_expression();
            	    _fsp--;

            	    adaptor.addChild(root_0, between_expression187.getTree());

            	    }
            	    break;

            	default :
            	    break loop53;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end multiplicativeExpression

    public static class between_expression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start between_expression
    // Cubrid.g:447:1: between_expression : unaryExpression ( ( ( NOT )? ) BETWEEN unaryExpression AND unaryExpression )* ;
    public final between_expression_return between_expression() throws RecognitionException {
        between_expression_return retval = new between_expression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token NOT189=null;
        Token BETWEEN190=null;
        Token AND192=null;
        unaryExpression_return unaryExpression188 = null;

        unaryExpression_return unaryExpression191 = null;

        unaryExpression_return unaryExpression193 = null;


        Object NOT189_tree=null;
        Object BETWEEN190_tree=null;
        Object AND192_tree=null;

        try {
            // Cubrid.g:447:19: ( unaryExpression ( ( ( NOT )? ) BETWEEN unaryExpression AND unaryExpression )* )
            // Cubrid.g:448:2: unaryExpression ( ( ( NOT )? ) BETWEEN unaryExpression AND unaryExpression )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_unaryExpression_in_between_expression2697);
            unaryExpression188=unaryExpression();
            _fsp--;

            adaptor.addChild(root_0, unaryExpression188.getTree());
            // Cubrid.g:448:18: ( ( ( NOT )? ) BETWEEN unaryExpression AND unaryExpression )*
            loop55:
            do {
                int alt55=2;
                int LA55_0 = input.LA(1);

                if ( (LA55_0==NOT) ) {
                    int LA55_2 = input.LA(2);

                    if ( (LA55_2==BETWEEN) ) {
                        alt55=1;
                    }


                }
                else if ( (LA55_0==BETWEEN) ) {
                    alt55=1;
                }


                switch (alt55) {
            	case 1 :
            	    // Cubrid.g:448:20: ( ( NOT )? ) BETWEEN unaryExpression AND unaryExpression
            	    {
            	    // Cubrid.g:448:20: ( ( NOT )? )
            	    // Cubrid.g:448:21: ( NOT )?
            	    {
            	    // Cubrid.g:448:21: ( NOT )?
            	    int alt54=2;
            	    int LA54_0 = input.LA(1);

            	    if ( (LA54_0==NOT) ) {
            	        alt54=1;
            	    }
            	    switch (alt54) {
            	        case 1 :
            	            // Cubrid.g:448:21: NOT
            	            {
            	            NOT189=(Token)input.LT(1);
            	            match(input,NOT,FOLLOW_NOT_in_between_expression2702); 
            	            NOT189_tree = (Object)adaptor.create(NOT189);
            	            adaptor.addChild(root_0, NOT189_tree);


            	            }
            	            break;

            	    }


            	    }

            	    BETWEEN190=(Token)input.LT(1);
            	    match(input,BETWEEN,FOLLOW_BETWEEN_in_between_expression2706); 
            	    BETWEEN190_tree = (Object)adaptor.create(BETWEEN190);
            	    adaptor.addChild(root_0, BETWEEN190_tree);

            	    pushFollow(FOLLOW_unaryExpression_in_between_expression2708);
            	    unaryExpression191=unaryExpression();
            	    _fsp--;

            	    adaptor.addChild(root_0, unaryExpression191.getTree());
            	    AND192=(Token)input.LT(1);
            	    match(input,AND,FOLLOW_AND_in_between_expression2710); 
            	    AND192_tree = (Object)adaptor.create(AND192);
            	    adaptor.addChild(root_0, AND192_tree);

            	    pushFollow(FOLLOW_unaryExpression_in_between_expression2712);
            	    unaryExpression193=unaryExpression();
            	    _fsp--;

            	    adaptor.addChild(root_0, unaryExpression193.getTree());

            	    }
            	    break;

            	default :
            	    break loop55;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end between_expression

    public static class unaryExpression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start unaryExpression
    // Cubrid.g:451:1: unaryExpression : ( NOT )? ( EXISTS )? primary ;
    public final unaryExpression_return unaryExpression() throws RecognitionException {
        unaryExpression_return retval = new unaryExpression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token NOT194=null;
        Token EXISTS195=null;
        primary_return primary196 = null;


        Object NOT194_tree=null;
        Object EXISTS195_tree=null;

        try {
            // Cubrid.g:451:16: ( ( NOT )? ( EXISTS )? primary )
            // Cubrid.g:452:2: ( NOT )? ( EXISTS )? primary
            {
            root_0 = (Object)adaptor.nil();

            // Cubrid.g:452:2: ( NOT )?
            int alt56=2;
            int LA56_0 = input.LA(1);

            if ( (LA56_0==NOT) ) {
                alt56=1;
            }
            switch (alt56) {
                case 1 :
                    // Cubrid.g:452:2: NOT
                    {
                    NOT194=(Token)input.LT(1);
                    match(input,NOT,FOLLOW_NOT_in_unaryExpression2729); 
                    NOT194_tree = (Object)adaptor.create(NOT194);
                    adaptor.addChild(root_0, NOT194_tree);


                    }
                    break;

            }

            // Cubrid.g:452:7: ( EXISTS )?
            int alt57=2;
            int LA57_0 = input.LA(1);

            if ( (LA57_0==EXISTS) ) {
                alt57=1;
            }
            switch (alt57) {
                case 1 :
                    // Cubrid.g:452:7: EXISTS
                    {
                    EXISTS195=(Token)input.LT(1);
                    match(input,EXISTS,FOLLOW_EXISTS_in_unaryExpression2732); 
                    EXISTS195_tree = (Object)adaptor.create(EXISTS195);
                    adaptor.addChild(root_0, EXISTS195_tree);


                    }
                    break;

            }

            pushFollow(FOLLOW_primary_in_unaryExpression2735);
            primary196=primary();
            _fsp--;

            adaptor.addChild(root_0, primary196.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end unaryExpression

    public static class primary_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start primary
    // Cubrid.g:455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );
    public final primary_return primary() throws RecognitionException {
        primary_return retval = new primary_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token NULL200=null;
        Token STAR202=null;
        Token CASE206=null;
        Token END_STRING209=null;
        parExpression_return parExpression197 = null;

        attribute_name_return attribute_name198 = null;

        outer_join_return outer_join199 = null;

        value_return value201 = null;

        set_return set203 = null;

        function_return function204 = null;

        subquery_return subquery205 = null;

        when_expression_return when_expression207 = null;

        else_expression_return else_expression208 = null;


        Object NULL200_tree=null;
        Object STAR202_tree=null;
        Object CASE206_tree=null;
        Object END_STRING209_tree=null;
        RewriteRuleTokenStream stream_END_STRING=new RewriteRuleTokenStream(adaptor,"token END_STRING");
        RewriteRuleTokenStream stream_CASE=new RewriteRuleTokenStream(adaptor,"token CASE");
        RewriteRuleSubtreeStream stream_else_expression=new RewriteRuleSubtreeStream(adaptor,"rule else_expression");
        RewriteRuleSubtreeStream stream_when_expression=new RewriteRuleSubtreeStream(adaptor,"rule when_expression");
        RewriteRuleSubtreeStream stream_subquery=new RewriteRuleSubtreeStream(adaptor,"rule subquery");
        try {
            // Cubrid.g:455:8: ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING )
            int alt61=9;
            switch ( input.LA(1) ) {
            case STARTBRACKET:
                {
                switch ( input.LA(2) ) {
                case SELECT:
                    {
                    alt61=8;
                    }
                    break;
                case STRING:
                    {
                    int LA61_9 = input.LA(3);

                    if ( (LA61_9==COMMA) ) {
                        alt61=6;
                    }
                    else if ( (LA61_9==AND||LA61_9==BETWEEN||LA61_9==IN||LA61_9==IS||LA61_9==LIKE||LA61_9==NOT||LA61_9==OR||LA61_9==STAR||(LA61_9>=EQUAL && LA61_9<=CONNECT)||LA61_9==ENDBRACKET||(LA61_9>=156 && LA61_9<=174)) ) {
                        alt61=1;
                    }
                    else {
                        NoViableAltException nvae =
                            new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 9, input);

                        throw nvae;
                    }
                    }
                    break;
                case QUOTA:
                    {
                    int LA61_10 = input.LA(3);

                    if ( (LA61_10==172) ) {
                        int LA61_19 = input.LA(4);

                        if ( (LA61_19==DECIMALLITERAL) ) {
                            int LA61_20 = input.LA(5);

                            if ( (LA61_20==DOT) ) {
                                int LA61_24 = input.LA(6);

                                if ( (LA61_24==DECIMALLITERAL) ) {
                                    int LA61_28 = input.LA(7);

                                    if ( (LA61_28==QUOTA) ) {
                                        int LA61_25 = input.LA(8);

                                        if ( (LA61_25==COMMA) ) {
                                            alt61=6;
                                        }
                                        else if ( (LA61_25==AND||LA61_25==BETWEEN||LA61_25==IN||LA61_25==IS||LA61_25==LIKE||LA61_25==NOT||LA61_25==OR||LA61_25==STAR||(LA61_25>=EQUAL && LA61_25<=CONNECT)||LA61_25==ENDBRACKET||(LA61_25>=156 && LA61_25<=174)) ) {
                                            alt61=1;
                                        }
                                        else {
                                            NoViableAltException nvae =
                                                new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 25, input);

                                            throw nvae;
                                        }
                                    }
                                    else {
                                        NoViableAltException nvae =
                                            new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 28, input);

                                        throw nvae;
                                    }
                                }
                                else {
                                    NoViableAltException nvae =
                                        new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 24, input);

                                    throw nvae;
                                }
                            }
                            else if ( (LA61_20==QUOTA) ) {
                                int LA61_25 = input.LA(6);

                                if ( (LA61_25==COMMA) ) {
                                    alt61=6;
                                }
                                else if ( (LA61_25==AND||LA61_25==BETWEEN||LA61_25==IN||LA61_25==IS||LA61_25==LIKE||LA61_25==NOT||LA61_25==OR||LA61_25==STAR||(LA61_25>=EQUAL && LA61_25<=CONNECT)||LA61_25==ENDBRACKET||(LA61_25>=156 && LA61_25<=174)) ) {
                                    alt61=1;
                                }
                                else {
                                    NoViableAltException nvae =
                                        new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 25, input);

                                    throw nvae;
                                }
                            }
                            else {
                                NoViableAltException nvae =
                                    new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 20, input);

                                throw nvae;
                            }
                        }
                        else {
                            NoViableAltException nvae =
                                new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 19, input);

                            throw nvae;
                        }
                    }
                    else if ( (LA61_10==DECIMALLITERAL) ) {
                        int LA61_20 = input.LA(4);

                        if ( (LA61_20==DOT) ) {
                            int LA61_24 = input.LA(5);

                            if ( (LA61_24==DECIMALLITERAL) ) {
                                int LA61_28 = input.LA(6);

                                if ( (LA61_28==QUOTA) ) {
                                    int LA61_25 = input.LA(7);

                                    if ( (LA61_25==COMMA) ) {
                                        alt61=6;
                                    }
                                    else if ( (LA61_25==AND||LA61_25==BETWEEN||LA61_25==IN||LA61_25==IS||LA61_25==LIKE||LA61_25==NOT||LA61_25==OR||LA61_25==STAR||(LA61_25>=EQUAL && LA61_25<=CONNECT)||LA61_25==ENDBRACKET||(LA61_25>=156 && LA61_25<=174)) ) {
                                        alt61=1;
                                    }
                                    else {
                                        NoViableAltException nvae =
                                            new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 25, input);

                                        throw nvae;
                                    }
                                }
                                else {
                                    NoViableAltException nvae =
                                        new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 28, input);

                                    throw nvae;
                                }
                            }
                            else {
                                NoViableAltException nvae =
                                    new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 24, input);

                                throw nvae;
                            }
                        }
                        else if ( (LA61_20==QUOTA) ) {
                            int LA61_25 = input.LA(5);

                            if ( (LA61_25==COMMA) ) {
                                alt61=6;
                            }
                            else if ( (LA61_25==AND||LA61_25==BETWEEN||LA61_25==IN||LA61_25==IS||LA61_25==LIKE||LA61_25==NOT||LA61_25==OR||LA61_25==STAR||(LA61_25>=EQUAL && LA61_25<=CONNECT)||LA61_25==ENDBRACKET||(LA61_25>=156 && LA61_25<=174)) ) {
                                alt61=1;
                            }
                            else {
                                NoViableAltException nvae =
                                    new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 25, input);

                                throw nvae;
                            }
                        }
                        else {
                            NoViableAltException nvae =
                                new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 20, input);

                            throw nvae;
                        }
                    }
                    else {
                        NoViableAltException nvae =
                            new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 10, input);

                        throw nvae;
                    }
                    }
                    break;
                case 172:
                    {
                    int LA61_11 = input.LA(3);

                    if ( (LA61_11==DECIMALLITERAL) ) {
                        switch ( input.LA(4) ) {
                        case DOT:
                            {
                            int LA61_21 = input.LA(5);

                            if ( (LA61_21==DECIMALLITERAL) ) {
                                int LA61_26 = input.LA(6);

                                if ( (LA61_26==COMMA) ) {
                                    alt61=6;
                                }
                                else if ( (LA61_26==AND||LA61_26==BETWEEN||LA61_26==IN||LA61_26==IS||LA61_26==LIKE||LA61_26==NOT||LA61_26==OR||LA61_26==STAR||(LA61_26>=EQUAL && LA61_26<=CONNECT)||LA61_26==ENDBRACKET||(LA61_26>=156 && LA61_26<=174)) ) {
                                    alt61=1;
                                }
                                else {
                                    NoViableAltException nvae =
                                        new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 26, input);

                                    throw nvae;
                                }
                            }
                            else {
                                NoViableAltException nvae =
                                    new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 21, input);

                                throw nvae;
                            }
                            }
                            break;
                        case AND:
                        case BETWEEN:
                        case IN:
                        case IS:
                        case LIKE:
                        case NOT:
                        case OR:
                        case STAR:
                        case EQUAL:
                        case CONNECT:
                        case ENDBRACKET:
                        case 156:
                        case 157:
                        case 158:
                        case 159:
                        case 160:
                        case 161:
                        case 162:
                        case 163:
                        case 164:
                        case 165:
                        case 166:
                        case 167:
                        case 168:
                        case 169:
                        case 170:
                        case 171:
                        case 172:
                        case 173:
                        case 174:
                            {
                            alt61=1;
                            }
                            break;
                        case COMMA:
                            {
                            alt61=6;
                            }
                            break;
                        default:
                            NoViableAltException nvae =
                                new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 12, input);

                            throw nvae;
                        }

                    }
                    else {
                        NoViableAltException nvae =
                            new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 11, input);

                        throw nvae;
                    }
                    }
                    break;
                case DECIMALLITERAL:
                    {
                    switch ( input.LA(3) ) {
                    case DOT:
                        {
                        int LA61_21 = input.LA(4);

                        if ( (LA61_21==DECIMALLITERAL) ) {
                            int LA61_26 = input.LA(5);

                            if ( (LA61_26==COMMA) ) {
                                alt61=6;
                            }
                            else if ( (LA61_26==AND||LA61_26==BETWEEN||LA61_26==IN||LA61_26==IS||LA61_26==LIKE||LA61_26==NOT||LA61_26==OR||LA61_26==STAR||(LA61_26>=EQUAL && LA61_26<=CONNECT)||LA61_26==ENDBRACKET||(LA61_26>=156 && LA61_26<=174)) ) {
                                alt61=1;
                            }
                            else {
                                NoViableAltException nvae =
                                    new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 26, input);

                                throw nvae;
                            }
                        }
                        else {
                            NoViableAltException nvae =
                                new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 21, input);

                            throw nvae;
                        }
                        }
                        break;
                    case AND:
                    case BETWEEN:
                    case IN:
                    case IS:
                    case LIKE:
                    case NOT:
                    case OR:
                    case STAR:
                    case EQUAL:
                    case CONNECT:
                    case ENDBRACKET:
                    case 156:
                    case 157:
                    case 158:
                    case 159:
                    case 160:
                    case 161:
                    case 162:
                    case 163:
                    case 164:
                    case 165:
                    case 166:
                    case 167:
                    case 168:
                    case 169:
                    case 170:
                    case 171:
                    case 172:
                    case 173:
                    case 174:
                        {
                        alt61=1;
                        }
                        break;
                    case COMMA:
                        {
                        alt61=6;
                        }
                        break;
                    default:
                        NoViableAltException nvae =
                            new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 12, input);

                        throw nvae;
                    }

                    }
                    break;
                case DOLLAR:
                    {
                    int LA61_13 = input.LA(3);

                    if ( (LA61_13==172) ) {
                        int LA61_22 = input.LA(4);

                        if ( (LA61_22==DECIMALLITERAL) ) {
                            switch ( input.LA(5) ) {
                            case DOT:
                                {
                                int LA61_27 = input.LA(6);

                                if ( (LA61_27==DECIMALLITERAL) ) {
                                    int LA61_29 = input.LA(7);

                                    if ( (LA61_29==COMMA) ) {
                                        alt61=6;
                                    }
                                    else if ( (LA61_29==AND||LA61_29==BETWEEN||LA61_29==IN||LA61_29==IS||LA61_29==LIKE||LA61_29==NOT||LA61_29==OR||LA61_29==STAR||(LA61_29>=EQUAL && LA61_29<=CONNECT)||LA61_29==ENDBRACKET||(LA61_29>=156 && LA61_29<=174)) ) {
                                        alt61=1;
                                    }
                                    else {
                                        NoViableAltException nvae =
                                            new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 29, input);

                                        throw nvae;
                                    }
                                }
                                else {
                                    NoViableAltException nvae =
                                        new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 27, input);

                                    throw nvae;
                                }
                                }
                                break;
                            case AND:
                            case BETWEEN:
                            case IN:
                            case IS:
                            case LIKE:
                            case NOT:
                            case OR:
                            case STAR:
                            case EQUAL:
                            case CONNECT:
                            case ENDBRACKET:
                            case 156:
                            case 157:
                            case 158:
                            case 159:
                            case 160:
                            case 161:
                            case 162:
                            case 163:
                            case 164:
                            case 165:
                            case 166:
                            case 167:
                            case 168:
                            case 169:
                            case 170:
                            case 171:
                            case 172:
                            case 173:
                            case 174:
                                {
                                alt61=1;
                                }
                                break;
                            case COMMA:
                                {
                                alt61=6;
                                }
                                break;
                            default:
                                NoViableAltException nvae =
                                    new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 23, input);

                                throw nvae;
                            }

                        }
                        else {
                            NoViableAltException nvae =
                                new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 22, input);

                            throw nvae;
                        }
                    }
                    else if ( (LA61_13==DECIMALLITERAL) ) {
                        switch ( input.LA(4) ) {
                        case DOT:
                            {
                            int LA61_27 = input.LA(5);

                            if ( (LA61_27==DECIMALLITERAL) ) {
                                int LA61_29 = input.LA(6);

                                if ( (LA61_29==COMMA) ) {
                                    alt61=6;
                                }
                                else if ( (LA61_29==AND||LA61_29==BETWEEN||LA61_29==IN||LA61_29==IS||LA61_29==LIKE||LA61_29==NOT||LA61_29==OR||LA61_29==STAR||(LA61_29>=EQUAL && LA61_29<=CONNECT)||LA61_29==ENDBRACKET||(LA61_29>=156 && LA61_29<=174)) ) {
                                    alt61=1;
                                }
                                else {
                                    NoViableAltException nvae =
                                        new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 29, input);

                                    throw nvae;
                                }
                            }
                            else {
                                NoViableAltException nvae =
                                    new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 27, input);

                                throw nvae;
                            }
                            }
                            break;
                        case AND:
                        case BETWEEN:
                        case IN:
                        case IS:
                        case LIKE:
                        case NOT:
                        case OR:
                        case STAR:
                        case EQUAL:
                        case CONNECT:
                        case ENDBRACKET:
                        case 156:
                        case 157:
                        case 158:
                        case 159:
                        case 160:
                        case 161:
                        case 162:
                        case 163:
                        case 164:
                        case 165:
                        case 166:
                        case 167:
                        case 168:
                        case 169:
                        case 170:
                        case 171:
                        case 172:
                        case 173:
                        case 174:
                            {
                            alt61=1;
                            }
                            break;
                        case COMMA:
                            {
                            alt61=6;
                            }
                            break;
                        default:
                            NoViableAltException nvae =
                                new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 23, input);

                            throw nvae;
                        }

                    }
                    else {
                        NoViableAltException nvae =
                            new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 13, input);

                        throw nvae;
                    }
                    }
                    break;
                case Q_MARK:
                    {
                    int LA61_14 = input.LA(3);

                    if ( (LA61_14==COMMA) ) {
                        alt61=6;
                    }
                    else if ( (LA61_14==AND||LA61_14==BETWEEN||LA61_14==IN||LA61_14==IS||LA61_14==LIKE||LA61_14==NOT||LA61_14==OR||LA61_14==STAR||(LA61_14>=EQUAL && LA61_14<=CONNECT)||LA61_14==ENDBRACKET||(LA61_14>=156 && LA61_14<=174)) ) {
                        alt61=1;
                    }
                    else {
                        NoViableAltException nvae =
                            new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 14, input);

                        throw nvae;
                    }
                    }
                    break;
                case CASE:
                case EXISTS:
                case NOT:
                case NULL:
                case STAR:
                case ID:
                case COLUMN:
                case STARTBRACKET:
                    {
                    alt61=1;
                    }
                    break;
                default:
                    NoViableAltException nvae =
                        new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 1, input);

                    throw nvae;
                }

                }
                break;
            case ID:
                {
                int LA61_2 = input.LA(2);

                if ( (LA61_2==EOF||(LA61_2>=AND && LA61_2<=AS)||LA61_2==BETWEEN||LA61_2==DIFFERENCE||(LA61_2>=ELSE && LA61_2<=END_STRING)||LA61_2==FROM||(LA61_2>=GROUP && LA61_2<=IN)||LA61_2==INHERIT||(LA61_2>=INTERSECTION && LA61_2<=IS)||LA61_2==LIKE||LA61_2==LEFT||LA61_2==NOT||LA61_2==ON||(LA61_2>=OR && LA61_2<=ORDER)||LA61_2==RIGHT||LA61_2==THEN||LA61_2==TO||LA61_2==UNION||LA61_2==USING||(LA61_2>=WHEN && LA61_2<=WITH)||(LA61_2>=END && LA61_2<=STARTBRACE)||LA61_2==DOT||(LA61_2>=EQUAL && LA61_2<=CONNECT)||LA61_2==ID||LA61_2==ENDBRACKET||(LA61_2>=156 && LA61_2<=174)) ) {
                    alt61=2;
                }
                else if ( (LA61_2==STARTBRACKET) ) {
                    alt61=7;
                }
                else {
                    NoViableAltException nvae =
                        new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 2, input);

                    throw nvae;
                }
                }
                break;
            case COLUMN:
                {
                alt61=2;
                }
                break;
            case NULL:
                {
                alt61=3;
                }
                break;
            case QUOTA:
            case DOLLAR:
            case Q_MARK:
            case DECIMALLITERAL:
            case STRING:
            case 172:
                {
                alt61=4;
                }
                break;
            case STAR:
                {
                alt61=5;
                }
                break;
            case CASE:
                {
                alt61=9;
                }
                break;
            default:
                NoViableAltException nvae =
                    new NoViableAltException("455:1: primary : ( parExpression | attribute_name ( ( outer_join )? ) | NULL | value | STAR | set | function | subquery -> ENTER subquery | CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING );", 61, 0, input);

                throw nvae;
            }

            switch (alt61) {
                case 1 :
                    // Cubrid.g:456:2: parExpression
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_parExpression_in_primary2745);
                    parExpression197=parExpression();
                    _fsp--;

                    adaptor.addChild(root_0, parExpression197.getTree());

                    }
                    break;
                case 2 :
                    // Cubrid.g:457:3: attribute_name ( ( outer_join )? )
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_attribute_name_in_primary2749);
                    attribute_name198=attribute_name();
                    _fsp--;

                    adaptor.addChild(root_0, attribute_name198.getTree());
                    // Cubrid.g:457:18: ( ( outer_join )? )
                    // Cubrid.g:457:19: ( outer_join )?
                    {
                    // Cubrid.g:457:19: ( outer_join )?
                    int alt58=2;
                    int LA58_0 = input.LA(1);

                    if ( (LA58_0==STARTBRACE) ) {
                        alt58=1;
                    }
                    switch (alt58) {
                        case 1 :
                            // Cubrid.g:457:19: outer_join
                            {
                            pushFollow(FOLLOW_outer_join_in_primary2752);
                            outer_join199=outer_join();
                            _fsp--;

                            adaptor.addChild(root_0, outer_join199.getTree());

                            }
                            break;

                    }


                    }


                    }
                    break;
                case 3 :
                    // Cubrid.g:458:4: NULL
                    {
                    root_0 = (Object)adaptor.nil();

                    NULL200=(Token)input.LT(1);
                    match(input,NULL,FOLLOW_NULL_in_primary2759); 
                    NULL200_tree = (Object)adaptor.create(NULL200);
                    adaptor.addChild(root_0, NULL200_tree);


                    }
                    break;
                case 4 :
                    // Cubrid.g:459:4: value
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_value_in_primary2764);
                    value201=value();
                    _fsp--;

                    adaptor.addChild(root_0, value201.getTree());

                    }
                    break;
                case 5 :
                    // Cubrid.g:460:4: STAR
                    {
                    root_0 = (Object)adaptor.nil();

                    STAR202=(Token)input.LT(1);
                    match(input,STAR,FOLLOW_STAR_in_primary2769); 
                    STAR202_tree = (Object)adaptor.create(STAR202);
                    adaptor.addChild(root_0, STAR202_tree);


                    }
                    break;
                case 6 :
                    // Cubrid.g:461:4: set
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_set_in_primary2774);
                    set203=set();
                    _fsp--;

                    adaptor.addChild(root_0, set203.getTree());

                    }
                    break;
                case 7 :
                    // Cubrid.g:462:4: function
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_function_in_primary2779);
                    function204=function();
                    _fsp--;

                    adaptor.addChild(root_0, function204.getTree());

                    }
                    break;
                case 8 :
                    // Cubrid.g:463:4: subquery
                    {
                    pushFollow(FOLLOW_subquery_in_primary2784);
                    subquery205=subquery();
                    _fsp--;

                    stream_subquery.add(subquery205.getTree());

                    // AST REWRITE
                    // elements: subquery
                    // token labels: 
                    // rule labels: retval
                    // token list labels: 
                    // rule list labels: 
                    retval.tree = root_0;
                    RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

                    root_0 = (Object)adaptor.nil();
                    // 463:13: -> ENTER subquery
                    {
                        adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                        adaptor.addChild(root_0, stream_subquery.next());

                    }



                    }
                    break;
                case 9 :
                    // Cubrid.g:465:4: CASE ( ( when_expression )+ ) ( ( else_expression )? ) END_STRING
                    {
                    CASE206=(Token)input.LT(1);
                    match(input,CASE,FOLLOW_CASE_in_primary2797); 
                    stream_CASE.add(CASE206);

                    // Cubrid.g:465:9: ( ( when_expression )+ )
                    // Cubrid.g:465:10: ( when_expression )+
                    {
                    // Cubrid.g:465:10: ( when_expression )+
                    int cnt59=0;
                    loop59:
                    do {
                        int alt59=2;
                        int LA59_0 = input.LA(1);

                        if ( (LA59_0==WHEN) ) {
                            alt59=1;
                        }


                        switch (alt59) {
                    	case 1 :
                    	    // Cubrid.g:465:10: when_expression
                    	    {
                    	    pushFollow(FOLLOW_when_expression_in_primary2800);
                    	    when_expression207=when_expression();
                    	    _fsp--;

                    	    stream_when_expression.add(when_expression207.getTree());

                    	    }
                    	    break;

                    	default :
                    	    if ( cnt59 >= 1 ) break loop59;
                                EarlyExitException eee =
                                    new EarlyExitException(59, input);
                                throw eee;
                        }
                        cnt59++;
                    } while (true);


                    }

                    // Cubrid.g:465:28: ( ( else_expression )? )
                    // Cubrid.g:465:29: ( else_expression )?
                    {
                    // Cubrid.g:465:29: ( else_expression )?
                    int alt60=2;
                    int LA60_0 = input.LA(1);

                    if ( (LA60_0==ELSE) ) {
                        alt60=1;
                    }
                    switch (alt60) {
                        case 1 :
                            // Cubrid.g:465:29: else_expression
                            {
                            pushFollow(FOLLOW_else_expression_in_primary2805);
                            else_expression208=else_expression();
                            _fsp--;

                            stream_else_expression.add(else_expression208.getTree());

                            }
                            break;

                    }


                    }

                    END_STRING209=(Token)input.LT(1);
                    match(input,END_STRING,FOLLOW_END_STRING_in_primary2809); 
                    stream_END_STRING.add(END_STRING209);


                    // AST REWRITE
                    // elements: END_STRING, when_expression, CASE, else_expression
                    // token labels: 
                    // rule labels: retval
                    // token list labels: 
                    // rule list labels: 
                    retval.tree = root_0;
                    RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

                    root_0 = (Object)adaptor.nil();
                    // 465:58: -> CASE ENTER TAB ( when_expression )+ ( else_expression )? UNTAB END_STRING
                    {
                        adaptor.addChild(root_0, stream_CASE.next());
                        adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                        adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));
                        if ( !(stream_when_expression.hasNext()) ) {
                            throw new RewriteEarlyExitException();
                        }
                        while ( stream_when_expression.hasNext() ) {
                            adaptor.addChild(root_0, stream_when_expression.next());

                        }
                        stream_when_expression.reset();
                        // Cubrid.g:466:36: ( else_expression )?
                        if ( stream_else_expression.hasNext() ) {
                            adaptor.addChild(root_0, stream_else_expression.next());

                        }
                        stream_else_expression.reset();
                        adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));
                        adaptor.addChild(root_0, stream_END_STRING.next());

                    }



                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end primary

    public static class when_expression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start when_expression
    // Cubrid.g:469:1: when_expression : WHEN expression THEN expression -> WHEN expression THEN expression ENTER ;
    public final when_expression_return when_expression() throws RecognitionException {
        when_expression_return retval = new when_expression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token WHEN210=null;
        Token THEN212=null;
        expression_return expression211 = null;

        expression_return expression213 = null;


        Object WHEN210_tree=null;
        Object THEN212_tree=null;
        RewriteRuleTokenStream stream_THEN=new RewriteRuleTokenStream(adaptor,"token THEN");
        RewriteRuleTokenStream stream_WHEN=new RewriteRuleTokenStream(adaptor,"token WHEN");
        RewriteRuleSubtreeStream stream_expression=new RewriteRuleSubtreeStream(adaptor,"rule expression");
        try {
            // Cubrid.g:469:16: ( WHEN expression THEN expression -> WHEN expression THEN expression ENTER )
            // Cubrid.g:470:2: WHEN expression THEN expression
            {
            WHEN210=(Token)input.LT(1);
            match(input,WHEN,FOLLOW_WHEN_in_when_expression2849); 
            stream_WHEN.add(WHEN210);

            pushFollow(FOLLOW_expression_in_when_expression2851);
            expression211=expression();
            _fsp--;

            stream_expression.add(expression211.getTree());
            THEN212=(Token)input.LT(1);
            match(input,THEN,FOLLOW_THEN_in_when_expression2853); 
            stream_THEN.add(THEN212);

            pushFollow(FOLLOW_expression_in_when_expression2855);
            expression213=expression();
            _fsp--;

            stream_expression.add(expression213.getTree());

            // AST REWRITE
            // elements: expression, THEN, WHEN, expression
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 470:34: -> WHEN expression THEN expression ENTER
            {
                adaptor.addChild(root_0, stream_WHEN.next());
                adaptor.addChild(root_0, stream_expression.next());
                adaptor.addChild(root_0, stream_THEN.next());
                adaptor.addChild(root_0, stream_expression.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end when_expression

    public static class else_expression_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start else_expression
    // Cubrid.g:474:1: else_expression : ELSE expression -> ELSE expression ENTER ;
    public final else_expression_return else_expression() throws RecognitionException {
        else_expression_return retval = new else_expression_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ELSE214=null;
        expression_return expression215 = null;


        Object ELSE214_tree=null;
        RewriteRuleTokenStream stream_ELSE=new RewriteRuleTokenStream(adaptor,"token ELSE");
        RewriteRuleSubtreeStream stream_expression=new RewriteRuleSubtreeStream(adaptor,"rule expression");
        try {
            // Cubrid.g:474:16: ( ELSE expression -> ELSE expression ENTER )
            // Cubrid.g:475:2: ELSE expression
            {
            ELSE214=(Token)input.LT(1);
            match(input,ELSE,FOLLOW_ELSE_in_else_expression2879); 
            stream_ELSE.add(ELSE214);

            pushFollow(FOLLOW_expression_in_else_expression2881);
            expression215=expression();
            _fsp--;

            stream_expression.add(expression215.getTree());

            // AST REWRITE
            // elements: expression, ELSE
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 475:18: -> ELSE expression ENTER
            {
                adaptor.addChild(root_0, stream_ELSE.next());
                adaptor.addChild(root_0, stream_expression.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end else_expression

    public static class index_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start index_comma_list
    // Cubrid.g:478:1: index_comma_list : index ( COMMA index )* ;
    public final index_comma_list_return index_comma_list() throws RecognitionException {
        index_comma_list_return retval = new index_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA217=null;
        index_return index216 = null;

        index_return index218 = null;


        Object COMMA217_tree=null;

        try {
            // Cubrid.g:478:17: ( index ( COMMA index )* )
            // Cubrid.g:479:2: index ( COMMA index )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_index_in_index_comma_list2901);
            index216=index();
            _fsp--;

            adaptor.addChild(root_0, index216.getTree());
            // Cubrid.g:479:8: ( COMMA index )*
            loop62:
            do {
                int alt62=2;
                int LA62_0 = input.LA(1);

                if ( (LA62_0==COMMA) ) {
                    alt62=1;
                }


                switch (alt62) {
            	case 1 :
            	    // Cubrid.g:479:9: COMMA index
            	    {
            	    COMMA217=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_index_comma_list2904); 
            	    COMMA217_tree = (Object)adaptor.create(COMMA217);
            	    adaptor.addChild(root_0, COMMA217_tree);

            	    pushFollow(FOLLOW_index_in_index_comma_list2906);
            	    index218=index();
            	    _fsp--;

            	    adaptor.addChild(root_0, index218.getTree());

            	    }
            	    break;

            	default :
            	    break loop62;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end index_comma_list

    public static class index_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start index
    // Cubrid.g:482:1: index : ID ( DOT ID )? ;
    public final index_return index() throws RecognitionException {
        index_return retval = new index_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ID219=null;
        Token DOT220=null;
        Token ID221=null;

        Object ID219_tree=null;
        Object DOT220_tree=null;
        Object ID221_tree=null;

        try {
            // Cubrid.g:482:6: ( ID ( DOT ID )? )
            // Cubrid.g:483:2: ID ( DOT ID )?
            {
            root_0 = (Object)adaptor.nil();

            ID219=(Token)input.LT(1);
            match(input,ID,FOLLOW_ID_in_index2919); 
            ID219_tree = (Object)adaptor.create(ID219);
            adaptor.addChild(root_0, ID219_tree);

            // Cubrid.g:483:5: ( DOT ID )?
            int alt63=2;
            int LA63_0 = input.LA(1);

            if ( (LA63_0==DOT) ) {
                alt63=1;
            }
            switch (alt63) {
                case 1 :
                    // Cubrid.g:483:6: DOT ID
                    {
                    DOT220=(Token)input.LT(1);
                    match(input,DOT,FOLLOW_DOT_in_index2922); 
                    DOT220_tree = (Object)adaptor.create(DOT220);
                    adaptor.addChild(root_0, DOT220_tree);

                    ID221=(Token)input.LT(1);
                    match(input,ID,FOLLOW_ID_in_index2924); 
                    ID221_tree = (Object)adaptor.create(ID221);
                    adaptor.addChild(root_0, ID221_tree);


                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end index

    public static class insert_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start insert
    // Cubrid.g:488:1: insert : INSERT INTO class_name insert_spec -> INSERT INTO class_name ENTER insert_spec ;
    public final insert_return insert() throws RecognitionException {
        insert_return retval = new insert_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token INSERT222=null;
        Token INTO223=null;
        class_name_return class_name224 = null;

        insert_spec_return insert_spec225 = null;


        Object INSERT222_tree=null;
        Object INTO223_tree=null;
        RewriteRuleTokenStream stream_INSERT=new RewriteRuleTokenStream(adaptor,"token INSERT");
        RewriteRuleTokenStream stream_INTO=new RewriteRuleTokenStream(adaptor,"token INTO");
        RewriteRuleSubtreeStream stream_insert_spec=new RewriteRuleSubtreeStream(adaptor,"rule insert_spec");
        RewriteRuleSubtreeStream stream_class_name=new RewriteRuleSubtreeStream(adaptor,"rule class_name");
        try {
            // Cubrid.g:488:7: ( INSERT INTO class_name insert_spec -> INSERT INTO class_name ENTER insert_spec )
            // Cubrid.g:489:2: INSERT INTO class_name insert_spec
            {
            INSERT222=(Token)input.LT(1);
            match(input,INSERT,FOLLOW_INSERT_in_insert2941); 
            stream_INSERT.add(INSERT222);

            INTO223=(Token)input.LT(1);
            match(input,INTO,FOLLOW_INTO_in_insert2943); 
            stream_INTO.add(INTO223);

            pushFollow(FOLLOW_class_name_in_insert2945);
            class_name224=class_name();
            _fsp--;

            stream_class_name.add(class_name224.getTree());
            pushFollow(FOLLOW_insert_spec_in_insert2947);
            insert_spec225=insert_spec();
            _fsp--;

            stream_insert_spec.add(insert_spec225.getTree());

            // AST REWRITE
            // elements: class_name, insert_spec, INTO, INSERT
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 489:37: -> INSERT INTO class_name ENTER insert_spec
            {
                adaptor.addChild(root_0, stream_INSERT.next());
                adaptor.addChild(root_0, stream_INTO.next());
                adaptor.addChild(root_0, stream_class_name.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, stream_insert_spec.next());

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end insert

    public static class insert_spec_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start insert_spec
    // Cubrid.g:491:1: insert_spec : ( ( attributes )? value_clause -> ( attributes )? value_clause | ( attributes )? DEFAULT VALUES -> ( attributes )? DEFAULT VALUES );
    public final insert_spec_return insert_spec() throws RecognitionException {
        insert_spec_return retval = new insert_spec_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token DEFAULT229=null;
        Token VALUES230=null;
        attributes_return attributes226 = null;

        value_clause_return value_clause227 = null;

        attributes_return attributes228 = null;


        Object DEFAULT229_tree=null;
        Object VALUES230_tree=null;
        RewriteRuleTokenStream stream_DEFAULT=new RewriteRuleTokenStream(adaptor,"token DEFAULT");
        RewriteRuleTokenStream stream_VALUES=new RewriteRuleTokenStream(adaptor,"token VALUES");
        RewriteRuleSubtreeStream stream_value_clause=new RewriteRuleSubtreeStream(adaptor,"rule value_clause");
        RewriteRuleSubtreeStream stream_attributes=new RewriteRuleSubtreeStream(adaptor,"rule attributes");
        try {
            // Cubrid.g:491:12: ( ( attributes )? value_clause -> ( attributes )? value_clause | ( attributes )? DEFAULT VALUES -> ( attributes )? DEFAULT VALUES )
            int alt66=2;
            alt66 = dfa66.predict(input);
            switch (alt66) {
                case 1 :
                    // Cubrid.g:492:2: ( attributes )? value_clause
                    {
                    // Cubrid.g:492:2: ( attributes )?
                    int alt64=2;
                    int LA64_0 = input.LA(1);

                    if ( (LA64_0==STARTBRACKET) ) {
                        alt64=1;
                    }
                    switch (alt64) {
                        case 1 :
                            // Cubrid.g:492:3: attributes
                            {
                            pushFollow(FOLLOW_attributes_in_insert_spec2969);
                            attributes226=attributes();
                            _fsp--;

                            stream_attributes.add(attributes226.getTree());

                            }
                            break;

                    }

                    pushFollow(FOLLOW_value_clause_in_insert_spec2973);
                    value_clause227=value_clause();
                    _fsp--;

                    stream_value_clause.add(value_clause227.getTree());

                    // AST REWRITE
                    // elements: value_clause, attributes
                    // token labels: 
                    // rule labels: retval
                    // token list labels: 
                    // rule list labels: 
                    retval.tree = root_0;
                    RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

                    root_0 = (Object)adaptor.nil();
                    // 492:29: -> ( attributes )? value_clause
                    {
                        // Cubrid.g:492:32: ( attributes )?
                        if ( stream_attributes.hasNext() ) {
                            adaptor.addChild(root_0, stream_attributes.next());

                        }
                        stream_attributes.reset();
                        adaptor.addChild(root_0, stream_value_clause.next());

                    }



                    }
                    break;
                case 2 :
                    // Cubrid.g:493:3: ( attributes )? DEFAULT VALUES
                    {
                    // Cubrid.g:493:3: ( attributes )?
                    int alt65=2;
                    int LA65_0 = input.LA(1);

                    if ( (LA65_0==STARTBRACKET) ) {
                        alt65=1;
                    }
                    switch (alt65) {
                        case 1 :
                            // Cubrid.g:493:4: attributes
                            {
                            pushFollow(FOLLOW_attributes_in_insert_spec2989);
                            attributes228=attributes();
                            _fsp--;

                            stream_attributes.add(attributes228.getTree());

                            }
                            break;

                    }

                    DEFAULT229=(Token)input.LT(1);
                    match(input,DEFAULT,FOLLOW_DEFAULT_in_insert_spec2993); 
                    stream_DEFAULT.add(DEFAULT229);

                    VALUES230=(Token)input.LT(1);
                    match(input,VALUES,FOLLOW_VALUES_in_insert_spec2995); 
                    stream_VALUES.add(VALUES230);


                    // AST REWRITE
                    // elements: VALUES, DEFAULT, attributes
                    // token labels: 
                    // rule labels: retval
                    // token list labels: 
                    // rule list labels: 
                    retval.tree = root_0;
                    RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

                    root_0 = (Object)adaptor.nil();
                    // 493:32: -> ( attributes )? DEFAULT VALUES
                    {
                        // Cubrid.g:493:34: ( attributes )?
                        if ( stream_attributes.hasNext() ) {
                            adaptor.addChild(root_0, stream_attributes.next());

                        }
                        stream_attributes.reset();
                        adaptor.addChild(root_0, stream_DEFAULT.next());
                        adaptor.addChild(root_0, stream_VALUES.next());

                    }



                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end insert_spec

    public static class attributes_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start attributes
    // Cubrid.g:496:1: attributes : STARTBRACKET attribute ( COMMA attribute )* ENDBRACKET -> STARTBRACKET ENTER TAB attribute ( COMMA ENTER attribute )* ENTER UNTAB ENDBRACKET ENTER ;
    public final attributes_return attributes() throws RecognitionException {
        attributes_return retval = new attributes_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token STARTBRACKET231=null;
        Token COMMA233=null;
        Token ENDBRACKET235=null;
        attribute_return attribute232 = null;

        attribute_return attribute234 = null;


        Object STARTBRACKET231_tree=null;
        Object COMMA233_tree=null;
        Object ENDBRACKET235_tree=null;
        RewriteRuleTokenStream stream_ENDBRACKET=new RewriteRuleTokenStream(adaptor,"token ENDBRACKET");
        RewriteRuleTokenStream stream_COMMA=new RewriteRuleTokenStream(adaptor,"token COMMA");
        RewriteRuleTokenStream stream_STARTBRACKET=new RewriteRuleTokenStream(adaptor,"token STARTBRACKET");
        RewriteRuleSubtreeStream stream_attribute=new RewriteRuleSubtreeStream(adaptor,"rule attribute");
        try {
            // Cubrid.g:496:11: ( STARTBRACKET attribute ( COMMA attribute )* ENDBRACKET -> STARTBRACKET ENTER TAB attribute ( COMMA ENTER attribute )* ENTER UNTAB ENDBRACKET ENTER )
            // Cubrid.g:497:2: STARTBRACKET attribute ( COMMA attribute )* ENDBRACKET
            {
            STARTBRACKET231=(Token)input.LT(1);
            match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_attributes3016); 
            stream_STARTBRACKET.add(STARTBRACKET231);

            pushFollow(FOLLOW_attribute_in_attributes3018);
            attribute232=attribute();
            _fsp--;

            stream_attribute.add(attribute232.getTree());
            // Cubrid.g:497:25: ( COMMA attribute )*
            loop67:
            do {
                int alt67=2;
                int LA67_0 = input.LA(1);

                if ( (LA67_0==COMMA) ) {
                    alt67=1;
                }


                switch (alt67) {
            	case 1 :
            	    // Cubrid.g:497:26: COMMA attribute
            	    {
            	    COMMA233=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_attributes3021); 
            	    stream_COMMA.add(COMMA233);

            	    pushFollow(FOLLOW_attribute_in_attributes3023);
            	    attribute234=attribute();
            	    _fsp--;

            	    stream_attribute.add(attribute234.getTree());

            	    }
            	    break;

            	default :
            	    break loop67;
                }
            } while (true);

            ENDBRACKET235=(Token)input.LT(1);
            match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_attributes3027); 
            stream_ENDBRACKET.add(ENDBRACKET235);


            // AST REWRITE
            // elements: ENDBRACKET, STARTBRACKET, attribute, attribute, COMMA
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 498:2: -> STARTBRACKET ENTER TAB attribute ( COMMA ENTER attribute )* ENTER UNTAB ENDBRACKET ENTER
            {
                adaptor.addChild(root_0, stream_STARTBRACKET.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));
                adaptor.addChild(root_0, stream_attribute.next());
                // Cubrid.g:498:38: ( COMMA ENTER attribute )*
                while ( stream_attribute.hasNext()||stream_COMMA.hasNext() ) {
                    adaptor.addChild(root_0, stream_COMMA.next());
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, stream_attribute.next());

                }
                stream_attribute.reset();
                stream_COMMA.reset();
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));
                adaptor.addChild(root_0, stream_ENDBRACKET.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end attributes

    public static class attribute_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start attribute
    // Cubrid.g:501:1: attribute : ID ;
    public final attribute_return attribute() throws RecognitionException {
        attribute_return retval = new attribute_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ID236=null;

        Object ID236_tree=null;

        try {
            // Cubrid.g:501:10: ( ID )
            // Cubrid.g:502:2: ID
            {
            root_0 = (Object)adaptor.nil();

            ID236=(Token)input.LT(1);
            match(input,ID,FOLLOW_ID_in_attribute3066); 
            ID236_tree = (Object)adaptor.create(ID236);
            adaptor.addChild(root_0, ID236_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end attribute

    public static class value_clause_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start value_clause
    // Cubrid.g:505:1: value_clause : VALUES STARTBRACKET insert_item_comma_list ENDBRACKET ( TO variable )? -> VALUES ENTER STARTBRACKET ENTER TAB insert_item_comma_list UNTAB ENDBRACKET ( TO variable )? ;
    public final value_clause_return value_clause() throws RecognitionException {
        value_clause_return retval = new value_clause_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token VALUES237=null;
        Token STARTBRACKET238=null;
        Token ENDBRACKET240=null;
        Token TO241=null;
        insert_item_comma_list_return insert_item_comma_list239 = null;

        variable_return variable242 = null;


        Object VALUES237_tree=null;
        Object STARTBRACKET238_tree=null;
        Object ENDBRACKET240_tree=null;
        Object TO241_tree=null;
        RewriteRuleTokenStream stream_ENDBRACKET=new RewriteRuleTokenStream(adaptor,"token ENDBRACKET");
        RewriteRuleTokenStream stream_TO=new RewriteRuleTokenStream(adaptor,"token TO");
        RewriteRuleTokenStream stream_STARTBRACKET=new RewriteRuleTokenStream(adaptor,"token STARTBRACKET");
        RewriteRuleTokenStream stream_VALUES=new RewriteRuleTokenStream(adaptor,"token VALUES");
        RewriteRuleSubtreeStream stream_variable=new RewriteRuleSubtreeStream(adaptor,"rule variable");
        RewriteRuleSubtreeStream stream_insert_item_comma_list=new RewriteRuleSubtreeStream(adaptor,"rule insert_item_comma_list");
        try {
            // Cubrid.g:505:13: ( VALUES STARTBRACKET insert_item_comma_list ENDBRACKET ( TO variable )? -> VALUES ENTER STARTBRACKET ENTER TAB insert_item_comma_list UNTAB ENDBRACKET ( TO variable )? )
            // Cubrid.g:506:2: VALUES STARTBRACKET insert_item_comma_list ENDBRACKET ( TO variable )?
            {
            VALUES237=(Token)input.LT(1);
            match(input,VALUES,FOLLOW_VALUES_in_value_clause3077); 
            stream_VALUES.add(VALUES237);

            STARTBRACKET238=(Token)input.LT(1);
            match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_value_clause3080); 
            stream_STARTBRACKET.add(STARTBRACKET238);

            pushFollow(FOLLOW_insert_item_comma_list_in_value_clause3082);
            insert_item_comma_list239=insert_item_comma_list();
            _fsp--;

            stream_insert_item_comma_list.add(insert_item_comma_list239.getTree());
            ENDBRACKET240=(Token)input.LT(1);
            match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_value_clause3084); 
            stream_ENDBRACKET.add(ENDBRACKET240);

            // Cubrid.g:508:2: ( TO variable )?
            int alt68=2;
            int LA68_0 = input.LA(1);

            if ( (LA68_0==TO) ) {
                alt68=1;
            }
            switch (alt68) {
                case 1 :
                    // Cubrid.g:508:3: TO variable
                    {
                    TO241=(Token)input.LT(1);
                    match(input,TO,FOLLOW_TO_in_value_clause3088); 
                    stream_TO.add(TO241);

                    pushFollow(FOLLOW_variable_in_value_clause3090);
                    variable242=variable();
                    _fsp--;

                    stream_variable.add(variable242.getTree());

                    }
                    break;

            }


            // AST REWRITE
            // elements: STARTBRACKET, ENDBRACKET, TO, variable, insert_item_comma_list, VALUES
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 509:2: -> VALUES ENTER STARTBRACKET ENTER TAB insert_item_comma_list UNTAB ENDBRACKET ( TO variable )?
            {
                adaptor.addChild(root_0, stream_VALUES.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, stream_STARTBRACKET.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));
                adaptor.addChild(root_0, stream_insert_item_comma_list.next());
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));
                adaptor.addChild(root_0, stream_ENDBRACKET.next());
                // Cubrid.g:512:2: ( TO variable )?
                if ( stream_TO.hasNext()||stream_variable.hasNext() ) {
                    adaptor.addChild(root_0, stream_TO.next());
                    adaptor.addChild(root_0, stream_variable.next());

                }
                stream_TO.reset();
                stream_variable.reset();

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end value_clause

    public static class insert_item_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start insert_item_comma_list
    // Cubrid.g:515:1: insert_item_comma_list : insert_item ( COMMA insert_item )* -> insert_item ( COMMA ENTER insert_item )* ENTER ;
    public final insert_item_comma_list_return insert_item_comma_list() throws RecognitionException {
        insert_item_comma_list_return retval = new insert_item_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA244=null;
        insert_item_return insert_item243 = null;

        insert_item_return insert_item245 = null;


        Object COMMA244_tree=null;
        RewriteRuleTokenStream stream_COMMA=new RewriteRuleTokenStream(adaptor,"token COMMA");
        RewriteRuleSubtreeStream stream_insert_item=new RewriteRuleSubtreeStream(adaptor,"rule insert_item");
        try {
            // Cubrid.g:515:23: ( insert_item ( COMMA insert_item )* -> insert_item ( COMMA ENTER insert_item )* ENTER )
            // Cubrid.g:516:2: insert_item ( COMMA insert_item )*
            {
            pushFollow(FOLLOW_insert_item_in_insert_item_comma_list3134);
            insert_item243=insert_item();
            _fsp--;

            stream_insert_item.add(insert_item243.getTree());
            // Cubrid.g:516:14: ( COMMA insert_item )*
            loop69:
            do {
                int alt69=2;
                int LA69_0 = input.LA(1);

                if ( (LA69_0==COMMA) ) {
                    alt69=1;
                }


                switch (alt69) {
            	case 1 :
            	    // Cubrid.g:516:15: COMMA insert_item
            	    {
            	    COMMA244=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_insert_item_comma_list3137); 
            	    stream_COMMA.add(COMMA244);

            	    pushFollow(FOLLOW_insert_item_in_insert_item_comma_list3139);
            	    insert_item245=insert_item();
            	    _fsp--;

            	    stream_insert_item.add(insert_item245.getTree());

            	    }
            	    break;

            	default :
            	    break loop69;
                }
            } while (true);


            // AST REWRITE
            // elements: insert_item, COMMA, insert_item
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 516:35: -> insert_item ( COMMA ENTER insert_item )* ENTER
            {
                adaptor.addChild(root_0, stream_insert_item.next());
                // Cubrid.g:516:50: ( COMMA ENTER insert_item )*
                while ( stream_insert_item.hasNext()||stream_COMMA.hasNext() ) {
                    adaptor.addChild(root_0, stream_COMMA.next());
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, stream_insert_item.next());

                }
                stream_insert_item.reset();
                stream_COMMA.reset();
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end insert_item_comma_list

    public static class insert_item_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start insert_item
    // Cubrid.g:519:1: insert_item : expression -> expression ;
    public final insert_item_return insert_item() throws RecognitionException {
        insert_item_return retval = new insert_item_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        expression_return expression246 = null;


        RewriteRuleSubtreeStream stream_expression=new RewriteRuleSubtreeStream(adaptor,"rule expression");
        try {
            // Cubrid.g:519:12: ( expression -> expression )
            // Cubrid.g:520:2: expression
            {
            pushFollow(FOLLOW_expression_in_insert_item3168);
            expression246=expression();
            _fsp--;

            stream_expression.add(expression246.getTree());

            // AST REWRITE
            // elements: expression
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 520:13: -> expression
            {
                adaptor.addChild(root_0, stream_expression.next());

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end insert_item

    public static class update_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start update
    // Cubrid.g:524:1: update : UPDATE class_all_spec SET assignment_comma_list where_clause -> UPDATE class_all_spec ENTER SET ENTER TAB assignment_comma_list UNTAB where_clause ;
    public final update_return update() throws RecognitionException {
        update_return retval = new update_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token UPDATE247=null;
        Token SET249=null;
        class_all_spec_return class_all_spec248 = null;

        assignment_comma_list_return assignment_comma_list250 = null;

        where_clause_return where_clause251 = null;


        Object UPDATE247_tree=null;
        Object SET249_tree=null;
        RewriteRuleTokenStream stream_UPDATE=new RewriteRuleTokenStream(adaptor,"token UPDATE");
        RewriteRuleTokenStream stream_SET=new RewriteRuleTokenStream(adaptor,"token SET");
        RewriteRuleSubtreeStream stream_where_clause=new RewriteRuleSubtreeStream(adaptor,"rule where_clause");
        RewriteRuleSubtreeStream stream_assignment_comma_list=new RewriteRuleSubtreeStream(adaptor,"rule assignment_comma_list");
        RewriteRuleSubtreeStream stream_class_all_spec=new RewriteRuleSubtreeStream(adaptor,"rule class_all_spec");
        try {
            // Cubrid.g:524:8: ( UPDATE class_all_spec SET assignment_comma_list where_clause -> UPDATE class_all_spec ENTER SET ENTER TAB assignment_comma_list UNTAB where_clause )
            // Cubrid.g:525:2: UPDATE class_all_spec SET assignment_comma_list where_clause
            {
            UPDATE247=(Token)input.LT(1);
            match(input,UPDATE,FOLLOW_UPDATE_in_update3186); 
            stream_UPDATE.add(UPDATE247);

            pushFollow(FOLLOW_class_all_spec_in_update3190);
            class_all_spec248=class_all_spec();
            _fsp--;

            stream_class_all_spec.add(class_all_spec248.getTree());
            SET249=(Token)input.LT(1);
            match(input,SET,FOLLOW_SET_in_update3193); 
            stream_SET.add(SET249);

            pushFollow(FOLLOW_assignment_comma_list_in_update3195);
            assignment_comma_list250=assignment_comma_list();
            _fsp--;

            stream_assignment_comma_list.add(assignment_comma_list250.getTree());
            pushFollow(FOLLOW_where_clause_in_update3198);
            where_clause251=where_clause();
            _fsp--;

            stream_where_clause.add(where_clause251.getTree());

            // AST REWRITE
            // elements: SET, assignment_comma_list, class_all_spec, UPDATE, where_clause
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 529:2: -> UPDATE class_all_spec ENTER SET ENTER TAB assignment_comma_list UNTAB where_clause
            {
                adaptor.addChild(root_0, stream_UPDATE.next());
                adaptor.addChild(root_0, stream_class_all_spec.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, stream_SET.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));
                adaptor.addChild(root_0, stream_assignment_comma_list.next());
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));
                adaptor.addChild(root_0, stream_where_clause.next());

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end update

    public static class class_all_spec_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start class_all_spec
    // Cubrid.g:536:1: class_all_spec : ( class_specification | metaclass_specification );
    public final class_all_spec_return class_all_spec() throws RecognitionException {
        class_all_spec_return retval = new class_all_spec_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        class_specification_return class_specification252 = null;

        metaclass_specification_return metaclass_specification253 = null;



        try {
            // Cubrid.g:536:15: ( class_specification | metaclass_specification )
            int alt70=2;
            int LA70_0 = input.LA(1);

            if ( (LA70_0==ALL||LA70_0==ONLY||(LA70_0>=ID && LA70_0<=STARTBRACKET)) ) {
                alt70=1;
            }
            else if ( (LA70_0==CLASS) ) {
                alt70=2;
            }
            else {
                NoViableAltException nvae =
                    new NoViableAltException("536:1: class_all_spec : ( class_specification | metaclass_specification );", 70, 0, input);

                throw nvae;
            }
            switch (alt70) {
                case 1 :
                    // Cubrid.g:537:2: class_specification
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_class_specification_in_class_all_spec3237);
                    class_specification252=class_specification();
                    _fsp--;

                    adaptor.addChild(root_0, class_specification252.getTree());

                    }
                    break;
                case 2 :
                    // Cubrid.g:538:4: metaclass_specification
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_metaclass_specification_in_class_all_spec3243);
                    metaclass_specification253=metaclass_specification();
                    _fsp--;

                    adaptor.addChild(root_0, metaclass_specification253.getTree());

                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end class_all_spec

    public static class assignment_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start assignment_comma_list
    // Cubrid.g:541:1: assignment_comma_list : assignment ( COMMA assignment )* -> assignment ( COMMA ENTER assignment )* ENTER ;
    public final assignment_comma_list_return assignment_comma_list() throws RecognitionException {
        assignment_comma_list_return retval = new assignment_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA255=null;
        assignment_return assignment254 = null;

        assignment_return assignment256 = null;


        Object COMMA255_tree=null;
        RewriteRuleTokenStream stream_COMMA=new RewriteRuleTokenStream(adaptor,"token COMMA");
        RewriteRuleSubtreeStream stream_assignment=new RewriteRuleSubtreeStream(adaptor,"rule assignment");
        try {
            // Cubrid.g:541:22: ( assignment ( COMMA assignment )* -> assignment ( COMMA ENTER assignment )* ENTER )
            // Cubrid.g:542:2: assignment ( COMMA assignment )*
            {
            pushFollow(FOLLOW_assignment_in_assignment_comma_list3254);
            assignment254=assignment();
            _fsp--;

            stream_assignment.add(assignment254.getTree());
            // Cubrid.g:542:13: ( COMMA assignment )*
            loop71:
            do {
                int alt71=2;
                int LA71_0 = input.LA(1);

                if ( (LA71_0==COMMA) ) {
                    alt71=1;
                }


                switch (alt71) {
            	case 1 :
            	    // Cubrid.g:542:14: COMMA assignment
            	    {
            	    COMMA255=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_assignment_comma_list3257); 
            	    stream_COMMA.add(COMMA255);

            	    pushFollow(FOLLOW_assignment_in_assignment_comma_list3259);
            	    assignment256=assignment();
            	    _fsp--;

            	    stream_assignment.add(assignment256.getTree());

            	    }
            	    break;

            	default :
            	    break loop71;
                }
            } while (true);


            // AST REWRITE
            // elements: assignment, assignment, COMMA
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 543:2: -> assignment ( COMMA ENTER assignment )* ENTER
            {
                adaptor.addChild(root_0, stream_assignment.next());
                // Cubrid.g:543:16: ( COMMA ENTER assignment )*
                while ( stream_assignment.hasNext()||stream_COMMA.hasNext() ) {
                    adaptor.addChild(root_0, stream_COMMA.next());
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, stream_assignment.next());

                }
                stream_assignment.reset();
                stream_COMMA.reset();
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end assignment_comma_list

    public static class assignment_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start assignment
    // Cubrid.g:546:1: assignment : attribute_name EQUAL expression ;
    public final assignment_return assignment() throws RecognitionException {
        assignment_return retval = new assignment_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token EQUAL258=null;
        attribute_name_return attribute_name257 = null;

        expression_return expression259 = null;


        Object EQUAL258_tree=null;

        try {
            // Cubrid.g:546:11: ( attribute_name EQUAL expression )
            // Cubrid.g:547:2: attribute_name EQUAL expression
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_attribute_name_in_assignment3289);
            attribute_name257=attribute_name();
            _fsp--;

            adaptor.addChild(root_0, attribute_name257.getTree());
            EQUAL258=(Token)input.LT(1);
            match(input,EQUAL,FOLLOW_EQUAL_in_assignment3291); 
            EQUAL258_tree = (Object)adaptor.create(EQUAL258);
            adaptor.addChild(root_0, EQUAL258_tree);

            pushFollow(FOLLOW_expression_in_assignment3293);
            expression259=expression();
            _fsp--;

            adaptor.addChild(root_0, expression259.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end assignment

    public static class delete_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start delete
    // Cubrid.g:552:1: delete : DELETE FROM class_specification where_clause -> DELETE FROM ENTER TAB class_specification ENTER UNTAB where_clause ;
    public final delete_return delete() throws RecognitionException {
        delete_return retval = new delete_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token DELETE260=null;
        Token FROM261=null;
        class_specification_return class_specification262 = null;

        where_clause_return where_clause263 = null;


        Object DELETE260_tree=null;
        Object FROM261_tree=null;
        RewriteRuleTokenStream stream_DELETE=new RewriteRuleTokenStream(adaptor,"token DELETE");
        RewriteRuleTokenStream stream_FROM=new RewriteRuleTokenStream(adaptor,"token FROM");
        RewriteRuleSubtreeStream stream_where_clause=new RewriteRuleSubtreeStream(adaptor,"rule where_clause");
        RewriteRuleSubtreeStream stream_class_specification=new RewriteRuleSubtreeStream(adaptor,"rule class_specification");
        try {
            // Cubrid.g:552:7: ( DELETE FROM class_specification where_clause -> DELETE FROM ENTER TAB class_specification ENTER UNTAB where_clause )
            // Cubrid.g:553:2: DELETE FROM class_specification where_clause
            {
            DELETE260=(Token)input.LT(1);
            match(input,DELETE,FOLLOW_DELETE_in_delete3307); 
            stream_DELETE.add(DELETE260);

            FROM261=(Token)input.LT(1);
            match(input,FROM,FOLLOW_FROM_in_delete3309); 
            stream_FROM.add(FROM261);

            pushFollow(FOLLOW_class_specification_in_delete3311);
            class_specification262=class_specification();
            _fsp--;

            stream_class_specification.add(class_specification262.getTree());
            pushFollow(FOLLOW_where_clause_in_delete3314);
            where_clause263=where_clause();
            _fsp--;

            stream_where_clause.add(where_clause263.getTree());

            // AST REWRITE
            // elements: class_specification, DELETE, where_clause, FROM
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 555:2: -> DELETE FROM ENTER TAB class_specification ENTER UNTAB where_clause
            {
                adaptor.addChild(root_0, stream_DELETE.next());
                adaptor.addChild(root_0, stream_FROM.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));
                adaptor.addChild(root_0, stream_class_specification.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));
                adaptor.addChild(root_0, stream_where_clause.next());

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end delete

    public static class create_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start create
    // Cubrid.g:562:1: create : CREATE class_or_table class_name ( subclass_definition )? ( class_element_definition_part )? ( CLASS ATTRIBUTE attribute_definition_comma_list )? ( METHOD method_definition_comma_list )? ( FILE method_file_comma_list )? ( INHERIT resolution_comma_list )? ;
    public final create_return create() throws RecognitionException {
        create_return retval = new create_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token CREATE264=null;
        Token CLASS269=null;
        Token ATTRIBUTE270=null;
        Token METHOD272=null;
        Token FILE274=null;
        Token INHERIT276=null;
        class_or_table_return class_or_table265 = null;

        class_name_return class_name266 = null;

        subclass_definition_return subclass_definition267 = null;

        class_element_definition_part_return class_element_definition_part268 = null;

        attribute_definition_comma_list_return attribute_definition_comma_list271 = null;

        method_definition_comma_list_return method_definition_comma_list273 = null;

        method_file_comma_list_return method_file_comma_list275 = null;

        resolution_comma_list_return resolution_comma_list277 = null;


        Object CREATE264_tree=null;
        Object CLASS269_tree=null;
        Object ATTRIBUTE270_tree=null;
        Object METHOD272_tree=null;
        Object FILE274_tree=null;
        Object INHERIT276_tree=null;

        try {
            // Cubrid.g:562:7: ( CREATE class_or_table class_name ( subclass_definition )? ( class_element_definition_part )? ( CLASS ATTRIBUTE attribute_definition_comma_list )? ( METHOD method_definition_comma_list )? ( FILE method_file_comma_list )? ( INHERIT resolution_comma_list )? )
            // Cubrid.g:563:2: CREATE class_or_table class_name ( subclass_definition )? ( class_element_definition_part )? ( CLASS ATTRIBUTE attribute_definition_comma_list )? ( METHOD method_definition_comma_list )? ( FILE method_file_comma_list )? ( INHERIT resolution_comma_list )?
            {
            root_0 = (Object)adaptor.nil();

            CREATE264=(Token)input.LT(1);
            match(input,CREATE,FOLLOW_CREATE_in_create3348); 
            CREATE264_tree = (Object)adaptor.create(CREATE264);
            adaptor.addChild(root_0, CREATE264_tree);

            pushFollow(FOLLOW_class_or_table_in_create3350);
            class_or_table265=class_or_table();
            _fsp--;

            adaptor.addChild(root_0, class_or_table265.getTree());
            pushFollow(FOLLOW_class_name_in_create3352);
            class_name266=class_name();
            _fsp--;

            adaptor.addChild(root_0, class_name266.getTree());
            // Cubrid.g:564:2: ( subclass_definition )?
            int alt72=2;
            int LA72_0 = input.LA(1);

            if ( (LA72_0==AS) ) {
                alt72=1;
            }
            switch (alt72) {
                case 1 :
                    // Cubrid.g:564:2: subclass_definition
                    {
                    pushFollow(FOLLOW_subclass_definition_in_create3355);
                    subclass_definition267=subclass_definition();
                    _fsp--;

                    adaptor.addChild(root_0, subclass_definition267.getTree());

                    }
                    break;

            }

            // Cubrid.g:565:2: ( class_element_definition_part )?
            int alt73=2;
            int LA73_0 = input.LA(1);

            if ( (LA73_0==STARTBRACKET) ) {
                alt73=1;
            }
            switch (alt73) {
                case 1 :
                    // Cubrid.g:565:2: class_element_definition_part
                    {
                    pushFollow(FOLLOW_class_element_definition_part_in_create3359);
                    class_element_definition_part268=class_element_definition_part();
                    _fsp--;

                    adaptor.addChild(root_0, class_element_definition_part268.getTree());

                    }
                    break;

            }

            // Cubrid.g:566:2: ( CLASS ATTRIBUTE attribute_definition_comma_list )?
            int alt74=2;
            int LA74_0 = input.LA(1);

            if ( (LA74_0==CLASS) ) {
                alt74=1;
            }
            switch (alt74) {
                case 1 :
                    // Cubrid.g:566:3: CLASS ATTRIBUTE attribute_definition_comma_list
                    {
                    CLASS269=(Token)input.LT(1);
                    match(input,CLASS,FOLLOW_CLASS_in_create3364); 
                    CLASS269_tree = (Object)adaptor.create(CLASS269);
                    adaptor.addChild(root_0, CLASS269_tree);

                    ATTRIBUTE270=(Token)input.LT(1);
                    match(input,ATTRIBUTE,FOLLOW_ATTRIBUTE_in_create3366); 
                    ATTRIBUTE270_tree = (Object)adaptor.create(ATTRIBUTE270);
                    adaptor.addChild(root_0, ATTRIBUTE270_tree);

                    pushFollow(FOLLOW_attribute_definition_comma_list_in_create3369);
                    attribute_definition_comma_list271=attribute_definition_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, attribute_definition_comma_list271.getTree());

                    }
                    break;

            }

            // Cubrid.g:567:2: ( METHOD method_definition_comma_list )?
            int alt75=2;
            int LA75_0 = input.LA(1);

            if ( (LA75_0==METHOD) ) {
                alt75=1;
            }
            switch (alt75) {
                case 1 :
                    // Cubrid.g:567:3: METHOD method_definition_comma_list
                    {
                    METHOD272=(Token)input.LT(1);
                    match(input,METHOD,FOLLOW_METHOD_in_create3375); 
                    METHOD272_tree = (Object)adaptor.create(METHOD272);
                    adaptor.addChild(root_0, METHOD272_tree);

                    pushFollow(FOLLOW_method_definition_comma_list_in_create3377);
                    method_definition_comma_list273=method_definition_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, method_definition_comma_list273.getTree());

                    }
                    break;

            }

            // Cubrid.g:568:2: ( FILE method_file_comma_list )?
            int alt76=2;
            int LA76_0 = input.LA(1);

            if ( (LA76_0==FILE) ) {
                alt76=1;
            }
            switch (alt76) {
                case 1 :
                    // Cubrid.g:568:3: FILE method_file_comma_list
                    {
                    FILE274=(Token)input.LT(1);
                    match(input,FILE,FOLLOW_FILE_in_create3383); 
                    FILE274_tree = (Object)adaptor.create(FILE274);
                    adaptor.addChild(root_0, FILE274_tree);

                    pushFollow(FOLLOW_method_file_comma_list_in_create3385);
                    method_file_comma_list275=method_file_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, method_file_comma_list275.getTree());

                    }
                    break;

            }

            // Cubrid.g:569:2: ( INHERIT resolution_comma_list )?
            int alt77=2;
            int LA77_0 = input.LA(1);

            if ( (LA77_0==INHERIT) ) {
                alt77=1;
            }
            switch (alt77) {
                case 1 :
                    // Cubrid.g:569:3: INHERIT resolution_comma_list
                    {
                    INHERIT276=(Token)input.LT(1);
                    match(input,INHERIT,FOLLOW_INHERIT_in_create3391); 
                    INHERIT276_tree = (Object)adaptor.create(INHERIT276);
                    adaptor.addChild(root_0, INHERIT276_tree);

                    pushFollow(FOLLOW_resolution_comma_list_in_create3393);
                    resolution_comma_list277=resolution_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, resolution_comma_list277.getTree());

                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end create

    public static class create_virtual_class_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start create_virtual_class
    // Cubrid.g:572:1: create_virtual_class : CREATE vclass_or_view class_name ( subclass_definition )? ( view_attribute_definition_part )? ( CLASS ATTRIBUTE attribute_definition_comma_list )? ( METHOD method_definition_comma_list )? ( FILE method_file_comma_list )? ( INHERIT resolution_comma_list )? ( AS query_statement )? ( WITH CHECK OPTION )? ;
    public final create_virtual_class_return create_virtual_class() throws RecognitionException {
        create_virtual_class_return retval = new create_virtual_class_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token CREATE278=null;
        Token CLASS283=null;
        Token ATTRIBUTE284=null;
        Token METHOD286=null;
        Token FILE288=null;
        Token INHERIT290=null;
        Token AS292=null;
        Token WITH294=null;
        Token CHECK295=null;
        Token OPTION296=null;
        vclass_or_view_return vclass_or_view279 = null;

        class_name_return class_name280 = null;

        subclass_definition_return subclass_definition281 = null;

        view_attribute_definition_part_return view_attribute_definition_part282 = null;

        attribute_definition_comma_list_return attribute_definition_comma_list285 = null;

        method_definition_comma_list_return method_definition_comma_list287 = null;

        method_file_comma_list_return method_file_comma_list289 = null;

        resolution_comma_list_return resolution_comma_list291 = null;

        query_statement_return query_statement293 = null;


        Object CREATE278_tree=null;
        Object CLASS283_tree=null;
        Object ATTRIBUTE284_tree=null;
        Object METHOD286_tree=null;
        Object FILE288_tree=null;
        Object INHERIT290_tree=null;
        Object AS292_tree=null;
        Object WITH294_tree=null;
        Object CHECK295_tree=null;
        Object OPTION296_tree=null;

        try {
            // Cubrid.g:572:21: ( CREATE vclass_or_view class_name ( subclass_definition )? ( view_attribute_definition_part )? ( CLASS ATTRIBUTE attribute_definition_comma_list )? ( METHOD method_definition_comma_list )? ( FILE method_file_comma_list )? ( INHERIT resolution_comma_list )? ( AS query_statement )? ( WITH CHECK OPTION )? )
            // Cubrid.g:573:2: CREATE vclass_or_view class_name ( subclass_definition )? ( view_attribute_definition_part )? ( CLASS ATTRIBUTE attribute_definition_comma_list )? ( METHOD method_definition_comma_list )? ( FILE method_file_comma_list )? ( INHERIT resolution_comma_list )? ( AS query_statement )? ( WITH CHECK OPTION )?
            {
            root_0 = (Object)adaptor.nil();

            CREATE278=(Token)input.LT(1);
            match(input,CREATE,FOLLOW_CREATE_in_create_virtual_class3406); 
            CREATE278_tree = (Object)adaptor.create(CREATE278);
            adaptor.addChild(root_0, CREATE278_tree);

            pushFollow(FOLLOW_vclass_or_view_in_create_virtual_class3408);
            vclass_or_view279=vclass_or_view();
            _fsp--;

            adaptor.addChild(root_0, vclass_or_view279.getTree());
            pushFollow(FOLLOW_class_name_in_create_virtual_class3410);
            class_name280=class_name();
            _fsp--;

            adaptor.addChild(root_0, class_name280.getTree());
            // Cubrid.g:574:2: ( subclass_definition )?
            int alt78=2;
            int LA78_0 = input.LA(1);

            if ( (LA78_0==AS) ) {
                int LA78_1 = input.LA(2);

                if ( (LA78_1==SUBCLASS) ) {
                    alt78=1;
                }
            }
            switch (alt78) {
                case 1 :
                    // Cubrid.g:574:2: subclass_definition
                    {
                    pushFollow(FOLLOW_subclass_definition_in_create_virtual_class3414);
                    subclass_definition281=subclass_definition();
                    _fsp--;

                    adaptor.addChild(root_0, subclass_definition281.getTree());

                    }
                    break;

            }

            // Cubrid.g:575:2: ( view_attribute_definition_part )?
            int alt79=2;
            int LA79_0 = input.LA(1);

            if ( (LA79_0==STARTBRACKET) ) {
                alt79=1;
            }
            switch (alt79) {
                case 1 :
                    // Cubrid.g:575:2: view_attribute_definition_part
                    {
                    pushFollow(FOLLOW_view_attribute_definition_part_in_create_virtual_class3418);
                    view_attribute_definition_part282=view_attribute_definition_part();
                    _fsp--;

                    adaptor.addChild(root_0, view_attribute_definition_part282.getTree());

                    }
                    break;

            }

            // Cubrid.g:576:2: ( CLASS ATTRIBUTE attribute_definition_comma_list )?
            int alt80=2;
            int LA80_0 = input.LA(1);

            if ( (LA80_0==CLASS) ) {
                alt80=1;
            }
            switch (alt80) {
                case 1 :
                    // Cubrid.g:576:3: CLASS ATTRIBUTE attribute_definition_comma_list
                    {
                    CLASS283=(Token)input.LT(1);
                    match(input,CLASS,FOLLOW_CLASS_in_create_virtual_class3423); 
                    CLASS283_tree = (Object)adaptor.create(CLASS283);
                    adaptor.addChild(root_0, CLASS283_tree);

                    ATTRIBUTE284=(Token)input.LT(1);
                    match(input,ATTRIBUTE,FOLLOW_ATTRIBUTE_in_create_virtual_class3425); 
                    ATTRIBUTE284_tree = (Object)adaptor.create(ATTRIBUTE284);
                    adaptor.addChild(root_0, ATTRIBUTE284_tree);

                    pushFollow(FOLLOW_attribute_definition_comma_list_in_create_virtual_class3428);
                    attribute_definition_comma_list285=attribute_definition_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, attribute_definition_comma_list285.getTree());

                    }
                    break;

            }

            // Cubrid.g:577:2: ( METHOD method_definition_comma_list )?
            int alt81=2;
            int LA81_0 = input.LA(1);

            if ( (LA81_0==METHOD) ) {
                alt81=1;
            }
            switch (alt81) {
                case 1 :
                    // Cubrid.g:577:3: METHOD method_definition_comma_list
                    {
                    METHOD286=(Token)input.LT(1);
                    match(input,METHOD,FOLLOW_METHOD_in_create_virtual_class3434); 
                    METHOD286_tree = (Object)adaptor.create(METHOD286);
                    adaptor.addChild(root_0, METHOD286_tree);

                    pushFollow(FOLLOW_method_definition_comma_list_in_create_virtual_class3436);
                    method_definition_comma_list287=method_definition_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, method_definition_comma_list287.getTree());

                    }
                    break;

            }

            // Cubrid.g:578:2: ( FILE method_file_comma_list )?
            int alt82=2;
            int LA82_0 = input.LA(1);

            if ( (LA82_0==FILE) ) {
                alt82=1;
            }
            switch (alt82) {
                case 1 :
                    // Cubrid.g:578:3: FILE method_file_comma_list
                    {
                    FILE288=(Token)input.LT(1);
                    match(input,FILE,FOLLOW_FILE_in_create_virtual_class3442); 
                    FILE288_tree = (Object)adaptor.create(FILE288);
                    adaptor.addChild(root_0, FILE288_tree);

                    pushFollow(FOLLOW_method_file_comma_list_in_create_virtual_class3444);
                    method_file_comma_list289=method_file_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, method_file_comma_list289.getTree());

                    }
                    break;

            }

            // Cubrid.g:579:2: ( INHERIT resolution_comma_list )?
            int alt83=2;
            int LA83_0 = input.LA(1);

            if ( (LA83_0==INHERIT) ) {
                alt83=1;
            }
            switch (alt83) {
                case 1 :
                    // Cubrid.g:579:3: INHERIT resolution_comma_list
                    {
                    INHERIT290=(Token)input.LT(1);
                    match(input,INHERIT,FOLLOW_INHERIT_in_create_virtual_class3450); 
                    INHERIT290_tree = (Object)adaptor.create(INHERIT290);
                    adaptor.addChild(root_0, INHERIT290_tree);

                    pushFollow(FOLLOW_resolution_comma_list_in_create_virtual_class3452);
                    resolution_comma_list291=resolution_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, resolution_comma_list291.getTree());

                    }
                    break;

            }

            // Cubrid.g:580:2: ( AS query_statement )?
            int alt84=2;
            int LA84_0 = input.LA(1);

            if ( (LA84_0==AS) ) {
                alt84=1;
            }
            switch (alt84) {
                case 1 :
                    // Cubrid.g:580:3: AS query_statement
                    {
                    AS292=(Token)input.LT(1);
                    match(input,AS,FOLLOW_AS_in_create_virtual_class3458); 
                    AS292_tree = (Object)adaptor.create(AS292);
                    adaptor.addChild(root_0, AS292_tree);

                    pushFollow(FOLLOW_query_statement_in_create_virtual_class3460);
                    query_statement293=query_statement();
                    _fsp--;

                    adaptor.addChild(root_0, query_statement293.getTree());

                    }
                    break;

            }

            // Cubrid.g:581:2: ( WITH CHECK OPTION )?
            int alt85=2;
            int LA85_0 = input.LA(1);

            if ( (LA85_0==WITH) ) {
                alt85=1;
            }
            switch (alt85) {
                case 1 :
                    // Cubrid.g:581:3: WITH CHECK OPTION
                    {
                    WITH294=(Token)input.LT(1);
                    match(input,WITH,FOLLOW_WITH_in_create_virtual_class3466); 
                    WITH294_tree = (Object)adaptor.create(WITH294);
                    adaptor.addChild(root_0, WITH294_tree);

                    CHECK295=(Token)input.LT(1);
                    match(input,CHECK,FOLLOW_CHECK_in_create_virtual_class3468); 
                    CHECK295_tree = (Object)adaptor.create(CHECK295);
                    adaptor.addChild(root_0, CHECK295_tree);

                    OPTION296=(Token)input.LT(1);
                    match(input,OPTION,FOLLOW_OPTION_in_create_virtual_class3470); 
                    OPTION296_tree = (Object)adaptor.create(OPTION296);
                    adaptor.addChild(root_0, OPTION296_tree);


                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end create_virtual_class

    public static class class_or_table_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start class_or_table
    // Cubrid.g:584:1: class_or_table : ( CLASS | TABLE );
    public final class_or_table_return class_or_table() throws RecognitionException {
        class_or_table_return retval = new class_or_table_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set297=null;

        Object set297_tree=null;

        try {
            // Cubrid.g:584:15: ( CLASS | TABLE )
            // Cubrid.g:
            {
            root_0 = (Object)adaptor.nil();

            set297=(Token)input.LT(1);
            if ( input.LA(1)==CLASS||input.LA(1)==TABLE ) {
                input.consume();
                adaptor.addChild(root_0, adaptor.create(set297));
                errorRecovery=false;
            }
            else {
                MismatchedSetException mse =
                    new MismatchedSetException(null,input);
                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_class_or_table0);    throw mse;
            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end class_or_table

    public static class vclass_or_view_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start vclass_or_view
    // Cubrid.g:589:1: vclass_or_view : ( VCLASS | VIEW );
    public final vclass_or_view_return vclass_or_view() throws RecognitionException {
        vclass_or_view_return retval = new vclass_or_view_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set298=null;

        Object set298_tree=null;

        try {
            // Cubrid.g:589:15: ( VCLASS | VIEW )
            // Cubrid.g:
            {
            root_0 = (Object)adaptor.nil();

            set298=(Token)input.LT(1);
            if ( (input.LA(1)>=VCLASS && input.LA(1)<=VIEW) ) {
                input.consume();
                adaptor.addChild(root_0, adaptor.create(set298));
                errorRecovery=false;
            }
            else {
                MismatchedSetException mse =
                    new MismatchedSetException(null,input);
                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_vclass_or_view0);    throw mse;
            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end vclass_or_view

    public static class subclass_definition_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start subclass_definition
    // Cubrid.g:594:1: subclass_definition : AS SUBCLASS OF class_name_comma_list ;
    public final subclass_definition_return subclass_definition() throws RecognitionException {
        subclass_definition_return retval = new subclass_definition_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token AS299=null;
        Token SUBCLASS300=null;
        Token OF301=null;
        class_name_comma_list_return class_name_comma_list302 = null;


        Object AS299_tree=null;
        Object SUBCLASS300_tree=null;
        Object OF301_tree=null;

        try {
            // Cubrid.g:594:20: ( AS SUBCLASS OF class_name_comma_list )
            // Cubrid.g:595:2: AS SUBCLASS OF class_name_comma_list
            {
            root_0 = (Object)adaptor.nil();

            AS299=(Token)input.LT(1);
            match(input,AS,FOLLOW_AS_in_subclass_definition3514); 
            AS299_tree = (Object)adaptor.create(AS299);
            adaptor.addChild(root_0, AS299_tree);

            SUBCLASS300=(Token)input.LT(1);
            match(input,SUBCLASS,FOLLOW_SUBCLASS_in_subclass_definition3516); 
            SUBCLASS300_tree = (Object)adaptor.create(SUBCLASS300);
            adaptor.addChild(root_0, SUBCLASS300_tree);

            OF301=(Token)input.LT(1);
            match(input,OF,FOLLOW_OF_in_subclass_definition3518); 
            OF301_tree = (Object)adaptor.create(OF301);
            adaptor.addChild(root_0, OF301_tree);

            pushFollow(FOLLOW_class_name_comma_list_in_subclass_definition3520);
            class_name_comma_list302=class_name_comma_list();
            _fsp--;

            adaptor.addChild(root_0, class_name_comma_list302.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end subclass_definition

    public static class class_name_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start class_name_comma_list
    // Cubrid.g:598:1: class_name_comma_list : class_name ( COMMA class_name )* -> ENTER TAB class_name ( COMMA ENTER class_name )* ENTER UNTAB ;
    public final class_name_comma_list_return class_name_comma_list() throws RecognitionException {
        class_name_comma_list_return retval = new class_name_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA304=null;
        class_name_return class_name303 = null;

        class_name_return class_name305 = null;


        Object COMMA304_tree=null;
        RewriteRuleTokenStream stream_COMMA=new RewriteRuleTokenStream(adaptor,"token COMMA");
        RewriteRuleSubtreeStream stream_class_name=new RewriteRuleSubtreeStream(adaptor,"rule class_name");
        try {
            // Cubrid.g:598:22: ( class_name ( COMMA class_name )* -> ENTER TAB class_name ( COMMA ENTER class_name )* ENTER UNTAB )
            // Cubrid.g:599:2: class_name ( COMMA class_name )*
            {
            pushFollow(FOLLOW_class_name_in_class_name_comma_list3530);
            class_name303=class_name();
            _fsp--;

            stream_class_name.add(class_name303.getTree());
            // Cubrid.g:599:13: ( COMMA class_name )*
            loop86:
            do {
                int alt86=2;
                int LA86_0 = input.LA(1);

                if ( (LA86_0==COMMA) ) {
                    alt86=1;
                }


                switch (alt86) {
            	case 1 :
            	    // Cubrid.g:599:14: COMMA class_name
            	    {
            	    COMMA304=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_class_name_comma_list3533); 
            	    stream_COMMA.add(COMMA304);

            	    pushFollow(FOLLOW_class_name_in_class_name_comma_list3535);
            	    class_name305=class_name();
            	    _fsp--;

            	    stream_class_name.add(class_name305.getTree());

            	    }
            	    break;

            	default :
            	    break loop86;
                }
            } while (true);


            // AST REWRITE
            // elements: class_name, COMMA, class_name
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 600:2: -> ENTER TAB class_name ( COMMA ENTER class_name )* ENTER UNTAB
            {
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));
                adaptor.addChild(root_0, stream_class_name.next());
                // Cubrid.g:600:25: ( COMMA ENTER class_name )*
                while ( stream_class_name.hasNext()||stream_COMMA.hasNext() ) {
                    adaptor.addChild(root_0, stream_COMMA.next());
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, stream_class_name.next());

                }
                stream_class_name.reset();
                stream_COMMA.reset();
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end class_name_comma_list

    public static class class_element_definition_part_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start class_element_definition_part
    // Cubrid.g:603:1: class_element_definition_part : STARTBRACKET class_element_comma_list ENDBRACKET -> STARTBRACKET class_element_comma_list ENDBRACKET ENTER ;
    public final class_element_definition_part_return class_element_definition_part() throws RecognitionException {
        class_element_definition_part_return retval = new class_element_definition_part_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token STARTBRACKET306=null;
        Token ENDBRACKET308=null;
        class_element_comma_list_return class_element_comma_list307 = null;


        Object STARTBRACKET306_tree=null;
        Object ENDBRACKET308_tree=null;
        RewriteRuleTokenStream stream_ENDBRACKET=new RewriteRuleTokenStream(adaptor,"token ENDBRACKET");
        RewriteRuleTokenStream stream_STARTBRACKET=new RewriteRuleTokenStream(adaptor,"token STARTBRACKET");
        RewriteRuleSubtreeStream stream_class_element_comma_list=new RewriteRuleSubtreeStream(adaptor,"rule class_element_comma_list");
        try {
            // Cubrid.g:603:30: ( STARTBRACKET class_element_comma_list ENDBRACKET -> STARTBRACKET class_element_comma_list ENDBRACKET ENTER )
            // Cubrid.g:604:2: STARTBRACKET class_element_comma_list ENDBRACKET
            {
            STARTBRACKET306=(Token)input.LT(1);
            match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_class_element_definition_part3569); 
            stream_STARTBRACKET.add(STARTBRACKET306);

            pushFollow(FOLLOW_class_element_comma_list_in_class_element_definition_part3573);
            class_element_comma_list307=class_element_comma_list();
            _fsp--;

            stream_class_element_comma_list.add(class_element_comma_list307.getTree());
            ENDBRACKET308=(Token)input.LT(1);
            match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_class_element_definition_part3577); 
            stream_ENDBRACKET.add(ENDBRACKET308);


            // AST REWRITE
            // elements: class_element_comma_list, STARTBRACKET, ENDBRACKET
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 607:2: -> STARTBRACKET class_element_comma_list ENDBRACKET ENTER
            {
                adaptor.addChild(root_0, stream_STARTBRACKET.next());
                adaptor.addChild(root_0, stream_class_element_comma_list.next());
                adaptor.addChild(root_0, stream_ENDBRACKET.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end class_element_definition_part

    public static class class_element_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start class_element_comma_list
    // Cubrid.g:614:1: class_element_comma_list : class_element ( COMMA class_element )* -> ENTER TAB class_element ( COMMA ENTER class_element )* ENTER UNTAB ;
    public final class_element_comma_list_return class_element_comma_list() throws RecognitionException {
        class_element_comma_list_return retval = new class_element_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA310=null;
        class_element_return class_element309 = null;

        class_element_return class_element311 = null;


        Object COMMA310_tree=null;
        RewriteRuleTokenStream stream_COMMA=new RewriteRuleTokenStream(adaptor,"token COMMA");
        RewriteRuleSubtreeStream stream_class_element=new RewriteRuleSubtreeStream(adaptor,"rule class_element");
        try {
            // Cubrid.g:614:25: ( class_element ( COMMA class_element )* -> ENTER TAB class_element ( COMMA ENTER class_element )* ENTER UNTAB )
            // Cubrid.g:615:2: class_element ( COMMA class_element )*
            {
            pushFollow(FOLLOW_class_element_in_class_element_comma_list3604);
            class_element309=class_element();
            _fsp--;

            stream_class_element.add(class_element309.getTree());
            // Cubrid.g:615:16: ( COMMA class_element )*
            loop87:
            do {
                int alt87=2;
                int LA87_0 = input.LA(1);

                if ( (LA87_0==COMMA) ) {
                    alt87=1;
                }


                switch (alt87) {
            	case 1 :
            	    // Cubrid.g:615:17: COMMA class_element
            	    {
            	    COMMA310=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_class_element_comma_list3607); 
            	    stream_COMMA.add(COMMA310);

            	    pushFollow(FOLLOW_class_element_in_class_element_comma_list3609);
            	    class_element311=class_element();
            	    _fsp--;

            	    stream_class_element.add(class_element311.getTree());

            	    }
            	    break;

            	default :
            	    break loop87;
                }
            } while (true);


            // AST REWRITE
            // elements: class_element, COMMA, class_element
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 616:2: -> ENTER TAB class_element ( COMMA ENTER class_element )* ENTER UNTAB
            {
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));
                adaptor.addChild(root_0, stream_class_element.next());
                // Cubrid.g:617:26: ( COMMA ENTER class_element )*
                while ( stream_COMMA.hasNext()||stream_class_element.hasNext() ) {
                    adaptor.addChild(root_0, stream_COMMA.next());
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, stream_class_element.next());

                }
                stream_COMMA.reset();
                stream_class_element.reset();
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end class_element_comma_list

    public static class class_element_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start class_element
    // Cubrid.g:620:1: class_element : ( attribute_definition | class_constraint );
    public final class_element_return class_element() throws RecognitionException {
        class_element_return retval = new class_element_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        attribute_definition_return attribute_definition312 = null;

        class_constraint_return class_constraint313 = null;



        try {
            // Cubrid.g:620:14: ( attribute_definition | class_constraint )
            int alt88=2;
            int LA88_0 = input.LA(1);

            if ( (LA88_0==CLASS||(LA88_0>=ID && LA88_0<=COLUMN)) ) {
                alt88=1;
            }
            else if ( (LA88_0==CONSTRAINT||LA88_0==FOREIGN||LA88_0==INHERIT||LA88_0==PRIMARY||LA88_0==UNIQUE||(LA88_0>=END && LA88_0<=COMMA)||LA88_0==ENDBRACKET) ) {
                alt88=2;
            }
            else {
                NoViableAltException nvae =
                    new NoViableAltException("620:1: class_element : ( attribute_definition | class_constraint );", 88, 0, input);

                throw nvae;
            }
            switch (alt88) {
                case 1 :
                    // Cubrid.g:621:2: attribute_definition
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_attribute_definition_in_class_element3645);
                    attribute_definition312=attribute_definition();
                    _fsp--;

                    adaptor.addChild(root_0, attribute_definition312.getTree());

                    }
                    break;
                case 2 :
                    // Cubrid.g:622:4: class_constraint
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_class_constraint_in_class_element3651);
                    class_constraint313=class_constraint();
                    _fsp--;

                    adaptor.addChild(root_0, class_constraint313.getTree());

                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end class_element

    public static class class_constraint_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start class_constraint
    // Cubrid.g:625:1: class_constraint : ( ( CONSTRAINT constraint_name )? UNIQUE attribute_comma_list_part | ( PRIMARY KEY attribute_comma_list_part )? | referential_constraint );
    public final class_constraint_return class_constraint() throws RecognitionException {
        class_constraint_return retval = new class_constraint_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token CONSTRAINT314=null;
        Token UNIQUE316=null;
        Token PRIMARY318=null;
        Token KEY319=null;
        constraint_name_return constraint_name315 = null;

        attribute_comma_list_part_return attribute_comma_list_part317 = null;

        attribute_comma_list_part_return attribute_comma_list_part320 = null;

        referential_constraint_return referential_constraint321 = null;


        Object CONSTRAINT314_tree=null;
        Object UNIQUE316_tree=null;
        Object PRIMARY318_tree=null;
        Object KEY319_tree=null;

        try {
            // Cubrid.g:625:17: ( ( CONSTRAINT constraint_name )? UNIQUE attribute_comma_list_part | ( PRIMARY KEY attribute_comma_list_part )? | referential_constraint )
            int alt91=3;
            switch ( input.LA(1) ) {
            case CONSTRAINT:
            case UNIQUE:
                {
                alt91=1;
                }
                break;
            case INHERIT:
            case PRIMARY:
            case END:
            case COMMA:
            case ENDBRACKET:
                {
                alt91=2;
                }
                break;
            case FOREIGN:
                {
                alt91=3;
                }
                break;
            default:
                NoViableAltException nvae =
                    new NoViableAltException("625:1: class_constraint : ( ( CONSTRAINT constraint_name )? UNIQUE attribute_comma_list_part | ( PRIMARY KEY attribute_comma_list_part )? | referential_constraint );", 91, 0, input);

                throw nvae;
            }

            switch (alt91) {
                case 1 :
                    // Cubrid.g:626:2: ( CONSTRAINT constraint_name )? UNIQUE attribute_comma_list_part
                    {
                    root_0 = (Object)adaptor.nil();

                    // Cubrid.g:626:2: ( CONSTRAINT constraint_name )?
                    int alt89=2;
                    int LA89_0 = input.LA(1);

                    if ( (LA89_0==CONSTRAINT) ) {
                        alt89=1;
                    }
                    switch (alt89) {
                        case 1 :
                            // Cubrid.g:626:4: CONSTRAINT constraint_name
                            {
                            CONSTRAINT314=(Token)input.LT(1);
                            match(input,CONSTRAINT,FOLLOW_CONSTRAINT_in_class_constraint3664); 
                            CONSTRAINT314_tree = (Object)adaptor.create(CONSTRAINT314);
                            adaptor.addChild(root_0, CONSTRAINT314_tree);

                            pushFollow(FOLLOW_constraint_name_in_class_constraint3666);
                            constraint_name315=constraint_name();
                            _fsp--;

                            adaptor.addChild(root_0, constraint_name315.getTree());

                            }
                            break;

                    }

                    UNIQUE316=(Token)input.LT(1);
                    match(input,UNIQUE,FOLLOW_UNIQUE_in_class_constraint3671); 
                    UNIQUE316_tree = (Object)adaptor.create(UNIQUE316);
                    adaptor.addChild(root_0, UNIQUE316_tree);

                    pushFollow(FOLLOW_attribute_comma_list_part_in_class_constraint3673);
                    attribute_comma_list_part317=attribute_comma_list_part();
                    _fsp--;

                    adaptor.addChild(root_0, attribute_comma_list_part317.getTree());

                    }
                    break;
                case 2 :
                    // Cubrid.g:628:3: ( PRIMARY KEY attribute_comma_list_part )?
                    {
                    root_0 = (Object)adaptor.nil();

                    // Cubrid.g:628:3: ( PRIMARY KEY attribute_comma_list_part )?
                    int alt90=2;
                    int LA90_0 = input.LA(1);

                    if ( (LA90_0==PRIMARY) ) {
                        alt90=1;
                    }
                    switch (alt90) {
                        case 1 :
                            // Cubrid.g:628:4: PRIMARY KEY attribute_comma_list_part
                            {
                            PRIMARY318=(Token)input.LT(1);
                            match(input,PRIMARY,FOLLOW_PRIMARY_in_class_constraint3678); 
                            PRIMARY318_tree = (Object)adaptor.create(PRIMARY318);
                            adaptor.addChild(root_0, PRIMARY318_tree);

                            KEY319=(Token)input.LT(1);
                            match(input,KEY,FOLLOW_KEY_in_class_constraint3680); 
                            KEY319_tree = (Object)adaptor.create(KEY319);
                            adaptor.addChild(root_0, KEY319_tree);

                            pushFollow(FOLLOW_attribute_comma_list_part_in_class_constraint3682);
                            attribute_comma_list_part320=attribute_comma_list_part();
                            _fsp--;

                            adaptor.addChild(root_0, attribute_comma_list_part320.getTree());

                            }
                            break;

                    }


                    }
                    break;
                case 3 :
                    // Cubrid.g:629:4: referential_constraint
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_referential_constraint_in_class_constraint3689);
                    referential_constraint321=referential_constraint();
                    _fsp--;

                    adaptor.addChild(root_0, referential_constraint321.getTree());

                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end class_constraint

    public static class constraint_name_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start constraint_name
    // Cubrid.g:632:1: constraint_name : ID ;
    public final constraint_name_return constraint_name() throws RecognitionException {
        constraint_name_return retval = new constraint_name_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ID322=null;

        Object ID322_tree=null;

        try {
            // Cubrid.g:632:16: ( ID )
            // Cubrid.g:633:2: ID
            {
            root_0 = (Object)adaptor.nil();

            ID322=(Token)input.LT(1);
            match(input,ID,FOLLOW_ID_in_constraint_name3701); 
            ID322_tree = (Object)adaptor.create(ID322);
            adaptor.addChild(root_0, ID322_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end constraint_name

    public static class referential_constraint_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start referential_constraint
    // Cubrid.g:636:1: referential_constraint : FOREIGN KEY ( constraint_name )? attribute_comma_list_part REFERENCES ( referenced_table_name )? attribute_comma_list_part ( referential_triggered_action )? ;
    public final referential_constraint_return referential_constraint() throws RecognitionException {
        referential_constraint_return retval = new referential_constraint_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token FOREIGN323=null;
        Token KEY324=null;
        Token REFERENCES327=null;
        constraint_name_return constraint_name325 = null;

        attribute_comma_list_part_return attribute_comma_list_part326 = null;

        referenced_table_name_return referenced_table_name328 = null;

        attribute_comma_list_part_return attribute_comma_list_part329 = null;

        referential_triggered_action_return referential_triggered_action330 = null;


        Object FOREIGN323_tree=null;
        Object KEY324_tree=null;
        Object REFERENCES327_tree=null;

        try {
            // Cubrid.g:636:23: ( FOREIGN KEY ( constraint_name )? attribute_comma_list_part REFERENCES ( referenced_table_name )? attribute_comma_list_part ( referential_triggered_action )? )
            // Cubrid.g:637:2: FOREIGN KEY ( constraint_name )? attribute_comma_list_part REFERENCES ( referenced_table_name )? attribute_comma_list_part ( referential_triggered_action )?
            {
            root_0 = (Object)adaptor.nil();

            FOREIGN323=(Token)input.LT(1);
            match(input,FOREIGN,FOLLOW_FOREIGN_in_referential_constraint3711); 
            FOREIGN323_tree = (Object)adaptor.create(FOREIGN323);
            adaptor.addChild(root_0, FOREIGN323_tree);

            KEY324=(Token)input.LT(1);
            match(input,KEY,FOLLOW_KEY_in_referential_constraint3713); 
            KEY324_tree = (Object)adaptor.create(KEY324);
            adaptor.addChild(root_0, KEY324_tree);

            // Cubrid.g:638:2: ( constraint_name )?
            int alt92=2;
            int LA92_0 = input.LA(1);

            if ( (LA92_0==ID) ) {
                alt92=1;
            }
            switch (alt92) {
                case 1 :
                    // Cubrid.g:638:2: constraint_name
                    {
                    pushFollow(FOLLOW_constraint_name_in_referential_constraint3716);
                    constraint_name325=constraint_name();
                    _fsp--;

                    adaptor.addChild(root_0, constraint_name325.getTree());

                    }
                    break;

            }

            pushFollow(FOLLOW_attribute_comma_list_part_in_referential_constraint3720);
            attribute_comma_list_part326=attribute_comma_list_part();
            _fsp--;

            adaptor.addChild(root_0, attribute_comma_list_part326.getTree());
            REFERENCES327=(Token)input.LT(1);
            match(input,REFERENCES,FOLLOW_REFERENCES_in_referential_constraint3723); 
            REFERENCES327_tree = (Object)adaptor.create(REFERENCES327);
            adaptor.addChild(root_0, REFERENCES327_tree);

            // Cubrid.g:641:2: ( referenced_table_name )?
            int alt93=2;
            int LA93_0 = input.LA(1);

            if ( (LA93_0==ID) ) {
                alt93=1;
            }
            switch (alt93) {
                case 1 :
                    // Cubrid.g:641:2: referenced_table_name
                    {
                    pushFollow(FOLLOW_referenced_table_name_in_referential_constraint3726);
                    referenced_table_name328=referenced_table_name();
                    _fsp--;

                    adaptor.addChild(root_0, referenced_table_name328.getTree());

                    }
                    break;

            }

            pushFollow(FOLLOW_attribute_comma_list_part_in_referential_constraint3730);
            attribute_comma_list_part329=attribute_comma_list_part();
            _fsp--;

            adaptor.addChild(root_0, attribute_comma_list_part329.getTree());
            // Cubrid.g:643:2: ( referential_triggered_action )?
            int alt94=2;
            int LA94_0 = input.LA(1);

            if ( (LA94_0==ON) ) {
                alt94=1;
            }
            switch (alt94) {
                case 1 :
                    // Cubrid.g:643:2: referential_triggered_action
                    {
                    pushFollow(FOLLOW_referential_triggered_action_in_referential_constraint3733);
                    referential_triggered_action330=referential_triggered_action();
                    _fsp--;

                    adaptor.addChild(root_0, referential_triggered_action330.getTree());

                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end referential_constraint

    public static class referenced_table_name_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start referenced_table_name
    // Cubrid.g:646:1: referenced_table_name : ID ;
    public final referenced_table_name_return referenced_table_name() throws RecognitionException {
        referenced_table_name_return retval = new referenced_table_name_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ID331=null;

        Object ID331_tree=null;

        try {
            // Cubrid.g:646:22: ( ID )
            // Cubrid.g:647:2: ID
            {
            root_0 = (Object)adaptor.nil();

            ID331=(Token)input.LT(1);
            match(input,ID,FOLLOW_ID_in_referenced_table_name3744); 
            ID331_tree = (Object)adaptor.create(ID331);
            adaptor.addChild(root_0, ID331_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end referenced_table_name

    public static class referential_triggered_action_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start referential_triggered_action
    // Cubrid.g:650:1: referential_triggered_action : update_rule ( delete_rule ( cache_object_rule )? )? ;
    public final referential_triggered_action_return referential_triggered_action() throws RecognitionException {
        referential_triggered_action_return retval = new referential_triggered_action_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        update_rule_return update_rule332 = null;

        delete_rule_return delete_rule333 = null;

        cache_object_rule_return cache_object_rule334 = null;



        try {
            // Cubrid.g:650:29: ( update_rule ( delete_rule ( cache_object_rule )? )? )
            // Cubrid.g:651:2: update_rule ( delete_rule ( cache_object_rule )? )?
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_update_rule_in_referential_triggered_action3755);
            update_rule332=update_rule();
            _fsp--;

            adaptor.addChild(root_0, update_rule332.getTree());
            // Cubrid.g:652:2: ( delete_rule ( cache_object_rule )? )?
            int alt96=2;
            int LA96_0 = input.LA(1);

            if ( (LA96_0==ON) ) {
                alt96=1;
            }
            switch (alt96) {
                case 1 :
                    // Cubrid.g:652:3: delete_rule ( cache_object_rule )?
                    {
                    pushFollow(FOLLOW_delete_rule_in_referential_triggered_action3759);
                    delete_rule333=delete_rule();
                    _fsp--;

                    adaptor.addChild(root_0, delete_rule333.getTree());
                    // Cubrid.g:652:15: ( cache_object_rule )?
                    int alt95=2;
                    int LA95_0 = input.LA(1);

                    if ( (LA95_0==ON) ) {
                        alt95=1;
                    }
                    switch (alt95) {
                        case 1 :
                            // Cubrid.g:652:15: cache_object_rule
                            {
                            pushFollow(FOLLOW_cache_object_rule_in_referential_triggered_action3761);
                            cache_object_rule334=cache_object_rule();
                            _fsp--;

                            adaptor.addChild(root_0, cache_object_rule334.getTree());

                            }
                            break;

                    }


                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end referential_triggered_action

    public static class update_rule_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start update_rule
    // Cubrid.g:655:1: update_rule : ON UPDATE referential_action ;
    public final update_rule_return update_rule() throws RecognitionException {
        update_rule_return retval = new update_rule_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ON335=null;
        Token UPDATE336=null;
        referential_action_return referential_action337 = null;


        Object ON335_tree=null;
        Object UPDATE336_tree=null;

        try {
            // Cubrid.g:655:12: ( ON UPDATE referential_action )
            // Cubrid.g:656:2: ON UPDATE referential_action
            {
            root_0 = (Object)adaptor.nil();

            ON335=(Token)input.LT(1);
            match(input,ON,FOLLOW_ON_in_update_rule3775); 
            ON335_tree = (Object)adaptor.create(ON335);
            adaptor.addChild(root_0, ON335_tree);

            UPDATE336=(Token)input.LT(1);
            match(input,UPDATE,FOLLOW_UPDATE_in_update_rule3777); 
            UPDATE336_tree = (Object)adaptor.create(UPDATE336);
            adaptor.addChild(root_0, UPDATE336_tree);

            pushFollow(FOLLOW_referential_action_in_update_rule3779);
            referential_action337=referential_action();
            _fsp--;

            adaptor.addChild(root_0, referential_action337.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end update_rule

    public static class delete_rule_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start delete_rule
    // Cubrid.g:659:1: delete_rule : ON DELETE referential_action ;
    public final delete_rule_return delete_rule() throws RecognitionException {
        delete_rule_return retval = new delete_rule_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ON338=null;
        Token DELETE339=null;
        referential_action_return referential_action340 = null;


        Object ON338_tree=null;
        Object DELETE339_tree=null;

        try {
            // Cubrid.g:659:12: ( ON DELETE referential_action )
            // Cubrid.g:660:2: ON DELETE referential_action
            {
            root_0 = (Object)adaptor.nil();

            ON338=(Token)input.LT(1);
            match(input,ON,FOLLOW_ON_in_delete_rule3789); 
            ON338_tree = (Object)adaptor.create(ON338);
            adaptor.addChild(root_0, ON338_tree);

            DELETE339=(Token)input.LT(1);
            match(input,DELETE,FOLLOW_DELETE_in_delete_rule3791); 
            DELETE339_tree = (Object)adaptor.create(DELETE339);
            adaptor.addChild(root_0, DELETE339_tree);

            pushFollow(FOLLOW_referential_action_in_delete_rule3793);
            referential_action340=referential_action();
            _fsp--;

            adaptor.addChild(root_0, referential_action340.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end delete_rule

    public static class referential_action_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start referential_action
    // Cubrid.g:663:1: referential_action : ( CASCADE | RESTRICT | NO ACTION );
    public final referential_action_return referential_action() throws RecognitionException {
        referential_action_return retval = new referential_action_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token CASCADE341=null;
        Token RESTRICT342=null;
        Token NO343=null;
        Token ACTION344=null;

        Object CASCADE341_tree=null;
        Object RESTRICT342_tree=null;
        Object NO343_tree=null;
        Object ACTION344_tree=null;

        try {
            // Cubrid.g:663:19: ( CASCADE | RESTRICT | NO ACTION )
            int alt97=3;
            switch ( input.LA(1) ) {
            case CASCADE:
                {
                alt97=1;
                }
                break;
            case RESTRICT:
                {
                alt97=2;
                }
                break;
            case NO:
                {
                alt97=3;
                }
                break;
            default:
                NoViableAltException nvae =
                    new NoViableAltException("663:1: referential_action : ( CASCADE | RESTRICT | NO ACTION );", 97, 0, input);

                throw nvae;
            }

            switch (alt97) {
                case 1 :
                    // Cubrid.g:664:2: CASCADE
                    {
                    root_0 = (Object)adaptor.nil();

                    CASCADE341=(Token)input.LT(1);
                    match(input,CASCADE,FOLLOW_CASCADE_in_referential_action3804); 
                    CASCADE341_tree = (Object)adaptor.create(CASCADE341);
                    adaptor.addChild(root_0, CASCADE341_tree);


                    }
                    break;
                case 2 :
                    // Cubrid.g:665:4: RESTRICT
                    {
                    root_0 = (Object)adaptor.nil();

                    RESTRICT342=(Token)input.LT(1);
                    match(input,RESTRICT,FOLLOW_RESTRICT_in_referential_action3810); 
                    RESTRICT342_tree = (Object)adaptor.create(RESTRICT342);
                    adaptor.addChild(root_0, RESTRICT342_tree);


                    }
                    break;
                case 3 :
                    // Cubrid.g:666:4: NO ACTION
                    {
                    root_0 = (Object)adaptor.nil();

                    NO343=(Token)input.LT(1);
                    match(input,NO,FOLLOW_NO_in_referential_action3816); 
                    NO343_tree = (Object)adaptor.create(NO343);
                    adaptor.addChild(root_0, NO343_tree);

                    ACTION344=(Token)input.LT(1);
                    match(input,ACTION,FOLLOW_ACTION_in_referential_action3818); 
                    ACTION344_tree = (Object)adaptor.create(ACTION344);
                    adaptor.addChild(root_0, ACTION344_tree);


                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end referential_action

    public static class cache_object_rule_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start cache_object_rule
    // Cubrid.g:669:1: cache_object_rule : ON CACHE OBJECT cache_object_column_name ;
    public final cache_object_rule_return cache_object_rule() throws RecognitionException {
        cache_object_rule_return retval = new cache_object_rule_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ON345=null;
        Token CACHE346=null;
        Token OBJECT347=null;
        cache_object_column_name_return cache_object_column_name348 = null;


        Object ON345_tree=null;
        Object CACHE346_tree=null;
        Object OBJECT347_tree=null;

        try {
            // Cubrid.g:669:18: ( ON CACHE OBJECT cache_object_column_name )
            // Cubrid.g:670:2: ON CACHE OBJECT cache_object_column_name
            {
            root_0 = (Object)adaptor.nil();

            ON345=(Token)input.LT(1);
            match(input,ON,FOLLOW_ON_in_cache_object_rule3828); 
            ON345_tree = (Object)adaptor.create(ON345);
            adaptor.addChild(root_0, ON345_tree);

            CACHE346=(Token)input.LT(1);
            match(input,CACHE,FOLLOW_CACHE_in_cache_object_rule3830); 
            CACHE346_tree = (Object)adaptor.create(CACHE346);
            adaptor.addChild(root_0, CACHE346_tree);

            OBJECT347=(Token)input.LT(1);
            match(input,OBJECT,FOLLOW_OBJECT_in_cache_object_rule3832); 
            OBJECT347_tree = (Object)adaptor.create(OBJECT347);
            adaptor.addChild(root_0, OBJECT347_tree);

            pushFollow(FOLLOW_cache_object_column_name_in_cache_object_rule3834);
            cache_object_column_name348=cache_object_column_name();
            _fsp--;

            adaptor.addChild(root_0, cache_object_column_name348.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end cache_object_rule

    public static class cache_object_column_name_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start cache_object_column_name
    // Cubrid.g:673:1: cache_object_column_name : attribute_name ;
    public final cache_object_column_name_return cache_object_column_name() throws RecognitionException {
        cache_object_column_name_return retval = new cache_object_column_name_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        attribute_name_return attribute_name349 = null;



        try {
            // Cubrid.g:673:25: ( attribute_name )
            // Cubrid.g:674:2: attribute_name
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_attribute_name_in_cache_object_column_name3845);
            attribute_name349=attribute_name();
            _fsp--;

            adaptor.addChild(root_0, attribute_name349.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end cache_object_column_name

    public static class view_attribute_definition_part_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start view_attribute_definition_part
    // Cubrid.g:677:1: view_attribute_definition_part : STARTBRACKET view_attribute_def_comma_list ENDBRACKET ;
    public final view_attribute_definition_part_return view_attribute_definition_part() throws RecognitionException {
        view_attribute_definition_part_return retval = new view_attribute_definition_part_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token STARTBRACKET350=null;
        Token ENDBRACKET352=null;
        view_attribute_def_comma_list_return view_attribute_def_comma_list351 = null;


        Object STARTBRACKET350_tree=null;
        Object ENDBRACKET352_tree=null;

        try {
            // Cubrid.g:677:31: ( STARTBRACKET view_attribute_def_comma_list ENDBRACKET )
            // Cubrid.g:678:2: STARTBRACKET view_attribute_def_comma_list ENDBRACKET
            {
            root_0 = (Object)adaptor.nil();

            STARTBRACKET350=(Token)input.LT(1);
            match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_view_attribute_definition_part3857); 
            STARTBRACKET350_tree = (Object)adaptor.create(STARTBRACKET350);
            adaptor.addChild(root_0, STARTBRACKET350_tree);

            pushFollow(FOLLOW_view_attribute_def_comma_list_in_view_attribute_definition_part3861);
            view_attribute_def_comma_list351=view_attribute_def_comma_list();
            _fsp--;

            adaptor.addChild(root_0, view_attribute_def_comma_list351.getTree());
            ENDBRACKET352=(Token)input.LT(1);
            match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_view_attribute_definition_part3865); 
            ENDBRACKET352_tree = (Object)adaptor.create(ENDBRACKET352);
            adaptor.addChild(root_0, ENDBRACKET352_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end view_attribute_definition_part

    public static class view_attribute_def_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start view_attribute_def_comma_list
    // Cubrid.g:683:1: view_attribute_def_comma_list : view_attribute_definition ( COMMA view_attribute_definition )* -> ENTER TAB view_attribute_definition ( COMMA ENTER view_attribute_definition )* ENTER UNTAB ;
    public final view_attribute_def_comma_list_return view_attribute_def_comma_list() throws RecognitionException {
        view_attribute_def_comma_list_return retval = new view_attribute_def_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA354=null;
        view_attribute_definition_return view_attribute_definition353 = null;

        view_attribute_definition_return view_attribute_definition355 = null;


        Object COMMA354_tree=null;
        RewriteRuleTokenStream stream_COMMA=new RewriteRuleTokenStream(adaptor,"token COMMA");
        RewriteRuleSubtreeStream stream_view_attribute_definition=new RewriteRuleSubtreeStream(adaptor,"rule view_attribute_definition");
        try {
            // Cubrid.g:683:30: ( view_attribute_definition ( COMMA view_attribute_definition )* -> ENTER TAB view_attribute_definition ( COMMA ENTER view_attribute_definition )* ENTER UNTAB )
            // Cubrid.g:684:2: view_attribute_definition ( COMMA view_attribute_definition )*
            {
            pushFollow(FOLLOW_view_attribute_definition_in_view_attribute_def_comma_list3876);
            view_attribute_definition353=view_attribute_definition();
            _fsp--;

            stream_view_attribute_definition.add(view_attribute_definition353.getTree());
            // Cubrid.g:684:28: ( COMMA view_attribute_definition )*
            loop98:
            do {
                int alt98=2;
                int LA98_0 = input.LA(1);

                if ( (LA98_0==COMMA) ) {
                    alt98=1;
                }


                switch (alt98) {
            	case 1 :
            	    // Cubrid.g:684:29: COMMA view_attribute_definition
            	    {
            	    COMMA354=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_view_attribute_def_comma_list3879); 
            	    stream_COMMA.add(COMMA354);

            	    pushFollow(FOLLOW_view_attribute_definition_in_view_attribute_def_comma_list3881);
            	    view_attribute_definition355=view_attribute_definition();
            	    _fsp--;

            	    stream_view_attribute_definition.add(view_attribute_definition355.getTree());

            	    }
            	    break;

            	default :
            	    break loop98;
                }
            } while (true);


            // AST REWRITE
            // elements: view_attribute_definition, COMMA, view_attribute_definition
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 685:2: -> ENTER TAB view_attribute_definition ( COMMA ENTER view_attribute_definition )* ENTER UNTAB
            {
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));
                adaptor.addChild(root_0, stream_view_attribute_definition.next());
                // Cubrid.g:686:38: ( COMMA ENTER view_attribute_definition )*
                while ( stream_view_attribute_definition.hasNext()||stream_COMMA.hasNext() ) {
                    adaptor.addChild(root_0, stream_COMMA.next());
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, stream_view_attribute_definition.next());

                }
                stream_view_attribute_definition.reset();
                stream_COMMA.reset();
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end view_attribute_def_comma_list

    public static class view_attribute_definition_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start view_attribute_definition
    // Cubrid.g:689:1: view_attribute_definition : ( attribute_definition | attribute_name );
    public final view_attribute_definition_return view_attribute_definition() throws RecognitionException {
        view_attribute_definition_return retval = new view_attribute_definition_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        attribute_definition_return attribute_definition356 = null;

        attribute_name_return attribute_name357 = null;



        try {
            // Cubrid.g:689:26: ( attribute_definition | attribute_name )
            int alt99=2;
            switch ( input.LA(1) ) {
            case CLASS:
                {
                alt99=1;
                }
                break;
            case ID:
                {
                switch ( input.LA(2) ) {
                case BIT:
                case CHAR:
                case DATE:
                case DECIMAL:
                case DOUBLE:
                case FLOAT:
                case INT:
                case INTEGER:
                case LIST:
                case MONETARY:
                case MULTISET:
                case NCHAR:
                case NUMERIC:
                case OBJECT:
                case REAL:
                case SET:
                case SMALLINT:
                case STRING_STR:
                case TIME:
                case TIMESTAMP:
                case VARCHAR:
                case ID:
                case COLUMN:
                case STARTBRACKET:
                    {
                    alt99=1;
                    }
                    break;
                case DOT:
                    {
                    int LA99_4 = input.LA(3);

                    if ( ((LA99_4>=ID && LA99_4<=COLUMN)) ) {
                        int LA99_6 = input.LA(4);

                        if ( (LA99_6==COMMA||LA99_6==ENDBRACKET) ) {
                            alt99=2;
                        }
                        else if ( (LA99_6==BIT||LA99_6==CHAR||(LA99_6>=DATE && LA99_6<=DECIMAL)||LA99_6==DOUBLE||LA99_6==FLOAT||(LA99_6>=INT && LA99_6<=INTEGER)||LA99_6==LIST||(LA99_6>=MONETARY && LA99_6<=MULTISET)||LA99_6==NCHAR||(LA99_6>=NUMERIC && LA99_6<=OBJECT)||LA99_6==REAL||LA99_6==SET||LA99_6==SMALLINT||LA99_6==STRING_STR||(LA99_6>=TIME && LA99_6<=TIMESTAMP)||LA99_6==VARCHAR||(LA99_6>=ID && LA99_6<=COLUMN)) ) {
                            alt99=1;
                        }
                        else {
                            NoViableAltException nvae =
                                new NoViableAltException("689:1: view_attribute_definition : ( attribute_definition | attribute_name );", 99, 6, input);

                            throw nvae;
                        }
                    }
                    else {
                        NoViableAltException nvae =
                            new NoViableAltException("689:1: view_attribute_definition : ( attribute_definition | attribute_name );", 99, 4, input);

                        throw nvae;
                    }
                    }
                    break;
                case COMMA:
                case ENDBRACKET:
                    {
                    alt99=2;
                    }
                    break;
                default:
                    NoViableAltException nvae =
                        new NoViableAltException("689:1: view_attribute_definition : ( attribute_definition | attribute_name );", 99, 2, input);

                    throw nvae;
                }

                }
                break;
            case COLUMN:
                {
                int LA99_3 = input.LA(2);

                if ( (LA99_3==BIT||LA99_3==CHAR||(LA99_3>=DATE && LA99_3<=DECIMAL)||LA99_3==DOUBLE||LA99_3==FLOAT||(LA99_3>=INT && LA99_3<=INTEGER)||LA99_3==LIST||(LA99_3>=MONETARY && LA99_3<=MULTISET)||LA99_3==NCHAR||(LA99_3>=NUMERIC && LA99_3<=OBJECT)||LA99_3==REAL||LA99_3==SET||LA99_3==SMALLINT||LA99_3==STRING_STR||(LA99_3>=TIME && LA99_3<=TIMESTAMP)||LA99_3==VARCHAR||(LA99_3>=ID && LA99_3<=COLUMN)) ) {
                    alt99=1;
                }
                else if ( (LA99_3==COMMA||LA99_3==ENDBRACKET) ) {
                    alt99=2;
                }
                else {
                    NoViableAltException nvae =
                        new NoViableAltException("689:1: view_attribute_definition : ( attribute_definition | attribute_name );", 99, 3, input);

                    throw nvae;
                }
                }
                break;
            default:
                NoViableAltException nvae =
                    new NoViableAltException("689:1: view_attribute_definition : ( attribute_definition | attribute_name );", 99, 0, input);

                throw nvae;
            }

            switch (alt99) {
                case 1 :
                    // Cubrid.g:690:2: attribute_definition
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_attribute_definition_in_view_attribute_definition3917);
                    attribute_definition356=attribute_definition();
                    _fsp--;

                    adaptor.addChild(root_0, attribute_definition356.getTree());

                    }
                    break;
                case 2 :
                    // Cubrid.g:691:4: attribute_name
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_attribute_name_in_view_attribute_definition3922);
                    attribute_name357=attribute_name();
                    _fsp--;

                    adaptor.addChild(root_0, attribute_name357.getTree());

                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end view_attribute_definition

    public static class attribute_definition_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start attribute_definition
    // Cubrid.g:694:1: attribute_definition : ( ( general_attribute_name attribute_type ( default_or_shared )? ( auto_increment )? ( attribute_constraint_list )? ) | function );
    public final attribute_definition_return attribute_definition() throws RecognitionException {
        attribute_definition_return retval = new attribute_definition_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        general_attribute_name_return general_attribute_name358 = null;

        attribute_type_return attribute_type359 = null;

        default_or_shared_return default_or_shared360 = null;

        auto_increment_return auto_increment361 = null;

        attribute_constraint_list_return attribute_constraint_list362 = null;

        function_return function363 = null;



        try {
            // Cubrid.g:694:21: ( ( general_attribute_name attribute_type ( default_or_shared )? ( auto_increment )? ( attribute_constraint_list )? ) | function )
            int alt103=2;
            int LA103_0 = input.LA(1);

            if ( (LA103_0==CLASS||LA103_0==COLUMN) ) {
                alt103=1;
            }
            else if ( (LA103_0==ID) ) {
                int LA103_2 = input.LA(2);

                if ( (LA103_2==STARTBRACKET) ) {
                    alt103=2;
                }
                else if ( (LA103_2==BIT||LA103_2==CHAR||(LA103_2>=DATE && LA103_2<=DECIMAL)||LA103_2==DOUBLE||LA103_2==FLOAT||(LA103_2>=INT && LA103_2<=INTEGER)||LA103_2==LIST||(LA103_2>=MONETARY && LA103_2<=MULTISET)||LA103_2==NCHAR||(LA103_2>=NUMERIC && LA103_2<=OBJECT)||LA103_2==REAL||LA103_2==SET||LA103_2==SMALLINT||LA103_2==STRING_STR||(LA103_2>=TIME && LA103_2<=TIMESTAMP)||LA103_2==VARCHAR||LA103_2==DOT||(LA103_2>=ID && LA103_2<=COLUMN)) ) {
                    alt103=1;
                }
                else {
                    NoViableAltException nvae =
                        new NoViableAltException("694:1: attribute_definition : ( ( general_attribute_name attribute_type ( default_or_shared )? ( auto_increment )? ( attribute_constraint_list )? ) | function );", 103, 2, input);

                    throw nvae;
                }
            }
            else {
                NoViableAltException nvae =
                    new NoViableAltException("694:1: attribute_definition : ( ( general_attribute_name attribute_type ( default_or_shared )? ( auto_increment )? ( attribute_constraint_list )? ) | function );", 103, 0, input);

                throw nvae;
            }
            switch (alt103) {
                case 1 :
                    // Cubrid.g:695:2: ( general_attribute_name attribute_type ( default_or_shared )? ( auto_increment )? ( attribute_constraint_list )? )
                    {
                    root_0 = (Object)adaptor.nil();

                    // Cubrid.g:695:2: ( general_attribute_name attribute_type ( default_or_shared )? ( auto_increment )? ( attribute_constraint_list )? )
                    // Cubrid.g:695:3: general_attribute_name attribute_type ( default_or_shared )? ( auto_increment )? ( attribute_constraint_list )?
                    {
                    pushFollow(FOLLOW_general_attribute_name_in_attribute_definition3933);
                    general_attribute_name358=general_attribute_name();
                    _fsp--;

                    adaptor.addChild(root_0, general_attribute_name358.getTree());
                    pushFollow(FOLLOW_attribute_type_in_attribute_definition3935);
                    attribute_type359=attribute_type();
                    _fsp--;

                    adaptor.addChild(root_0, attribute_type359.getTree());
                    // Cubrid.g:696:2: ( default_or_shared )?
                    int alt100=2;
                    int LA100_0 = input.LA(1);

                    if ( (LA100_0==DEFAULT||LA100_0==SHARE) ) {
                        alt100=1;
                    }
                    switch (alt100) {
                        case 1 :
                            // Cubrid.g:696:2: default_or_shared
                            {
                            pushFollow(FOLLOW_default_or_shared_in_attribute_definition3938);
                            default_or_shared360=default_or_shared();
                            _fsp--;

                            adaptor.addChild(root_0, default_or_shared360.getTree());

                            }
                            break;

                    }

                    // Cubrid.g:697:2: ( auto_increment )?
                    int alt101=2;
                    int LA101_0 = input.LA(1);

                    if ( (LA101_0==AUTO_INCREMENT) ) {
                        alt101=1;
                    }
                    switch (alt101) {
                        case 1 :
                            // Cubrid.g:697:2: auto_increment
                            {
                            pushFollow(FOLLOW_auto_increment_in_attribute_definition3942);
                            auto_increment361=auto_increment();
                            _fsp--;

                            adaptor.addChild(root_0, auto_increment361.getTree());

                            }
                            break;

                    }

                    // Cubrid.g:698:2: ( attribute_constraint_list )?
                    int alt102=2;
                    int LA102_0 = input.LA(1);

                    if ( (LA102_0==NOT||LA102_0==PRIMARY||LA102_0==UNIQUE) ) {
                        alt102=1;
                    }
                    switch (alt102) {
                        case 1 :
                            // Cubrid.g:698:2: attribute_constraint_list
                            {
                            pushFollow(FOLLOW_attribute_constraint_list_in_attribute_definition3946);
                            attribute_constraint_list362=attribute_constraint_list();
                            _fsp--;

                            adaptor.addChild(root_0, attribute_constraint_list362.getTree());

                            }
                            break;

                    }


                    }


                    }
                    break;
                case 2 :
                    // Cubrid.g:699:4: function
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_function_in_attribute_definition3953);
                    function363=function();
                    _fsp--;

                    adaptor.addChild(root_0, function363.getTree());

                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end attribute_definition

    public static class auto_increment_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start auto_increment
    // Cubrid.g:702:1: auto_increment : AUTO_INCREMENT ( STARTBRACKET DECIMALLITERAL COMMA DECIMALLITERAL ENDBRACKET )? ;
    public final auto_increment_return auto_increment() throws RecognitionException {
        auto_increment_return retval = new auto_increment_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token AUTO_INCREMENT364=null;
        Token STARTBRACKET365=null;
        Token DECIMALLITERAL366=null;
        Token COMMA367=null;
        Token DECIMALLITERAL368=null;
        Token ENDBRACKET369=null;

        Object AUTO_INCREMENT364_tree=null;
        Object STARTBRACKET365_tree=null;
        Object DECIMALLITERAL366_tree=null;
        Object COMMA367_tree=null;
        Object DECIMALLITERAL368_tree=null;
        Object ENDBRACKET369_tree=null;

        try {
            // Cubrid.g:702:15: ( AUTO_INCREMENT ( STARTBRACKET DECIMALLITERAL COMMA DECIMALLITERAL ENDBRACKET )? )
            // Cubrid.g:703:2: AUTO_INCREMENT ( STARTBRACKET DECIMALLITERAL COMMA DECIMALLITERAL ENDBRACKET )?
            {
            root_0 = (Object)adaptor.nil();

            AUTO_INCREMENT364=(Token)input.LT(1);
            match(input,AUTO_INCREMENT,FOLLOW_AUTO_INCREMENT_in_auto_increment3963); 
            AUTO_INCREMENT364_tree = (Object)adaptor.create(AUTO_INCREMENT364);
            adaptor.addChild(root_0, AUTO_INCREMENT364_tree);

            // Cubrid.g:703:17: ( STARTBRACKET DECIMALLITERAL COMMA DECIMALLITERAL ENDBRACKET )?
            int alt104=2;
            int LA104_0 = input.LA(1);

            if ( (LA104_0==STARTBRACKET) ) {
                alt104=1;
            }
            switch (alt104) {
                case 1 :
                    // Cubrid.g:703:18: STARTBRACKET DECIMALLITERAL COMMA DECIMALLITERAL ENDBRACKET
                    {
                    STARTBRACKET365=(Token)input.LT(1);
                    match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_auto_increment3966); 
                    STARTBRACKET365_tree = (Object)adaptor.create(STARTBRACKET365);
                    adaptor.addChild(root_0, STARTBRACKET365_tree);

                    DECIMALLITERAL366=(Token)input.LT(1);
                    match(input,DECIMALLITERAL,FOLLOW_DECIMALLITERAL_in_auto_increment3968); 
                    DECIMALLITERAL366_tree = (Object)adaptor.create(DECIMALLITERAL366);
                    adaptor.addChild(root_0, DECIMALLITERAL366_tree);

                    COMMA367=(Token)input.LT(1);
                    match(input,COMMA,FOLLOW_COMMA_in_auto_increment3970); 
                    COMMA367_tree = (Object)adaptor.create(COMMA367);
                    adaptor.addChild(root_0, COMMA367_tree);

                    DECIMALLITERAL368=(Token)input.LT(1);
                    match(input,DECIMALLITERAL,FOLLOW_DECIMALLITERAL_in_auto_increment3972); 
                    DECIMALLITERAL368_tree = (Object)adaptor.create(DECIMALLITERAL368);
                    adaptor.addChild(root_0, DECIMALLITERAL368_tree);

                    ENDBRACKET369=(Token)input.LT(1);
                    match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_auto_increment3974); 
                    ENDBRACKET369_tree = (Object)adaptor.create(ENDBRACKET369);
                    adaptor.addChild(root_0, ENDBRACKET369_tree);


                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end auto_increment

    public static class general_attribute_name_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start general_attribute_name
    // Cubrid.g:706:1: general_attribute_name : ( CLASS )? attribute_name ;
    public final general_attribute_name_return general_attribute_name() throws RecognitionException {
        general_attribute_name_return retval = new general_attribute_name_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token CLASS370=null;
        attribute_name_return attribute_name371 = null;


        Object CLASS370_tree=null;

        try {
            // Cubrid.g:706:23: ( ( CLASS )? attribute_name )
            // Cubrid.g:707:2: ( CLASS )? attribute_name
            {
            root_0 = (Object)adaptor.nil();

            // Cubrid.g:707:2: ( CLASS )?
            int alt105=2;
            int LA105_0 = input.LA(1);

            if ( (LA105_0==CLASS) ) {
                alt105=1;
            }
            switch (alt105) {
                case 1 :
                    // Cubrid.g:707:2: CLASS
                    {
                    CLASS370=(Token)input.LT(1);
                    match(input,CLASS,FOLLOW_CLASS_in_general_attribute_name3986); 
                    CLASS370_tree = (Object)adaptor.create(CLASS370);
                    adaptor.addChild(root_0, CLASS370_tree);


                    }
                    break;

            }

            pushFollow(FOLLOW_attribute_name_in_general_attribute_name3989);
            attribute_name371=attribute_name();
            _fsp--;

            adaptor.addChild(root_0, attribute_name371.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end general_attribute_name

    public static class attribute_type_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start attribute_type
    // Cubrid.g:710:1: attribute_type : domain ;
    public final attribute_type_return attribute_type() throws RecognitionException {
        attribute_type_return retval = new attribute_type_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        domain_return domain372 = null;



        try {
            // Cubrid.g:710:15: ( domain )
            // Cubrid.g:711:2: domain
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_domain_in_attribute_type4000);
            domain372=domain();
            _fsp--;

            adaptor.addChild(root_0, domain372.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end attribute_type

    public static class domain_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start domain
    // Cubrid.g:714:1: domain : ( privative_type | collections );
    public final domain_return domain() throws RecognitionException {
        domain_return retval = new domain_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        privative_type_return privative_type373 = null;

        collections_return collections374 = null;



        try {
            // Cubrid.g:714:7: ( privative_type | collections )
            int alt106=2;
            int LA106_0 = input.LA(1);

            if ( (LA106_0==BIT||LA106_0==CHAR||(LA106_0>=DATE && LA106_0<=DECIMAL)||LA106_0==DOUBLE||LA106_0==FLOAT||(LA106_0>=INT && LA106_0<=INTEGER)||LA106_0==MONETARY||LA106_0==NCHAR||(LA106_0>=NUMERIC && LA106_0<=OBJECT)||LA106_0==REAL||LA106_0==SMALLINT||LA106_0==STRING_STR||(LA106_0>=TIME && LA106_0<=TIMESTAMP)||LA106_0==VARCHAR||(LA106_0>=ID && LA106_0<=COLUMN)) ) {
                alt106=1;
            }
            else if ( (LA106_0==LIST||LA106_0==MULTISET||LA106_0==SET) ) {
                alt106=2;
            }
            else {
                NoViableAltException nvae =
                    new NoViableAltException("714:1: domain : ( privative_type | collections );", 106, 0, input);

                throw nvae;
            }
            switch (alt106) {
                case 1 :
                    // Cubrid.g:715:2: privative_type
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_privative_type_in_domain4012);
                    privative_type373=privative_type();
                    _fsp--;

                    adaptor.addChild(root_0, privative_type373.getTree());

                    }
                    break;
                case 2 :
                    // Cubrid.g:716:4: collections
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_collections_in_domain4017);
                    collections374=collections();
                    _fsp--;

                    adaptor.addChild(root_0, collections374.getTree());

                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end domain

    public static class domain_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start domain_comma_list
    // Cubrid.g:718:1: domain_comma_list : domain ( COMMA domain )* ;
    public final domain_comma_list_return domain_comma_list() throws RecognitionException {
        domain_comma_list_return retval = new domain_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA376=null;
        domain_return domain375 = null;

        domain_return domain377 = null;


        Object COMMA376_tree=null;

        try {
            // Cubrid.g:718:18: ( domain ( COMMA domain )* )
            // Cubrid.g:719:2: domain ( COMMA domain )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_domain_in_domain_comma_list4026);
            domain375=domain();
            _fsp--;

            adaptor.addChild(root_0, domain375.getTree());
            // Cubrid.g:719:9: ( COMMA domain )*
            loop107:
            do {
                int alt107=2;
                int LA107_0 = input.LA(1);

                if ( (LA107_0==COMMA) ) {
                    alt107=1;
                }


                switch (alt107) {
            	case 1 :
            	    // Cubrid.g:719:10: COMMA domain
            	    {
            	    COMMA376=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_domain_comma_list4029); 
            	    COMMA376_tree = (Object)adaptor.create(COMMA376);
            	    adaptor.addChild(root_0, COMMA376_tree);

            	    pushFollow(FOLLOW_domain_in_domain_comma_list4031);
            	    domain377=domain();
            	    _fsp--;

            	    adaptor.addChild(root_0, domain377.getTree());

            	    }
            	    break;

            	default :
            	    break loop107;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end domain_comma_list

    public static class collections_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start collections
    // Cubrid.g:722:1: collections : ( SET domain | SET STARTBRACKET domain_comma_list ENDBRACKET | LIST OR SEQUENCE domain | LIST OR SEQUENCE STARTBRACKET domain_comma_list ENDBRACKET | MULTISET domain | MULTISET STARTBRACKET domain_comma_list ENDBRACKET );
    public final collections_return collections() throws RecognitionException {
        collections_return retval = new collections_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token SET378=null;
        Token SET380=null;
        Token STARTBRACKET381=null;
        Token ENDBRACKET383=null;
        Token LIST384=null;
        Token OR385=null;
        Token SEQUENCE386=null;
        Token LIST388=null;
        Token OR389=null;
        Token SEQUENCE390=null;
        Token STARTBRACKET391=null;
        Token ENDBRACKET393=null;
        Token MULTISET394=null;
        Token MULTISET396=null;
        Token STARTBRACKET397=null;
        Token ENDBRACKET399=null;
        domain_return domain379 = null;

        domain_comma_list_return domain_comma_list382 = null;

        domain_return domain387 = null;

        domain_comma_list_return domain_comma_list392 = null;

        domain_return domain395 = null;

        domain_comma_list_return domain_comma_list398 = null;


        Object SET378_tree=null;
        Object SET380_tree=null;
        Object STARTBRACKET381_tree=null;
        Object ENDBRACKET383_tree=null;
        Object LIST384_tree=null;
        Object OR385_tree=null;
        Object SEQUENCE386_tree=null;
        Object LIST388_tree=null;
        Object OR389_tree=null;
        Object SEQUENCE390_tree=null;
        Object STARTBRACKET391_tree=null;
        Object ENDBRACKET393_tree=null;
        Object MULTISET394_tree=null;
        Object MULTISET396_tree=null;
        Object STARTBRACKET397_tree=null;
        Object ENDBRACKET399_tree=null;

        try {
            // Cubrid.g:722:12: ( SET domain | SET STARTBRACKET domain_comma_list ENDBRACKET | LIST OR SEQUENCE domain | LIST OR SEQUENCE STARTBRACKET domain_comma_list ENDBRACKET | MULTISET domain | MULTISET STARTBRACKET domain_comma_list ENDBRACKET )
            int alt108=6;
            switch ( input.LA(1) ) {
            case SET:
                {
                int LA108_1 = input.LA(2);

                if ( (LA108_1==STARTBRACKET) ) {
                    alt108=2;
                }
                else if ( (LA108_1==BIT||LA108_1==CHAR||(LA108_1>=DATE && LA108_1<=DECIMAL)||LA108_1==DOUBLE||LA108_1==FLOAT||(LA108_1>=INT && LA108_1<=INTEGER)||LA108_1==LIST||(LA108_1>=MONETARY && LA108_1<=MULTISET)||LA108_1==NCHAR||(LA108_1>=NUMERIC && LA108_1<=OBJECT)||LA108_1==REAL||LA108_1==SET||LA108_1==SMALLINT||LA108_1==STRING_STR||(LA108_1>=TIME && LA108_1<=TIMESTAMP)||LA108_1==VARCHAR||(LA108_1>=ID && LA108_1<=COLUMN)) ) {
                    alt108=1;
                }
                else {
                    NoViableAltException nvae =
                        new NoViableAltException("722:1: collections : ( SET domain | SET STARTBRACKET domain_comma_list ENDBRACKET | LIST OR SEQUENCE domain | LIST OR SEQUENCE STARTBRACKET domain_comma_list ENDBRACKET | MULTISET domain | MULTISET STARTBRACKET domain_comma_list ENDBRACKET );", 108, 1, input);

                    throw nvae;
                }
                }
                break;
            case LIST:
                {
                int LA108_2 = input.LA(2);

                if ( (LA108_2==OR) ) {
                    int LA108_6 = input.LA(3);

                    if ( (LA108_6==SEQUENCE) ) {
                        int LA108_9 = input.LA(4);

                        if ( (LA108_9==STARTBRACKET) ) {
                            alt108=4;
                        }
                        else if ( (LA108_9==BIT||LA108_9==CHAR||(LA108_9>=DATE && LA108_9<=DECIMAL)||LA108_9==DOUBLE||LA108_9==FLOAT||(LA108_9>=INT && LA108_9<=INTEGER)||LA108_9==LIST||(LA108_9>=MONETARY && LA108_9<=MULTISET)||LA108_9==NCHAR||(LA108_9>=NUMERIC && LA108_9<=OBJECT)||LA108_9==REAL||LA108_9==SET||LA108_9==SMALLINT||LA108_9==STRING_STR||(LA108_9>=TIME && LA108_9<=TIMESTAMP)||LA108_9==VARCHAR||(LA108_9>=ID && LA108_9<=COLUMN)) ) {
                            alt108=3;
                        }
                        else {
                            NoViableAltException nvae =
                                new NoViableAltException("722:1: collections : ( SET domain | SET STARTBRACKET domain_comma_list ENDBRACKET | LIST OR SEQUENCE domain | LIST OR SEQUENCE STARTBRACKET domain_comma_list ENDBRACKET | MULTISET domain | MULTISET STARTBRACKET domain_comma_list ENDBRACKET );", 108, 9, input);

                            throw nvae;
                        }
                    }
                    else {
                        NoViableAltException nvae =
                            new NoViableAltException("722:1: collections : ( SET domain | SET STARTBRACKET domain_comma_list ENDBRACKET | LIST OR SEQUENCE domain | LIST OR SEQUENCE STARTBRACKET domain_comma_list ENDBRACKET | MULTISET domain | MULTISET STARTBRACKET domain_comma_list ENDBRACKET );", 108, 6, input);

                        throw nvae;
                    }
                }
                else {
                    NoViableAltException nvae =
                        new NoViableAltException("722:1: collections : ( SET domain | SET STARTBRACKET domain_comma_list ENDBRACKET | LIST OR SEQUENCE domain | LIST OR SEQUENCE STARTBRACKET domain_comma_list ENDBRACKET | MULTISET domain | MULTISET STARTBRACKET domain_comma_list ENDBRACKET );", 108, 2, input);

                    throw nvae;
                }
                }
                break;
            case MULTISET:
                {
                int LA108_3 = input.LA(2);

                if ( (LA108_3==STARTBRACKET) ) {
                    alt108=6;
                }
                else if ( (LA108_3==BIT||LA108_3==CHAR||(LA108_3>=DATE && LA108_3<=DECIMAL)||LA108_3==DOUBLE||LA108_3==FLOAT||(LA108_3>=INT && LA108_3<=INTEGER)||LA108_3==LIST||(LA108_3>=MONETARY && LA108_3<=MULTISET)||LA108_3==NCHAR||(LA108_3>=NUMERIC && LA108_3<=OBJECT)||LA108_3==REAL||LA108_3==SET||LA108_3==SMALLINT||LA108_3==STRING_STR||(LA108_3>=TIME && LA108_3<=TIMESTAMP)||LA108_3==VARCHAR||(LA108_3>=ID && LA108_3<=COLUMN)) ) {
                    alt108=5;
                }
                else {
                    NoViableAltException nvae =
                        new NoViableAltException("722:1: collections : ( SET domain | SET STARTBRACKET domain_comma_list ENDBRACKET | LIST OR SEQUENCE domain | LIST OR SEQUENCE STARTBRACKET domain_comma_list ENDBRACKET | MULTISET domain | MULTISET STARTBRACKET domain_comma_list ENDBRACKET );", 108, 3, input);

                    throw nvae;
                }
                }
                break;
            default:
                NoViableAltException nvae =
                    new NoViableAltException("722:1: collections : ( SET domain | SET STARTBRACKET domain_comma_list ENDBRACKET | LIST OR SEQUENCE domain | LIST OR SEQUENCE STARTBRACKET domain_comma_list ENDBRACKET | MULTISET domain | MULTISET STARTBRACKET domain_comma_list ENDBRACKET );", 108, 0, input);

                throw nvae;
            }

            switch (alt108) {
                case 1 :
                    // Cubrid.g:723:2: SET domain
                    {
                    root_0 = (Object)adaptor.nil();

                    SET378=(Token)input.LT(1);
                    match(input,SET,FOLLOW_SET_in_collections4044); 
                    SET378_tree = (Object)adaptor.create(SET378);
                    adaptor.addChild(root_0, SET378_tree);

                    pushFollow(FOLLOW_domain_in_collections4046);
                    domain379=domain();
                    _fsp--;

                    adaptor.addChild(root_0, domain379.getTree());

                    }
                    break;
                case 2 :
                    // Cubrid.g:724:4: SET STARTBRACKET domain_comma_list ENDBRACKET
                    {
                    root_0 = (Object)adaptor.nil();

                    SET380=(Token)input.LT(1);
                    match(input,SET,FOLLOW_SET_in_collections4051); 
                    SET380_tree = (Object)adaptor.create(SET380);
                    adaptor.addChild(root_0, SET380_tree);

                    STARTBRACKET381=(Token)input.LT(1);
                    match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_collections4053); 
                    STARTBRACKET381_tree = (Object)adaptor.create(STARTBRACKET381);
                    adaptor.addChild(root_0, STARTBRACKET381_tree);

                    pushFollow(FOLLOW_domain_comma_list_in_collections4055);
                    domain_comma_list382=domain_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, domain_comma_list382.getTree());
                    ENDBRACKET383=(Token)input.LT(1);
                    match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_collections4057); 
                    ENDBRACKET383_tree = (Object)adaptor.create(ENDBRACKET383);
                    adaptor.addChild(root_0, ENDBRACKET383_tree);


                    }
                    break;
                case 3 :
                    // Cubrid.g:725:4: LIST OR SEQUENCE domain
                    {
                    root_0 = (Object)adaptor.nil();

                    LIST384=(Token)input.LT(1);
                    match(input,LIST,FOLLOW_LIST_in_collections4062); 
                    LIST384_tree = (Object)adaptor.create(LIST384);
                    adaptor.addChild(root_0, LIST384_tree);

                    OR385=(Token)input.LT(1);
                    match(input,OR,FOLLOW_OR_in_collections4064); 
                    OR385_tree = (Object)adaptor.create(OR385);
                    adaptor.addChild(root_0, OR385_tree);

                    SEQUENCE386=(Token)input.LT(1);
                    match(input,SEQUENCE,FOLLOW_SEQUENCE_in_collections4066); 
                    SEQUENCE386_tree = (Object)adaptor.create(SEQUENCE386);
                    adaptor.addChild(root_0, SEQUENCE386_tree);

                    pushFollow(FOLLOW_domain_in_collections4068);
                    domain387=domain();
                    _fsp--;

                    adaptor.addChild(root_0, domain387.getTree());

                    }
                    break;
                case 4 :
                    // Cubrid.g:726:4: LIST OR SEQUENCE STARTBRACKET domain_comma_list ENDBRACKET
                    {
                    root_0 = (Object)adaptor.nil();

                    LIST388=(Token)input.LT(1);
                    match(input,LIST,FOLLOW_LIST_in_collections4073); 
                    LIST388_tree = (Object)adaptor.create(LIST388);
                    adaptor.addChild(root_0, LIST388_tree);

                    OR389=(Token)input.LT(1);
                    match(input,OR,FOLLOW_OR_in_collections4075); 
                    OR389_tree = (Object)adaptor.create(OR389);
                    adaptor.addChild(root_0, OR389_tree);

                    SEQUENCE390=(Token)input.LT(1);
                    match(input,SEQUENCE,FOLLOW_SEQUENCE_in_collections4077); 
                    SEQUENCE390_tree = (Object)adaptor.create(SEQUENCE390);
                    adaptor.addChild(root_0, SEQUENCE390_tree);

                    STARTBRACKET391=(Token)input.LT(1);
                    match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_collections4079); 
                    STARTBRACKET391_tree = (Object)adaptor.create(STARTBRACKET391);
                    adaptor.addChild(root_0, STARTBRACKET391_tree);

                    pushFollow(FOLLOW_domain_comma_list_in_collections4081);
                    domain_comma_list392=domain_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, domain_comma_list392.getTree());
                    ENDBRACKET393=(Token)input.LT(1);
                    match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_collections4083); 
                    ENDBRACKET393_tree = (Object)adaptor.create(ENDBRACKET393);
                    adaptor.addChild(root_0, ENDBRACKET393_tree);


                    }
                    break;
                case 5 :
                    // Cubrid.g:727:4: MULTISET domain
                    {
                    root_0 = (Object)adaptor.nil();

                    MULTISET394=(Token)input.LT(1);
                    match(input,MULTISET,FOLLOW_MULTISET_in_collections4088); 
                    MULTISET394_tree = (Object)adaptor.create(MULTISET394);
                    adaptor.addChild(root_0, MULTISET394_tree);

                    pushFollow(FOLLOW_domain_in_collections4090);
                    domain395=domain();
                    _fsp--;

                    adaptor.addChild(root_0, domain395.getTree());

                    }
                    break;
                case 6 :
                    // Cubrid.g:728:4: MULTISET STARTBRACKET domain_comma_list ENDBRACKET
                    {
                    root_0 = (Object)adaptor.nil();

                    MULTISET396=(Token)input.LT(1);
                    match(input,MULTISET,FOLLOW_MULTISET_in_collections4095); 
                    MULTISET396_tree = (Object)adaptor.create(MULTISET396);
                    adaptor.addChild(root_0, MULTISET396_tree);

                    STARTBRACKET397=(Token)input.LT(1);
                    match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_collections4097); 
                    STARTBRACKET397_tree = (Object)adaptor.create(STARTBRACKET397);
                    adaptor.addChild(root_0, STARTBRACKET397_tree);

                    pushFollow(FOLLOW_domain_comma_list_in_collections4099);
                    domain_comma_list398=domain_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, domain_comma_list398.getTree());
                    ENDBRACKET399=(Token)input.LT(1);
                    match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_collections4101); 
                    ENDBRACKET399_tree = (Object)adaptor.create(ENDBRACKET399);
                    adaptor.addChild(root_0, ENDBRACKET399_tree);


                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end collections

    public static class privative_type_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start privative_type
    // Cubrid.g:731:1: privative_type : ( char_ | varchar | nchar | ncharvarying | bit | bitvarying | numeric | integer_ | smallint | monetary | float_ | doubleprecision | date_ | time_ | timestamp | string | class_name | OBJECT );
    public final privative_type_return privative_type() throws RecognitionException {
        privative_type_return retval = new privative_type_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token OBJECT417=null;
        char__return char_400 = null;

        varchar_return varchar401 = null;

        nchar_return nchar402 = null;

        ncharvarying_return ncharvarying403 = null;

        bit_return bit404 = null;

        bitvarying_return bitvarying405 = null;

        numeric_return numeric406 = null;

        integer__return integer_407 = null;

        smallint_return smallint408 = null;

        monetary_return monetary409 = null;

        float__return float_410 = null;

        doubleprecision_return doubleprecision411 = null;

        date__return date_412 = null;

        time__return time_413 = null;

        timestamp_return timestamp414 = null;

        string_return string415 = null;

        class_name_return class_name416 = null;


        Object OBJECT417_tree=null;

        try {
            // Cubrid.g:731:15: ( char_ | varchar | nchar | ncharvarying | bit | bitvarying | numeric | integer_ | smallint | monetary | float_ | doubleprecision | date_ | time_ | timestamp | string | class_name | OBJECT )
            int alt109=18;
            switch ( input.LA(1) ) {
            case CHAR:
                {
                alt109=1;
                }
                break;
            case VARCHAR:
                {
                alt109=2;
                }
                break;
            case NCHAR:
                {
                int LA109_3 = input.LA(2);

                if ( (LA109_3==VARYING) ) {
                    alt109=4;
                }
                else if ( (LA109_3==AS||LA109_3==AUTO_INCREMENT||LA109_3==DEFAULT||LA109_3==FILE||LA109_3==FUNCTION||LA109_3==INHERIT||LA109_3==METHOD||LA109_3==NOT||LA109_3==PRIMARY||LA109_3==SHARE||LA109_3==UNIQUE||LA109_3==WITH||(LA109_3>=END && LA109_3<=COMMA)||LA109_3==ENDBRACKET||LA109_3==LENGTH) ) {
                    alt109=3;
                }
                else {
                    NoViableAltException nvae =
                        new NoViableAltException("731:1: privative_type : ( char_ | varchar | nchar | ncharvarying | bit | bitvarying | numeric | integer_ | smallint | monetary | float_ | doubleprecision | date_ | time_ | timestamp | string | class_name | OBJECT );", 109, 3, input);

                    throw nvae;
                }
                }
                break;
            case BIT:
                {
                int LA109_4 = input.LA(2);

                if ( (LA109_4==LENGTH) ) {
                    alt109=5;
                }
                else if ( (LA109_4==VARYING) ) {
                    alt109=6;
                }
                else {
                    NoViableAltException nvae =
                        new NoViableAltException("731:1: privative_type : ( char_ | varchar | nchar | ncharvarying | bit | bitvarying | numeric | integer_ | smallint | monetary | float_ | doubleprecision | date_ | time_ | timestamp | string | class_name | OBJECT );", 109, 4, input);

                    throw nvae;
                }
                }
                break;
            case DECIMAL:
            case NUMERIC:
                {
                alt109=7;
                }
                break;
            case INT:
            case INTEGER:
                {
                alt109=8;
                }
                break;
            case SMALLINT:
                {
                alt109=9;
                }
                break;
            case MONETARY:
                {
                alt109=10;
                }
                break;
            case FLOAT:
            case REAL:
                {
                alt109=11;
                }
                break;
            case DOUBLE:
                {
                alt109=12;
                }
                break;
            case DATE:
                {
                alt109=13;
                }
                break;
            case TIME:
                {
                alt109=14;
                }
                break;
            case TIMESTAMP:
                {
                alt109=15;
                }
                break;
            case STRING_STR:
                {
                alt109=16;
                }
                break;
            case ID:
            case COLUMN:
                {
                alt109=17;
                }
                break;
            case OBJECT:
                {
                alt109=18;
                }
                break;
            default:
                NoViableAltException nvae =
                    new NoViableAltException("731:1: privative_type : ( char_ | varchar | nchar | ncharvarying | bit | bitvarying | numeric | integer_ | smallint | monetary | float_ | doubleprecision | date_ | time_ | timestamp | string | class_name | OBJECT );", 109, 0, input);

                throw nvae;
            }

            switch (alt109) {
                case 1 :
                    // Cubrid.g:732:2: char_
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_char__in_privative_type4111);
                    char_400=char_();
                    _fsp--;

                    adaptor.addChild(root_0, char_400.getTree());

                    }
                    break;
                case 2 :
                    // Cubrid.g:733:4: varchar
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_varchar_in_privative_type4116);
                    varchar401=varchar();
                    _fsp--;

                    adaptor.addChild(root_0, varchar401.getTree());

                    }
                    break;
                case 3 :
                    // Cubrid.g:734:4: nchar
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_nchar_in_privative_type4121);
                    nchar402=nchar();
                    _fsp--;

                    adaptor.addChild(root_0, nchar402.getTree());

                    }
                    break;
                case 4 :
                    // Cubrid.g:735:4: ncharvarying
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_ncharvarying_in_privative_type4126);
                    ncharvarying403=ncharvarying();
                    _fsp--;

                    adaptor.addChild(root_0, ncharvarying403.getTree());

                    }
                    break;
                case 5 :
                    // Cubrid.g:736:4: bit
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_bit_in_privative_type4131);
                    bit404=bit();
                    _fsp--;

                    adaptor.addChild(root_0, bit404.getTree());

                    }
                    break;
                case 6 :
                    // Cubrid.g:737:4: bitvarying
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_bitvarying_in_privative_type4136);
                    bitvarying405=bitvarying();
                    _fsp--;

                    adaptor.addChild(root_0, bitvarying405.getTree());

                    }
                    break;
                case 7 :
                    // Cubrid.g:738:4: numeric
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_numeric_in_privative_type4141);
                    numeric406=numeric();
                    _fsp--;

                    adaptor.addChild(root_0, numeric406.getTree());

                    }
                    break;
                case 8 :
                    // Cubrid.g:739:4: integer_
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_integer__in_privative_type4147);
                    integer_407=integer_();
                    _fsp--;

                    adaptor.addChild(root_0, integer_407.getTree());

                    }
                    break;
                case 9 :
                    // Cubrid.g:740:4: smallint
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_smallint_in_privative_type4152);
                    smallint408=smallint();
                    _fsp--;

                    adaptor.addChild(root_0, smallint408.getTree());

                    }
                    break;
                case 10 :
                    // Cubrid.g:741:4: monetary
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_monetary_in_privative_type4157);
                    monetary409=monetary();
                    _fsp--;

                    adaptor.addChild(root_0, monetary409.getTree());

                    }
                    break;
                case 11 :
                    // Cubrid.g:742:4: float_
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_float__in_privative_type4162);
                    float_410=float_();
                    _fsp--;

                    adaptor.addChild(root_0, float_410.getTree());

                    }
                    break;
                case 12 :
                    // Cubrid.g:743:4: doubleprecision
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_doubleprecision_in_privative_type4167);
                    doubleprecision411=doubleprecision();
                    _fsp--;

                    adaptor.addChild(root_0, doubleprecision411.getTree());

                    }
                    break;
                case 13 :
                    // Cubrid.g:744:4: date_
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_date__in_privative_type4172);
                    date_412=date_();
                    _fsp--;

                    adaptor.addChild(root_0, date_412.getTree());

                    }
                    break;
                case 14 :
                    // Cubrid.g:745:4: time_
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_time__in_privative_type4177);
                    time_413=time_();
                    _fsp--;

                    adaptor.addChild(root_0, time_413.getTree());

                    }
                    break;
                case 15 :
                    // Cubrid.g:746:4: timestamp
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_timestamp_in_privative_type4182);
                    timestamp414=timestamp();
                    _fsp--;

                    adaptor.addChild(root_0, timestamp414.getTree());

                    }
                    break;
                case 16 :
                    // Cubrid.g:747:4: string
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_string_in_privative_type4187);
                    string415=string();
                    _fsp--;

                    adaptor.addChild(root_0, string415.getTree());

                    }
                    break;
                case 17 :
                    // Cubrid.g:748:4: class_name
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_class_name_in_privative_type4192);
                    class_name416=class_name();
                    _fsp--;

                    adaptor.addChild(root_0, class_name416.getTree());

                    }
                    break;
                case 18 :
                    // Cubrid.g:749:4: OBJECT
                    {
                    root_0 = (Object)adaptor.nil();

                    OBJECT417=(Token)input.LT(1);
                    match(input,OBJECT,FOLLOW_OBJECT_in_privative_type4197); 
                    OBJECT417_tree = (Object)adaptor.create(OBJECT417);
                    adaptor.addChild(root_0, OBJECT417_tree);


                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end privative_type

    public static class string_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start string
    // Cubrid.g:752:1: string : STRING_STR ;
    public final string_return string() throws RecognitionException {
        string_return retval = new string_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token STRING_STR418=null;

        Object STRING_STR418_tree=null;

        try {
            // Cubrid.g:752:7: ( STRING_STR )
            // Cubrid.g:753:2: STRING_STR
            {
            root_0 = (Object)adaptor.nil();

            STRING_STR418=(Token)input.LT(1);
            match(input,STRING_STR,FOLLOW_STRING_STR_in_string4207); 
            STRING_STR418_tree = (Object)adaptor.create(STRING_STR418);
            adaptor.addChild(root_0, STRING_STR418_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end string

    public static class char__return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start char_
    // Cubrid.g:756:1: char_ : CHAR ( LENGTH )? ;
    public final char__return char_() throws RecognitionException {
        char__return retval = new char__return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token CHAR419=null;
        Token LENGTH420=null;

        Object CHAR419_tree=null;
        Object LENGTH420_tree=null;

        try {
            // Cubrid.g:756:6: ( CHAR ( LENGTH )? )
            // Cubrid.g:757:2: CHAR ( LENGTH )?
            {
            root_0 = (Object)adaptor.nil();

            CHAR419=(Token)input.LT(1);
            match(input,CHAR,FOLLOW_CHAR_in_char_4219); 
            CHAR419_tree = (Object)adaptor.create(CHAR419);
            adaptor.addChild(root_0, CHAR419_tree);

            // Cubrid.g:757:7: ( LENGTH )?
            int alt110=2;
            int LA110_0 = input.LA(1);

            if ( (LA110_0==LENGTH) ) {
                alt110=1;
            }
            switch (alt110) {
                case 1 :
                    // Cubrid.g:757:7: LENGTH
                    {
                    LENGTH420=(Token)input.LT(1);
                    match(input,LENGTH,FOLLOW_LENGTH_in_char_4221); 
                    LENGTH420_tree = (Object)adaptor.create(LENGTH420);
                    adaptor.addChild(root_0, LENGTH420_tree);


                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end char_

    public static class nchar_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start nchar
    // Cubrid.g:760:1: nchar : NCHAR ( LENGTH )? ;
    public final nchar_return nchar() throws RecognitionException {
        nchar_return retval = new nchar_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token NCHAR421=null;
        Token LENGTH422=null;

        Object NCHAR421_tree=null;
        Object LENGTH422_tree=null;

        try {
            // Cubrid.g:760:6: ( NCHAR ( LENGTH )? )
            // Cubrid.g:761:2: NCHAR ( LENGTH )?
            {
            root_0 = (Object)adaptor.nil();

            NCHAR421=(Token)input.LT(1);
            match(input,NCHAR,FOLLOW_NCHAR_in_nchar4233); 
            NCHAR421_tree = (Object)adaptor.create(NCHAR421);
            adaptor.addChild(root_0, NCHAR421_tree);

            // Cubrid.g:761:8: ( LENGTH )?
            int alt111=2;
            int LA111_0 = input.LA(1);

            if ( (LA111_0==LENGTH) ) {
                alt111=1;
            }
            switch (alt111) {
                case 1 :
                    // Cubrid.g:761:8: LENGTH
                    {
                    LENGTH422=(Token)input.LT(1);
                    match(input,LENGTH,FOLLOW_LENGTH_in_nchar4235); 
                    LENGTH422_tree = (Object)adaptor.create(LENGTH422);
                    adaptor.addChild(root_0, LENGTH422_tree);


                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end nchar

    public static class varchar_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start varchar
    // Cubrid.g:764:1: varchar : VARCHAR LENGTH ;
    public final varchar_return varchar() throws RecognitionException {
        varchar_return retval = new varchar_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token VARCHAR423=null;
        Token LENGTH424=null;

        Object VARCHAR423_tree=null;
        Object LENGTH424_tree=null;

        try {
            // Cubrid.g:764:8: ( VARCHAR LENGTH )
            // Cubrid.g:765:2: VARCHAR LENGTH
            {
            root_0 = (Object)adaptor.nil();

            VARCHAR423=(Token)input.LT(1);
            match(input,VARCHAR,FOLLOW_VARCHAR_in_varchar4247); 
            VARCHAR423_tree = (Object)adaptor.create(VARCHAR423);
            adaptor.addChild(root_0, VARCHAR423_tree);

            LENGTH424=(Token)input.LT(1);
            match(input,LENGTH,FOLLOW_LENGTH_in_varchar4249); 
            LENGTH424_tree = (Object)adaptor.create(LENGTH424);
            adaptor.addChild(root_0, LENGTH424_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end varchar

    public static class ncharvarying_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start ncharvarying
    // Cubrid.g:768:1: ncharvarying : NCHAR VARYING LENGTH ;
    public final ncharvarying_return ncharvarying() throws RecognitionException {
        ncharvarying_return retval = new ncharvarying_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token NCHAR425=null;
        Token VARYING426=null;
        Token LENGTH427=null;

        Object NCHAR425_tree=null;
        Object VARYING426_tree=null;
        Object LENGTH427_tree=null;

        try {
            // Cubrid.g:768:13: ( NCHAR VARYING LENGTH )
            // Cubrid.g:769:2: NCHAR VARYING LENGTH
            {
            root_0 = (Object)adaptor.nil();

            NCHAR425=(Token)input.LT(1);
            match(input,NCHAR,FOLLOW_NCHAR_in_ncharvarying4261); 
            NCHAR425_tree = (Object)adaptor.create(NCHAR425);
            adaptor.addChild(root_0, NCHAR425_tree);

            VARYING426=(Token)input.LT(1);
            match(input,VARYING,FOLLOW_VARYING_in_ncharvarying4263); 
            VARYING426_tree = (Object)adaptor.create(VARYING426);
            adaptor.addChild(root_0, VARYING426_tree);

            LENGTH427=(Token)input.LT(1);
            match(input,LENGTH,FOLLOW_LENGTH_in_ncharvarying4265); 
            LENGTH427_tree = (Object)adaptor.create(LENGTH427);
            adaptor.addChild(root_0, LENGTH427_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end ncharvarying

    public static class bit_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start bit
    // Cubrid.g:772:1: bit : BIT LENGTH ;
    public final bit_return bit() throws RecognitionException {
        bit_return retval = new bit_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token BIT428=null;
        Token LENGTH429=null;

        Object BIT428_tree=null;
        Object LENGTH429_tree=null;

        try {
            // Cubrid.g:772:4: ( BIT LENGTH )
            // Cubrid.g:773:2: BIT LENGTH
            {
            root_0 = (Object)adaptor.nil();

            BIT428=(Token)input.LT(1);
            match(input,BIT,FOLLOW_BIT_in_bit4277); 
            BIT428_tree = (Object)adaptor.create(BIT428);
            adaptor.addChild(root_0, BIT428_tree);

            LENGTH429=(Token)input.LT(1);
            match(input,LENGTH,FOLLOW_LENGTH_in_bit4279); 
            LENGTH429_tree = (Object)adaptor.create(LENGTH429);
            adaptor.addChild(root_0, LENGTH429_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end bit

    public static class bitvarying_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start bitvarying
    // Cubrid.g:776:1: bitvarying : BIT VARYING LENGTH ;
    public final bitvarying_return bitvarying() throws RecognitionException {
        bitvarying_return retval = new bitvarying_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token BIT430=null;
        Token VARYING431=null;
        Token LENGTH432=null;

        Object BIT430_tree=null;
        Object VARYING431_tree=null;
        Object LENGTH432_tree=null;

        try {
            // Cubrid.g:776:11: ( BIT VARYING LENGTH )
            // Cubrid.g:777:2: BIT VARYING LENGTH
            {
            root_0 = (Object)adaptor.nil();

            BIT430=(Token)input.LT(1);
            match(input,BIT,FOLLOW_BIT_in_bitvarying4290); 
            BIT430_tree = (Object)adaptor.create(BIT430);
            adaptor.addChild(root_0, BIT430_tree);

            VARYING431=(Token)input.LT(1);
            match(input,VARYING,FOLLOW_VARYING_in_bitvarying4292); 
            VARYING431_tree = (Object)adaptor.create(VARYING431);
            adaptor.addChild(root_0, VARYING431_tree);

            LENGTH432=(Token)input.LT(1);
            match(input,LENGTH,FOLLOW_LENGTH_in_bitvarying4294); 
            LENGTH432_tree = (Object)adaptor.create(LENGTH432);
            adaptor.addChild(root_0, LENGTH432_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end bitvarying

    public static class numeric_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start numeric
    // Cubrid.g:780:1: numeric : ( NUMERIC | DECIMAL ) ( STARTBRACKET DECIMALLITERAL ( COMMA DECIMALLITERAL )? ENDBRACKET )? ;
    public final numeric_return numeric() throws RecognitionException {
        numeric_return retval = new numeric_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set433=null;
        Token STARTBRACKET434=null;
        Token DECIMALLITERAL435=null;
        Token COMMA436=null;
        Token DECIMALLITERAL437=null;
        Token ENDBRACKET438=null;

        Object set433_tree=null;
        Object STARTBRACKET434_tree=null;
        Object DECIMALLITERAL435_tree=null;
        Object COMMA436_tree=null;
        Object DECIMALLITERAL437_tree=null;
        Object ENDBRACKET438_tree=null;

        try {
            // Cubrid.g:780:8: ( ( NUMERIC | DECIMAL ) ( STARTBRACKET DECIMALLITERAL ( COMMA DECIMALLITERAL )? ENDBRACKET )? )
            // Cubrid.g:781:3: ( NUMERIC | DECIMAL ) ( STARTBRACKET DECIMALLITERAL ( COMMA DECIMALLITERAL )? ENDBRACKET )?
            {
            root_0 = (Object)adaptor.nil();

            set433=(Token)input.LT(1);
            if ( input.LA(1)==DECIMAL||input.LA(1)==NUMERIC ) {
                input.consume();
                adaptor.addChild(root_0, adaptor.create(set433));
                errorRecovery=false;
            }
            else {
                MismatchedSetException mse =
                    new MismatchedSetException(null,input);
                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_numeric4306);    throw mse;
            }

            // Cubrid.g:782:3: ( STARTBRACKET DECIMALLITERAL ( COMMA DECIMALLITERAL )? ENDBRACKET )?
            int alt113=2;
            int LA113_0 = input.LA(1);

            if ( (LA113_0==STARTBRACKET) ) {
                alt113=1;
            }
            switch (alt113) {
                case 1 :
                    // Cubrid.g:782:5: STARTBRACKET DECIMALLITERAL ( COMMA DECIMALLITERAL )? ENDBRACKET
                    {
                    STARTBRACKET434=(Token)input.LT(1);
                    match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_numeric4319); 
                    STARTBRACKET434_tree = (Object)adaptor.create(STARTBRACKET434);
                    adaptor.addChild(root_0, STARTBRACKET434_tree);

                    DECIMALLITERAL435=(Token)input.LT(1);
                    match(input,DECIMALLITERAL,FOLLOW_DECIMALLITERAL_in_numeric4321); 
                    DECIMALLITERAL435_tree = (Object)adaptor.create(DECIMALLITERAL435);
                    adaptor.addChild(root_0, DECIMALLITERAL435_tree);

                    // Cubrid.g:782:33: ( COMMA DECIMALLITERAL )?
                    int alt112=2;
                    int LA112_0 = input.LA(1);

                    if ( (LA112_0==COMMA) ) {
                        alt112=1;
                    }
                    switch (alt112) {
                        case 1 :
                            // Cubrid.g:782:34: COMMA DECIMALLITERAL
                            {
                            COMMA436=(Token)input.LT(1);
                            match(input,COMMA,FOLLOW_COMMA_in_numeric4324); 
                            COMMA436_tree = (Object)adaptor.create(COMMA436);
                            adaptor.addChild(root_0, COMMA436_tree);

                            DECIMALLITERAL437=(Token)input.LT(1);
                            match(input,DECIMALLITERAL,FOLLOW_DECIMALLITERAL_in_numeric4326); 
                            DECIMALLITERAL437_tree = (Object)adaptor.create(DECIMALLITERAL437);
                            adaptor.addChild(root_0, DECIMALLITERAL437_tree);


                            }
                            break;

                    }

                    ENDBRACKET438=(Token)input.LT(1);
                    match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_numeric4331); 
                    ENDBRACKET438_tree = (Object)adaptor.create(ENDBRACKET438);
                    adaptor.addChild(root_0, ENDBRACKET438_tree);


                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end numeric

    public static class integer__return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start integer_
    // Cubrid.g:785:1: integer_ : ( INTEGER | INT );
    public final integer__return integer_() throws RecognitionException {
        integer__return retval = new integer__return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set439=null;

        Object set439_tree=null;

        try {
            // Cubrid.g:785:9: ( INTEGER | INT )
            // Cubrid.g:
            {
            root_0 = (Object)adaptor.nil();

            set439=(Token)input.LT(1);
            if ( (input.LA(1)>=INT && input.LA(1)<=INTEGER) ) {
                input.consume();
                adaptor.addChild(root_0, adaptor.create(set439));
                errorRecovery=false;
            }
            else {
                MismatchedSetException mse =
                    new MismatchedSetException(null,input);
                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_integer_0);    throw mse;
            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end integer_

    public static class smallint_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start smallint
    // Cubrid.g:789:1: smallint : SMALLINT ;
    public final smallint_return smallint() throws RecognitionException {
        smallint_return retval = new smallint_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token SMALLINT440=null;

        Object SMALLINT440_tree=null;

        try {
            // Cubrid.g:789:9: ( SMALLINT )
            // Cubrid.g:790:2: SMALLINT
            {
            root_0 = (Object)adaptor.nil();

            SMALLINT440=(Token)input.LT(1);
            match(input,SMALLINT,FOLLOW_SMALLINT_in_smallint4362); 
            SMALLINT440_tree = (Object)adaptor.create(SMALLINT440);
            adaptor.addChild(root_0, SMALLINT440_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end smallint

    public static class monetary_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start monetary
    // Cubrid.g:793:1: monetary : MONETARY ;
    public final monetary_return monetary() throws RecognitionException {
        monetary_return retval = new monetary_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token MONETARY441=null;

        Object MONETARY441_tree=null;

        try {
            // Cubrid.g:793:9: ( MONETARY )
            // Cubrid.g:794:2: MONETARY
            {
            root_0 = (Object)adaptor.nil();

            MONETARY441=(Token)input.LT(1);
            match(input,MONETARY,FOLLOW_MONETARY_in_monetary4373); 
            MONETARY441_tree = (Object)adaptor.create(MONETARY441);
            adaptor.addChild(root_0, MONETARY441_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end monetary

    public static class float__return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start float_
    // Cubrid.g:797:1: float_ : ( FLOAT | REAL ) ( STARTBRACKET DECIMALLITERAL ENDBRACKET )? ;
    public final float__return float_() throws RecognitionException {
        float__return retval = new float__return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set442=null;
        Token STARTBRACKET443=null;
        Token DECIMALLITERAL444=null;
        Token ENDBRACKET445=null;

        Object set442_tree=null;
        Object STARTBRACKET443_tree=null;
        Object DECIMALLITERAL444_tree=null;
        Object ENDBRACKET445_tree=null;

        try {
            // Cubrid.g:797:7: ( ( FLOAT | REAL ) ( STARTBRACKET DECIMALLITERAL ENDBRACKET )? )
            // Cubrid.g:798:2: ( FLOAT | REAL ) ( STARTBRACKET DECIMALLITERAL ENDBRACKET )?
            {
            root_0 = (Object)adaptor.nil();

            set442=(Token)input.LT(1);
            if ( input.LA(1)==FLOAT||input.LA(1)==REAL ) {
                input.consume();
                adaptor.addChild(root_0, adaptor.create(set442));
                errorRecovery=false;
            }
            else {
                MismatchedSetException mse =
                    new MismatchedSetException(null,input);
                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_float_4384);    throw mse;
            }

            // Cubrid.g:799:2: ( STARTBRACKET DECIMALLITERAL ENDBRACKET )?
            int alt114=2;
            int LA114_0 = input.LA(1);

            if ( (LA114_0==STARTBRACKET) ) {
                alt114=1;
            }
            switch (alt114) {
                case 1 :
                    // Cubrid.g:799:3: STARTBRACKET DECIMALLITERAL ENDBRACKET
                    {
                    STARTBRACKET443=(Token)input.LT(1);
                    match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_float_4394); 
                    STARTBRACKET443_tree = (Object)adaptor.create(STARTBRACKET443);
                    adaptor.addChild(root_0, STARTBRACKET443_tree);

                    DECIMALLITERAL444=(Token)input.LT(1);
                    match(input,DECIMALLITERAL,FOLLOW_DECIMALLITERAL_in_float_4396); 
                    DECIMALLITERAL444_tree = (Object)adaptor.create(DECIMALLITERAL444);
                    adaptor.addChild(root_0, DECIMALLITERAL444_tree);

                    ENDBRACKET445=(Token)input.LT(1);
                    match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_float_4398); 
                    ENDBRACKET445_tree = (Object)adaptor.create(ENDBRACKET445);
                    adaptor.addChild(root_0, ENDBRACKET445_tree);


                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end float_

    public static class doubleprecision_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start doubleprecision
    // Cubrid.g:802:1: doubleprecision : DOUBLE PRECISION ;
    public final doubleprecision_return doubleprecision() throws RecognitionException {
        doubleprecision_return retval = new doubleprecision_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token DOUBLE446=null;
        Token PRECISION447=null;

        Object DOUBLE446_tree=null;
        Object PRECISION447_tree=null;

        try {
            // Cubrid.g:802:16: ( DOUBLE PRECISION )
            // Cubrid.g:803:2: DOUBLE PRECISION
            {
            root_0 = (Object)adaptor.nil();

            DOUBLE446=(Token)input.LT(1);
            match(input,DOUBLE,FOLLOW_DOUBLE_in_doubleprecision4411); 
            DOUBLE446_tree = (Object)adaptor.create(DOUBLE446);
            adaptor.addChild(root_0, DOUBLE446_tree);

            PRECISION447=(Token)input.LT(1);
            match(input,PRECISION,FOLLOW_PRECISION_in_doubleprecision4413); 
            PRECISION447_tree = (Object)adaptor.create(PRECISION447);
            adaptor.addChild(root_0, PRECISION447_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end doubleprecision

    public static class date__return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start date_
    // Cubrid.g:806:1: date_ : DATE ( QUOTA DATE_FORMAT QUOTA )? ;
    public final date__return date_() throws RecognitionException {
        date__return retval = new date__return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token DATE448=null;
        Token QUOTA449=null;
        Token DATE_FORMAT450=null;
        Token QUOTA451=null;

        Object DATE448_tree=null;
        Object QUOTA449_tree=null;
        Object DATE_FORMAT450_tree=null;
        Object QUOTA451_tree=null;

        try {
            // Cubrid.g:806:6: ( DATE ( QUOTA DATE_FORMAT QUOTA )? )
            // Cubrid.g:807:2: DATE ( QUOTA DATE_FORMAT QUOTA )?
            {
            root_0 = (Object)adaptor.nil();

            DATE448=(Token)input.LT(1);
            match(input,DATE,FOLLOW_DATE_in_date_4424); 
            DATE448_tree = (Object)adaptor.create(DATE448);
            adaptor.addChild(root_0, DATE448_tree);

            // Cubrid.g:808:2: ( QUOTA DATE_FORMAT QUOTA )?
            int alt115=2;
            int LA115_0 = input.LA(1);

            if ( (LA115_0==QUOTA) ) {
                alt115=1;
            }
            switch (alt115) {
                case 1 :
                    // Cubrid.g:808:3: QUOTA DATE_FORMAT QUOTA
                    {
                    QUOTA449=(Token)input.LT(1);
                    match(input,QUOTA,FOLLOW_QUOTA_in_date_4429); 
                    QUOTA449_tree = (Object)adaptor.create(QUOTA449);
                    adaptor.addChild(root_0, QUOTA449_tree);

                    DATE_FORMAT450=(Token)input.LT(1);
                    match(input,DATE_FORMAT,FOLLOW_DATE_FORMAT_in_date_4431); 
                    DATE_FORMAT450_tree = (Object)adaptor.create(DATE_FORMAT450);
                    adaptor.addChild(root_0, DATE_FORMAT450_tree);

                    QUOTA451=(Token)input.LT(1);
                    match(input,QUOTA,FOLLOW_QUOTA_in_date_4433); 
                    QUOTA451_tree = (Object)adaptor.create(QUOTA451);
                    adaptor.addChild(root_0, QUOTA451_tree);


                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end date_

    public static class time__return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start time_
    // Cubrid.g:818:1: time_ : TIME ( QUOTA TIME_FORMAT QUOTA )? ;
    public final time__return time_() throws RecognitionException {
        time__return retval = new time__return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token TIME452=null;
        Token QUOTA453=null;
        Token TIME_FORMAT454=null;
        Token QUOTA455=null;

        Object TIME452_tree=null;
        Object QUOTA453_tree=null;
        Object TIME_FORMAT454_tree=null;
        Object QUOTA455_tree=null;

        try {
            // Cubrid.g:818:6: ( TIME ( QUOTA TIME_FORMAT QUOTA )? )
            // Cubrid.g:819:2: TIME ( QUOTA TIME_FORMAT QUOTA )?
            {
            root_0 = (Object)adaptor.nil();

            TIME452=(Token)input.LT(1);
            match(input,TIME,FOLLOW_TIME_in_time_4564); 
            TIME452_tree = (Object)adaptor.create(TIME452);
            adaptor.addChild(root_0, TIME452_tree);

            // Cubrid.g:820:2: ( QUOTA TIME_FORMAT QUOTA )?
            int alt116=2;
            int LA116_0 = input.LA(1);

            if ( (LA116_0==QUOTA) ) {
                alt116=1;
            }
            switch (alt116) {
                case 1 :
                    // Cubrid.g:820:3: QUOTA TIME_FORMAT QUOTA
                    {
                    QUOTA453=(Token)input.LT(1);
                    match(input,QUOTA,FOLLOW_QUOTA_in_time_4569); 
                    QUOTA453_tree = (Object)adaptor.create(QUOTA453);
                    adaptor.addChild(root_0, QUOTA453_tree);

                    TIME_FORMAT454=(Token)input.LT(1);
                    match(input,TIME_FORMAT,FOLLOW_TIME_FORMAT_in_time_4571); 
                    TIME_FORMAT454_tree = (Object)adaptor.create(TIME_FORMAT454);
                    adaptor.addChild(root_0, TIME_FORMAT454_tree);

                    QUOTA455=(Token)input.LT(1);
                    match(input,QUOTA,FOLLOW_QUOTA_in_time_4573); 
                    QUOTA455_tree = (Object)adaptor.create(QUOTA455);
                    adaptor.addChild(root_0, QUOTA455_tree);


                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end time_

    public static class timestamp_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start timestamp
    // Cubrid.g:823:1: timestamp : TIMESTAMP ( QUOTA ( ( DATE_FORMAT TIME_FORMAT ) | ( TIME_FORMAT DATE_FORMAT ) ) QUOTA )? ;
    public final timestamp_return timestamp() throws RecognitionException {
        timestamp_return retval = new timestamp_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token TIMESTAMP456=null;
        Token QUOTA457=null;
        Token DATE_FORMAT458=null;
        Token TIME_FORMAT459=null;
        Token TIME_FORMAT460=null;
        Token DATE_FORMAT461=null;
        Token QUOTA462=null;

        Object TIMESTAMP456_tree=null;
        Object QUOTA457_tree=null;
        Object DATE_FORMAT458_tree=null;
        Object TIME_FORMAT459_tree=null;
        Object TIME_FORMAT460_tree=null;
        Object DATE_FORMAT461_tree=null;
        Object QUOTA462_tree=null;

        try {
            // Cubrid.g:823:10: ( TIMESTAMP ( QUOTA ( ( DATE_FORMAT TIME_FORMAT ) | ( TIME_FORMAT DATE_FORMAT ) ) QUOTA )? )
            // Cubrid.g:824:2: TIMESTAMP ( QUOTA ( ( DATE_FORMAT TIME_FORMAT ) | ( TIME_FORMAT DATE_FORMAT ) ) QUOTA )?
            {
            root_0 = (Object)adaptor.nil();

            TIMESTAMP456=(Token)input.LT(1);
            match(input,TIMESTAMP,FOLLOW_TIMESTAMP_in_timestamp4586); 
            TIMESTAMP456_tree = (Object)adaptor.create(TIMESTAMP456);
            adaptor.addChild(root_0, TIMESTAMP456_tree);

            // Cubrid.g:825:2: ( QUOTA ( ( DATE_FORMAT TIME_FORMAT ) | ( TIME_FORMAT DATE_FORMAT ) ) QUOTA )?
            int alt118=2;
            int LA118_0 = input.LA(1);

            if ( (LA118_0==QUOTA) ) {
                alt118=1;
            }
            switch (alt118) {
                case 1 :
                    // Cubrid.g:825:3: QUOTA ( ( DATE_FORMAT TIME_FORMAT ) | ( TIME_FORMAT DATE_FORMAT ) ) QUOTA
                    {
                    QUOTA457=(Token)input.LT(1);
                    match(input,QUOTA,FOLLOW_QUOTA_in_timestamp4590); 
                    QUOTA457_tree = (Object)adaptor.create(QUOTA457);
                    adaptor.addChild(root_0, QUOTA457_tree);

                    // Cubrid.g:826:3: ( ( DATE_FORMAT TIME_FORMAT ) | ( TIME_FORMAT DATE_FORMAT ) )
                    int alt117=2;
                    int LA117_0 = input.LA(1);

                    if ( (LA117_0==DATE_FORMAT) ) {
                        alt117=1;
                    }
                    else if ( (LA117_0==TIME_FORMAT) ) {
                        alt117=2;
                    }
                    else {
                        NoViableAltException nvae =
                            new NoViableAltException("826:3: ( ( DATE_FORMAT TIME_FORMAT ) | ( TIME_FORMAT DATE_FORMAT ) )", 117, 0, input);

                        throw nvae;
                    }
                    switch (alt117) {
                        case 1 :
                            // Cubrid.g:826:5: ( DATE_FORMAT TIME_FORMAT )
                            {
                            // Cubrid.g:826:5: ( DATE_FORMAT TIME_FORMAT )
                            // Cubrid.g:826:6: DATE_FORMAT TIME_FORMAT
                            {
                            DATE_FORMAT458=(Token)input.LT(1);
                            match(input,DATE_FORMAT,FOLLOW_DATE_FORMAT_in_timestamp4597); 
                            DATE_FORMAT458_tree = (Object)adaptor.create(DATE_FORMAT458);
                            adaptor.addChild(root_0, DATE_FORMAT458_tree);

                            TIME_FORMAT459=(Token)input.LT(1);
                            match(input,TIME_FORMAT,FOLLOW_TIME_FORMAT_in_timestamp4599); 
                            TIME_FORMAT459_tree = (Object)adaptor.create(TIME_FORMAT459);
                            adaptor.addChild(root_0, TIME_FORMAT459_tree);


                            }


                            }
                            break;
                        case 2 :
                            // Cubrid.g:826:33: ( TIME_FORMAT DATE_FORMAT )
                            {
                            // Cubrid.g:826:33: ( TIME_FORMAT DATE_FORMAT )
                            // Cubrid.g:826:34: TIME_FORMAT DATE_FORMAT
                            {
                            TIME_FORMAT460=(Token)input.LT(1);
                            match(input,TIME_FORMAT,FOLLOW_TIME_FORMAT_in_timestamp4605); 
                            TIME_FORMAT460_tree = (Object)adaptor.create(TIME_FORMAT460);
                            adaptor.addChild(root_0, TIME_FORMAT460_tree);

                            DATE_FORMAT461=(Token)input.LT(1);
                            match(input,DATE_FORMAT,FOLLOW_DATE_FORMAT_in_timestamp4607); 
                            DATE_FORMAT461_tree = (Object)adaptor.create(DATE_FORMAT461);
                            adaptor.addChild(root_0, DATE_FORMAT461_tree);


                            }


                            }
                            break;

                    }

                    QUOTA462=(Token)input.LT(1);
                    match(input,QUOTA,FOLLOW_QUOTA_in_timestamp4613); 
                    QUOTA462_tree = (Object)adaptor.create(QUOTA462);
                    adaptor.addChild(root_0, QUOTA462_tree);


                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end timestamp

    public static class default_or_shared_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start default_or_shared
    // Cubrid.g:834:1: default_or_shared : ( SHARE ( value_specification )? | DEFAULT value_specification );
    public final default_or_shared_return default_or_shared() throws RecognitionException {
        default_or_shared_return retval = new default_or_shared_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token SHARE463=null;
        Token DEFAULT465=null;
        value_specification_return value_specification464 = null;

        value_specification_return value_specification466 = null;


        Object SHARE463_tree=null;
        Object DEFAULT465_tree=null;

        try {
            // Cubrid.g:834:18: ( SHARE ( value_specification )? | DEFAULT value_specification )
            int alt120=2;
            int LA120_0 = input.LA(1);

            if ( (LA120_0==SHARE) ) {
                alt120=1;
            }
            else if ( (LA120_0==DEFAULT) ) {
                alt120=2;
            }
            else {
                NoViableAltException nvae =
                    new NoViableAltException("834:1: default_or_shared : ( SHARE ( value_specification )? | DEFAULT value_specification );", 120, 0, input);

                throw nvae;
            }
            switch (alt120) {
                case 1 :
                    // Cubrid.g:835:2: SHARE ( value_specification )?
                    {
                    root_0 = (Object)adaptor.nil();

                    SHARE463=(Token)input.LT(1);
                    match(input,SHARE,FOLLOW_SHARE_in_default_or_shared4648); 
                    SHARE463_tree = (Object)adaptor.create(SHARE463);
                    adaptor.addChild(root_0, SHARE463_tree);

                    // Cubrid.g:835:8: ( value_specification )?
                    int alt119=2;
                    int LA119_0 = input.LA(1);

                    if ( (LA119_0==QUOTA||(LA119_0>=DOLLAR && LA119_0<=Q_MARK)||LA119_0==DECIMALLITERAL||LA119_0==STRING||LA119_0==172) ) {
                        alt119=1;
                    }
                    switch (alt119) {
                        case 1 :
                            // Cubrid.g:835:8: value_specification
                            {
                            pushFollow(FOLLOW_value_specification_in_default_or_shared4650);
                            value_specification464=value_specification();
                            _fsp--;

                            adaptor.addChild(root_0, value_specification464.getTree());

                            }
                            break;

                    }


                    }
                    break;
                case 2 :
                    // Cubrid.g:836:3: DEFAULT value_specification
                    {
                    root_0 = (Object)adaptor.nil();

                    DEFAULT465=(Token)input.LT(1);
                    match(input,DEFAULT,FOLLOW_DEFAULT_in_default_or_shared4655); 
                    DEFAULT465_tree = (Object)adaptor.create(DEFAULT465);
                    adaptor.addChild(root_0, DEFAULT465_tree);

                    pushFollow(FOLLOW_value_specification_in_default_or_shared4657);
                    value_specification466=value_specification();
                    _fsp--;

                    adaptor.addChild(root_0, value_specification466.getTree());

                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end default_or_shared

    public static class attribute_constraint_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start attribute_constraint_list
    // Cubrid.g:839:1: attribute_constraint_list : ( attribute_constraint )+ ;
    public final attribute_constraint_list_return attribute_constraint_list() throws RecognitionException {
        attribute_constraint_list_return retval = new attribute_constraint_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        attribute_constraint_return attribute_constraint467 = null;



        try {
            // Cubrid.g:839:26: ( ( attribute_constraint )+ )
            // Cubrid.g:840:2: ( attribute_constraint )+
            {
            root_0 = (Object)adaptor.nil();

            // Cubrid.g:840:2: ( attribute_constraint )+
            int cnt121=0;
            loop121:
            do {
                int alt121=2;
                int LA121_0 = input.LA(1);

                if ( (LA121_0==NOT||LA121_0==PRIMARY||LA121_0==UNIQUE) ) {
                    alt121=1;
                }


                switch (alt121) {
            	case 1 :
            	    // Cubrid.g:840:2: attribute_constraint
            	    {
            	    pushFollow(FOLLOW_attribute_constraint_in_attribute_constraint_list4668);
            	    attribute_constraint467=attribute_constraint();
            	    _fsp--;

            	    adaptor.addChild(root_0, attribute_constraint467.getTree());

            	    }
            	    break;

            	default :
            	    if ( cnt121 >= 1 ) break loop121;
                        EarlyExitException eee =
                            new EarlyExitException(121, input);
                        throw eee;
                }
                cnt121++;
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end attribute_constraint_list

    public static class attribute_constraint_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start attribute_constraint
    // Cubrid.g:843:1: attribute_constraint : ( NOT NULL | UNIQUE | PRIMARY KEY );
    public final attribute_constraint_return attribute_constraint() throws RecognitionException {
        attribute_constraint_return retval = new attribute_constraint_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token NOT468=null;
        Token NULL469=null;
        Token UNIQUE470=null;
        Token PRIMARY471=null;
        Token KEY472=null;

        Object NOT468_tree=null;
        Object NULL469_tree=null;
        Object UNIQUE470_tree=null;
        Object PRIMARY471_tree=null;
        Object KEY472_tree=null;

        try {
            // Cubrid.g:843:21: ( NOT NULL | UNIQUE | PRIMARY KEY )
            int alt122=3;
            switch ( input.LA(1) ) {
            case NOT:
                {
                alt122=1;
                }
                break;
            case UNIQUE:
                {
                alt122=2;
                }
                break;
            case PRIMARY:
                {
                alt122=3;
                }
                break;
            default:
                NoViableAltException nvae =
                    new NoViableAltException("843:1: attribute_constraint : ( NOT NULL | UNIQUE | PRIMARY KEY );", 122, 0, input);

                throw nvae;
            }

            switch (alt122) {
                case 1 :
                    // Cubrid.g:844:2: NOT NULL
                    {
                    root_0 = (Object)adaptor.nil();

                    NOT468=(Token)input.LT(1);
                    match(input,NOT,FOLLOW_NOT_in_attribute_constraint4680); 
                    NOT468_tree = (Object)adaptor.create(NOT468);
                    adaptor.addChild(root_0, NOT468_tree);

                    NULL469=(Token)input.LT(1);
                    match(input,NULL,FOLLOW_NULL_in_attribute_constraint4682); 
                    NULL469_tree = (Object)adaptor.create(NULL469);
                    adaptor.addChild(root_0, NULL469_tree);


                    }
                    break;
                case 2 :
                    // Cubrid.g:845:4: UNIQUE
                    {
                    root_0 = (Object)adaptor.nil();

                    UNIQUE470=(Token)input.LT(1);
                    match(input,UNIQUE,FOLLOW_UNIQUE_in_attribute_constraint4687); 
                    UNIQUE470_tree = (Object)adaptor.create(UNIQUE470);
                    adaptor.addChild(root_0, UNIQUE470_tree);


                    }
                    break;
                case 3 :
                    // Cubrid.g:846:4: PRIMARY KEY
                    {
                    root_0 = (Object)adaptor.nil();

                    PRIMARY471=(Token)input.LT(1);
                    match(input,PRIMARY,FOLLOW_PRIMARY_in_attribute_constraint4692); 
                    PRIMARY471_tree = (Object)adaptor.create(PRIMARY471);
                    adaptor.addChild(root_0, PRIMARY471_tree);

                    KEY472=(Token)input.LT(1);
                    match(input,KEY,FOLLOW_KEY_in_attribute_constraint4694); 
                    KEY472_tree = (Object)adaptor.create(KEY472);
                    adaptor.addChild(root_0, KEY472_tree);


                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end attribute_constraint

    public static class value_specification_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start value_specification
    // Cubrid.g:849:1: value_specification : value ;
    public final value_specification_return value_specification() throws RecognitionException {
        value_specification_return retval = new value_specification_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        value_return value473 = null;



        try {
            // Cubrid.g:849:20: ( value )
            // Cubrid.g:850:2: value
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_value_in_value_specification4706);
            value473=value();
            _fsp--;

            adaptor.addChild(root_0, value473.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end value_specification

    public static class attribute_definition_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start attribute_definition_comma_list
    // Cubrid.g:853:1: attribute_definition_comma_list : attribute_definition ( COMMA attribute_definition )* -> ENTER TAB attribute_definition ( COMMA ENTER attribute_definition )* ENTER UNTAB ;
    public final attribute_definition_comma_list_return attribute_definition_comma_list() throws RecognitionException {
        attribute_definition_comma_list_return retval = new attribute_definition_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA475=null;
        attribute_definition_return attribute_definition474 = null;

        attribute_definition_return attribute_definition476 = null;


        Object COMMA475_tree=null;
        RewriteRuleTokenStream stream_COMMA=new RewriteRuleTokenStream(adaptor,"token COMMA");
        RewriteRuleSubtreeStream stream_attribute_definition=new RewriteRuleSubtreeStream(adaptor,"rule attribute_definition");
        try {
            // Cubrid.g:853:32: ( attribute_definition ( COMMA attribute_definition )* -> ENTER TAB attribute_definition ( COMMA ENTER attribute_definition )* ENTER UNTAB )
            // Cubrid.g:854:2: attribute_definition ( COMMA attribute_definition )*
            {
            pushFollow(FOLLOW_attribute_definition_in_attribute_definition_comma_list4717);
            attribute_definition474=attribute_definition();
            _fsp--;

            stream_attribute_definition.add(attribute_definition474.getTree());
            // Cubrid.g:854:23: ( COMMA attribute_definition )*
            loop123:
            do {
                int alt123=2;
                int LA123_0 = input.LA(1);

                if ( (LA123_0==COMMA) ) {
                    alt123=1;
                }


                switch (alt123) {
            	case 1 :
            	    // Cubrid.g:854:24: COMMA attribute_definition
            	    {
            	    COMMA475=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_attribute_definition_comma_list4720); 
            	    stream_COMMA.add(COMMA475);

            	    pushFollow(FOLLOW_attribute_definition_in_attribute_definition_comma_list4722);
            	    attribute_definition476=attribute_definition();
            	    _fsp--;

            	    stream_attribute_definition.add(attribute_definition476.getTree());

            	    }
            	    break;

            	default :
            	    break loop123;
                }
            } while (true);


            // AST REWRITE
            // elements: COMMA, attribute_definition, attribute_definition
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 855:2: -> ENTER TAB attribute_definition ( COMMA ENTER attribute_definition )* ENTER UNTAB
            {
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));
                adaptor.addChild(root_0, stream_attribute_definition.next());
                // Cubrid.g:856:33: ( COMMA ENTER attribute_definition )*
                while ( stream_COMMA.hasNext()||stream_attribute_definition.hasNext() ) {
                    adaptor.addChild(root_0, stream_COMMA.next());
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, stream_attribute_definition.next());

                }
                stream_COMMA.reset();
                stream_attribute_definition.reset();
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end attribute_definition_comma_list

    public static class method_definition_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start method_definition_comma_list
    // Cubrid.g:859:1: method_definition_comma_list : method_definition ( COMMA method_definition )* -> ENTER TAB method_definition ( COMMA ENTER method_definition )* ENTER UNTAB ;
    public final method_definition_comma_list_return method_definition_comma_list() throws RecognitionException {
        method_definition_comma_list_return retval = new method_definition_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA478=null;
        method_definition_return method_definition477 = null;

        method_definition_return method_definition479 = null;


        Object COMMA478_tree=null;
        RewriteRuleTokenStream stream_COMMA=new RewriteRuleTokenStream(adaptor,"token COMMA");
        RewriteRuleSubtreeStream stream_method_definition=new RewriteRuleSubtreeStream(adaptor,"rule method_definition");
        try {
            // Cubrid.g:859:29: ( method_definition ( COMMA method_definition )* -> ENTER TAB method_definition ( COMMA ENTER method_definition )* ENTER UNTAB )
            // Cubrid.g:860:2: method_definition ( COMMA method_definition )*
            {
            pushFollow(FOLLOW_method_definition_in_method_definition_comma_list4759);
            method_definition477=method_definition();
            _fsp--;

            stream_method_definition.add(method_definition477.getTree());
            // Cubrid.g:860:20: ( COMMA method_definition )*
            loop124:
            do {
                int alt124=2;
                int LA124_0 = input.LA(1);

                if ( (LA124_0==COMMA) ) {
                    alt124=1;
                }


                switch (alt124) {
            	case 1 :
            	    // Cubrid.g:860:21: COMMA method_definition
            	    {
            	    COMMA478=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_method_definition_comma_list4762); 
            	    stream_COMMA.add(COMMA478);

            	    pushFollow(FOLLOW_method_definition_in_method_definition_comma_list4764);
            	    method_definition479=method_definition();
            	    _fsp--;

            	    stream_method_definition.add(method_definition479.getTree());

            	    }
            	    break;

            	default :
            	    break loop124;
                }
            } while (true);


            // AST REWRITE
            // elements: method_definition, COMMA, method_definition
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 861:2: -> ENTER TAB method_definition ( COMMA ENTER method_definition )* ENTER UNTAB
            {
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));
                adaptor.addChild(root_0, stream_method_definition.next());
                // Cubrid.g:862:30: ( COMMA ENTER method_definition )*
                while ( stream_method_definition.hasNext()||stream_COMMA.hasNext() ) {
                    adaptor.addChild(root_0, stream_COMMA.next());
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, stream_method_definition.next());

                }
                stream_method_definition.reset();
                stream_COMMA.reset();
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end method_definition_comma_list

    public static class method_definition_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start method_definition
    // Cubrid.g:865:1: method_definition : general_method_name ( argument_type_part )? ( result_type )? ( FUNCTION function_name )? ;
    public final method_definition_return method_definition() throws RecognitionException {
        method_definition_return retval = new method_definition_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token FUNCTION483=null;
        general_method_name_return general_method_name480 = null;

        argument_type_part_return argument_type_part481 = null;

        result_type_return result_type482 = null;

        function_name_return function_name484 = null;


        Object FUNCTION483_tree=null;

        try {
            // Cubrid.g:865:18: ( general_method_name ( argument_type_part )? ( result_type )? ( FUNCTION function_name )? )
            // Cubrid.g:866:2: general_method_name ( argument_type_part )? ( result_type )? ( FUNCTION function_name )?
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_general_method_name_in_method_definition4801);
            general_method_name480=general_method_name();
            _fsp--;

            adaptor.addChild(root_0, general_method_name480.getTree());
            // Cubrid.g:867:2: ( argument_type_part )?
            int alt125=2;
            int LA125_0 = input.LA(1);

            if ( (LA125_0==STARTBRACKET) ) {
                alt125=1;
            }
            switch (alt125) {
                case 1 :
                    // Cubrid.g:867:2: argument_type_part
                    {
                    pushFollow(FOLLOW_argument_type_part_in_method_definition4804);
                    argument_type_part481=argument_type_part();
                    _fsp--;

                    adaptor.addChild(root_0, argument_type_part481.getTree());

                    }
                    break;

            }

            // Cubrid.g:868:2: ( result_type )?
            int alt126=2;
            int LA126_0 = input.LA(1);

            if ( (LA126_0==BIT||LA126_0==CHAR||(LA126_0>=DATE && LA126_0<=DECIMAL)||LA126_0==DOUBLE||LA126_0==FLOAT||(LA126_0>=INT && LA126_0<=INTEGER)||LA126_0==LIST||(LA126_0>=MONETARY && LA126_0<=MULTISET)||LA126_0==NCHAR||(LA126_0>=NUMERIC && LA126_0<=OBJECT)||LA126_0==REAL||LA126_0==SET||LA126_0==SMALLINT||LA126_0==STRING_STR||(LA126_0>=TIME && LA126_0<=TIMESTAMP)||LA126_0==VARCHAR||(LA126_0>=ID && LA126_0<=COLUMN)) ) {
                alt126=1;
            }
            switch (alt126) {
                case 1 :
                    // Cubrid.g:868:2: result_type
                    {
                    pushFollow(FOLLOW_result_type_in_method_definition4808);
                    result_type482=result_type();
                    _fsp--;

                    adaptor.addChild(root_0, result_type482.getTree());

                    }
                    break;

            }

            // Cubrid.g:869:2: ( FUNCTION function_name )?
            int alt127=2;
            int LA127_0 = input.LA(1);

            if ( (LA127_0==FUNCTION) ) {
                alt127=1;
            }
            switch (alt127) {
                case 1 :
                    // Cubrid.g:869:3: FUNCTION function_name
                    {
                    FUNCTION483=(Token)input.LT(1);
                    match(input,FUNCTION,FOLLOW_FUNCTION_in_method_definition4813); 
                    FUNCTION483_tree = (Object)adaptor.create(FUNCTION483);
                    adaptor.addChild(root_0, FUNCTION483_tree);

                    pushFollow(FOLLOW_function_name_in_method_definition4815);
                    function_name484=function_name();
                    _fsp--;

                    adaptor.addChild(root_0, function_name484.getTree());

                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end method_definition

    public static class general_method_name_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start general_method_name
    // Cubrid.g:872:1: general_method_name : ID ;
    public final general_method_name_return general_method_name() throws RecognitionException {
        general_method_name_return retval = new general_method_name_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ID485=null;

        Object ID485_tree=null;

        try {
            // Cubrid.g:872:20: ( ID )
            // Cubrid.g:873:2: ID
            {
            root_0 = (Object)adaptor.nil();

            ID485=(Token)input.LT(1);
            match(input,ID,FOLLOW_ID_in_general_method_name4828); 
            ID485_tree = (Object)adaptor.create(ID485);
            adaptor.addChild(root_0, ID485_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end general_method_name

    public static class argument_type_part_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start argument_type_part
    // Cubrid.g:876:1: argument_type_part : STARTBRACKET ( argument_type_comma_list )? ENDBRACKET ;
    public final argument_type_part_return argument_type_part() throws RecognitionException {
        argument_type_part_return retval = new argument_type_part_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token STARTBRACKET486=null;
        Token ENDBRACKET488=null;
        argument_type_comma_list_return argument_type_comma_list487 = null;


        Object STARTBRACKET486_tree=null;
        Object ENDBRACKET488_tree=null;

        try {
            // Cubrid.g:876:19: ( STARTBRACKET ( argument_type_comma_list )? ENDBRACKET )
            // Cubrid.g:877:2: STARTBRACKET ( argument_type_comma_list )? ENDBRACKET
            {
            root_0 = (Object)adaptor.nil();

            STARTBRACKET486=(Token)input.LT(1);
            match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_argument_type_part4839); 
            STARTBRACKET486_tree = (Object)adaptor.create(STARTBRACKET486);
            adaptor.addChild(root_0, STARTBRACKET486_tree);

            // Cubrid.g:878:2: ( argument_type_comma_list )?
            int alt128=2;
            int LA128_0 = input.LA(1);

            if ( (LA128_0==BIT||LA128_0==CHAR||(LA128_0>=DATE && LA128_0<=DECIMAL)||LA128_0==DOUBLE||LA128_0==FLOAT||(LA128_0>=INT && LA128_0<=INTEGER)||LA128_0==LIST||(LA128_0>=MONETARY && LA128_0<=MULTISET)||LA128_0==NCHAR||(LA128_0>=NUMERIC && LA128_0<=OBJECT)||LA128_0==REAL||LA128_0==SET||LA128_0==SMALLINT||LA128_0==STRING_STR||(LA128_0>=TIME && LA128_0<=TIMESTAMP)||LA128_0==VARCHAR||(LA128_0>=ID && LA128_0<=COLUMN)) ) {
                alt128=1;
            }
            switch (alt128) {
                case 1 :
                    // Cubrid.g:878:2: argument_type_comma_list
                    {
                    pushFollow(FOLLOW_argument_type_comma_list_in_argument_type_part4843);
                    argument_type_comma_list487=argument_type_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, argument_type_comma_list487.getTree());

                    }
                    break;

            }

            ENDBRACKET488=(Token)input.LT(1);
            match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_argument_type_part4848); 
            ENDBRACKET488_tree = (Object)adaptor.create(ENDBRACKET488);
            adaptor.addChild(root_0, ENDBRACKET488_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end argument_type_part

    public static class argument_type_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start argument_type_comma_list
    // Cubrid.g:881:1: argument_type_comma_list : argument_type ( COMMA argument_type )* -> ENTER TAB argument_type ( COMMA ENTER argument_type )* ENTER UNTAB ;
    public final argument_type_comma_list_return argument_type_comma_list() throws RecognitionException {
        argument_type_comma_list_return retval = new argument_type_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA490=null;
        argument_type_return argument_type489 = null;

        argument_type_return argument_type491 = null;


        Object COMMA490_tree=null;
        RewriteRuleTokenStream stream_COMMA=new RewriteRuleTokenStream(adaptor,"token COMMA");
        RewriteRuleSubtreeStream stream_argument_type=new RewriteRuleSubtreeStream(adaptor,"rule argument_type");
        try {
            // Cubrid.g:881:25: ( argument_type ( COMMA argument_type )* -> ENTER TAB argument_type ( COMMA ENTER argument_type )* ENTER UNTAB )
            // Cubrid.g:882:2: argument_type ( COMMA argument_type )*
            {
            pushFollow(FOLLOW_argument_type_in_argument_type_comma_list4858);
            argument_type489=argument_type();
            _fsp--;

            stream_argument_type.add(argument_type489.getTree());
            // Cubrid.g:882:16: ( COMMA argument_type )*
            loop129:
            do {
                int alt129=2;
                int LA129_0 = input.LA(1);

                if ( (LA129_0==COMMA) ) {
                    alt129=1;
                }


                switch (alt129) {
            	case 1 :
            	    // Cubrid.g:882:17: COMMA argument_type
            	    {
            	    COMMA490=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_argument_type_comma_list4861); 
            	    stream_COMMA.add(COMMA490);

            	    pushFollow(FOLLOW_argument_type_in_argument_type_comma_list4863);
            	    argument_type491=argument_type();
            	    _fsp--;

            	    stream_argument_type.add(argument_type491.getTree());

            	    }
            	    break;

            	default :
            	    break loop129;
                }
            } while (true);


            // AST REWRITE
            // elements: argument_type, argument_type, COMMA
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 883:2: -> ENTER TAB argument_type ( COMMA ENTER argument_type )* ENTER UNTAB
            {
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));
                adaptor.addChild(root_0, stream_argument_type.next());
                // Cubrid.g:884:26: ( COMMA ENTER argument_type )*
                while ( stream_argument_type.hasNext()||stream_COMMA.hasNext() ) {
                    adaptor.addChild(root_0, stream_COMMA.next());
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, stream_argument_type.next());

                }
                stream_argument_type.reset();
                stream_COMMA.reset();
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end argument_type_comma_list

    public static class argument_type_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start argument_type
    // Cubrid.g:887:1: argument_type : domain ;
    public final argument_type_return argument_type() throws RecognitionException {
        argument_type_return retval = new argument_type_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        domain_return domain492 = null;



        try {
            // Cubrid.g:887:14: ( domain )
            // Cubrid.g:888:2: domain
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_domain_in_argument_type4899);
            domain492=domain();
            _fsp--;

            adaptor.addChild(root_0, domain492.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end argument_type

    public static class result_type_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start result_type
    // Cubrid.g:891:1: result_type : domain ;
    public final result_type_return result_type() throws RecognitionException {
        result_type_return retval = new result_type_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        domain_return domain493 = null;



        try {
            // Cubrid.g:891:12: ( domain )
            // Cubrid.g:892:2: domain
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_domain_in_result_type4909);
            domain493=domain();
            _fsp--;

            adaptor.addChild(root_0, domain493.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end result_type

    public static class function_name_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start function_name
    // Cubrid.g:895:1: function_name : ID ;
    public final function_name_return function_name() throws RecognitionException {
        function_name_return retval = new function_name_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ID494=null;

        Object ID494_tree=null;

        try {
            // Cubrid.g:895:14: ( ID )
            // Cubrid.g:896:2: ID
            {
            root_0 = (Object)adaptor.nil();

            ID494=(Token)input.LT(1);
            match(input,ID,FOLLOW_ID_in_function_name4920); 
            ID494_tree = (Object)adaptor.create(ID494);
            adaptor.addChild(root_0, ID494_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end function_name

    public static class method_file_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start method_file_comma_list
    // Cubrid.g:899:1: method_file_comma_list : PATH ( COMMA PATH )* -> ENTER TAB PATH ( COMMA ENTER PATH )* ENTER UNTAB ;
    public final method_file_comma_list_return method_file_comma_list() throws RecognitionException {
        method_file_comma_list_return retval = new method_file_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token PATH495=null;
        Token COMMA496=null;
        Token PATH497=null;

        Object PATH495_tree=null;
        Object COMMA496_tree=null;
        Object PATH497_tree=null;
        RewriteRuleTokenStream stream_PATH=new RewriteRuleTokenStream(adaptor,"token PATH");
        RewriteRuleTokenStream stream_COMMA=new RewriteRuleTokenStream(adaptor,"token COMMA");

        try {
            // Cubrid.g:899:23: ( PATH ( COMMA PATH )* -> ENTER TAB PATH ( COMMA ENTER PATH )* ENTER UNTAB )
            // Cubrid.g:900:2: PATH ( COMMA PATH )*
            {
            PATH495=(Token)input.LT(1);
            match(input,PATH,FOLLOW_PATH_in_method_file_comma_list4932); 
            stream_PATH.add(PATH495);

            // Cubrid.g:900:7: ( COMMA PATH )*
            loop130:
            do {
                int alt130=2;
                int LA130_0 = input.LA(1);

                if ( (LA130_0==COMMA) ) {
                    alt130=1;
                }


                switch (alt130) {
            	case 1 :
            	    // Cubrid.g:900:8: COMMA PATH
            	    {
            	    COMMA496=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_method_file_comma_list4935); 
            	    stream_COMMA.add(COMMA496);

            	    PATH497=(Token)input.LT(1);
            	    match(input,PATH,FOLLOW_PATH_in_method_file_comma_list4937); 
            	    stream_PATH.add(PATH497);


            	    }
            	    break;

            	default :
            	    break loop130;
                }
            } while (true);


            // AST REWRITE
            // elements: COMMA, PATH, PATH
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 901:2: -> ENTER TAB PATH ( COMMA ENTER PATH )* ENTER UNTAB
            {
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));
                adaptor.addChild(root_0, stream_PATH.next());
                // Cubrid.g:902:17: ( COMMA ENTER PATH )*
                while ( stream_COMMA.hasNext()||stream_PATH.hasNext() ) {
                    adaptor.addChild(root_0, stream_COMMA.next());
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, stream_PATH.next());

                }
                stream_COMMA.reset();
                stream_PATH.reset();
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end method_file_comma_list

    public static class resolution_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start resolution_comma_list
    // Cubrid.g:905:1: resolution_comma_list : resolution ( COMMA resolution )* -> ENTER TAB resolution ( COMMA ENTER resolution )* ENTER UNTAB ;
    public final resolution_comma_list_return resolution_comma_list() throws RecognitionException {
        resolution_comma_list_return retval = new resolution_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA499=null;
        resolution_return resolution498 = null;

        resolution_return resolution500 = null;


        Object COMMA499_tree=null;
        RewriteRuleTokenStream stream_COMMA=new RewriteRuleTokenStream(adaptor,"token COMMA");
        RewriteRuleSubtreeStream stream_resolution=new RewriteRuleSubtreeStream(adaptor,"rule resolution");
        try {
            // Cubrid.g:905:22: ( resolution ( COMMA resolution )* -> ENTER TAB resolution ( COMMA ENTER resolution )* ENTER UNTAB )
            // Cubrid.g:906:2: resolution ( COMMA resolution )*
            {
            pushFollow(FOLLOW_resolution_in_resolution_comma_list4973);
            resolution498=resolution();
            _fsp--;

            stream_resolution.add(resolution498.getTree());
            // Cubrid.g:906:13: ( COMMA resolution )*
            loop131:
            do {
                int alt131=2;
                int LA131_0 = input.LA(1);

                if ( (LA131_0==COMMA) ) {
                    alt131=1;
                }


                switch (alt131) {
            	case 1 :
            	    // Cubrid.g:906:14: COMMA resolution
            	    {
            	    COMMA499=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_resolution_comma_list4976); 
            	    stream_COMMA.add(COMMA499);

            	    pushFollow(FOLLOW_resolution_in_resolution_comma_list4978);
            	    resolution500=resolution();
            	    _fsp--;

            	    stream_resolution.add(resolution500.getTree());

            	    }
            	    break;

            	default :
            	    break loop131;
                }
            } while (true);


            // AST REWRITE
            // elements: resolution, COMMA, resolution
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 907:2: -> ENTER TAB resolution ( COMMA ENTER resolution )* ENTER UNTAB
            {
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));
                adaptor.addChild(root_0, stream_resolution.next());
                // Cubrid.g:908:23: ( COMMA ENTER resolution )*
                while ( stream_resolution.hasNext()||stream_COMMA.hasNext() ) {
                    adaptor.addChild(root_0, stream_COMMA.next());
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, stream_resolution.next());

                }
                stream_resolution.reset();
                stream_COMMA.reset();
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end resolution_comma_list

    public static class resolution_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start resolution
    // Cubrid.g:911:1: resolution : general_attribute_name OF class_name ( AS attribute_name )? ;
    public final resolution_return resolution() throws RecognitionException {
        resolution_return retval = new resolution_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token OF502=null;
        Token AS504=null;
        general_attribute_name_return general_attribute_name501 = null;

        class_name_return class_name503 = null;

        attribute_name_return attribute_name505 = null;


        Object OF502_tree=null;
        Object AS504_tree=null;

        try {
            // Cubrid.g:911:11: ( general_attribute_name OF class_name ( AS attribute_name )? )
            // Cubrid.g:912:2: general_attribute_name OF class_name ( AS attribute_name )?
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_general_attribute_name_in_resolution5014);
            general_attribute_name501=general_attribute_name();
            _fsp--;

            adaptor.addChild(root_0, general_attribute_name501.getTree());
            OF502=(Token)input.LT(1);
            match(input,OF,FOLLOW_OF_in_resolution5016); 
            OF502_tree = (Object)adaptor.create(OF502);
            adaptor.addChild(root_0, OF502_tree);

            pushFollow(FOLLOW_class_name_in_resolution5018);
            class_name503=class_name();
            _fsp--;

            adaptor.addChild(root_0, class_name503.getTree());
            // Cubrid.g:913:2: ( AS attribute_name )?
            int alt132=2;
            int LA132_0 = input.LA(1);

            if ( (LA132_0==AS) ) {
                int LA132_1 = input.LA(2);

                if ( ((LA132_1>=ID && LA132_1<=COLUMN)) ) {
                    alt132=1;
                }
            }
            switch (alt132) {
                case 1 :
                    // Cubrid.g:913:3: AS attribute_name
                    {
                    AS504=(Token)input.LT(1);
                    match(input,AS,FOLLOW_AS_in_resolution5022); 
                    AS504_tree = (Object)adaptor.create(AS504);
                    adaptor.addChild(root_0, AS504_tree);

                    pushFollow(FOLLOW_attribute_name_in_resolution5024);
                    attribute_name505=attribute_name();
                    _fsp--;

                    adaptor.addChild(root_0, attribute_name505.getTree());

                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end resolution

    public static class drop_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start drop
    // Cubrid.g:918:1: drop : ( drop_class | drop_index | drop_trigger | drop_deferred );
    public final drop_return drop() throws RecognitionException {
        drop_return retval = new drop_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        drop_class_return drop_class506 = null;

        drop_index_return drop_index507 = null;

        drop_trigger_return drop_trigger508 = null;

        drop_deferred_return drop_deferred509 = null;



        try {
            // Cubrid.g:918:5: ( drop_class | drop_index | drop_trigger | drop_deferred )
            int alt133=4;
            int LA133_0 = input.LA(1);

            if ( (LA133_0==DROP) ) {
                switch ( input.LA(2) ) {
                case DEFERRED:
                    {
                    alt133=4;
                    }
                    break;
                case TRIGGER:
                    {
                    alt133=3;
                    }
                    break;
                case ALL:
                case CLASS:
                case ONLY:
                case TABLE:
                case VCLASS:
                case VIEW:
                case ID:
                case COLUMN:
                case STARTBRACKET:
                    {
                    alt133=1;
                    }
                    break;
                case INDEX:
                case REVERSE:
                case UNIQUE:
                    {
                    alt133=2;
                    }
                    break;
                default:
                    NoViableAltException nvae =
                        new NoViableAltException("918:1: drop : ( drop_class | drop_index | drop_trigger | drop_deferred );", 133, 1, input);

                    throw nvae;
                }

            }
            else {
                NoViableAltException nvae =
                    new NoViableAltException("918:1: drop : ( drop_class | drop_index | drop_trigger | drop_deferred );", 133, 0, input);

                throw nvae;
            }
            switch (alt133) {
                case 1 :
                    // Cubrid.g:919:2: drop_class
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_drop_class_in_drop5039);
                    drop_class506=drop_class();
                    _fsp--;

                    adaptor.addChild(root_0, drop_class506.getTree());

                    }
                    break;
                case 2 :
                    // Cubrid.g:920:4: drop_index
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_drop_index_in_drop5044);
                    drop_index507=drop_index();
                    _fsp--;

                    adaptor.addChild(root_0, drop_index507.getTree());

                    }
                    break;
                case 3 :
                    // Cubrid.g:921:4: drop_trigger
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_drop_trigger_in_drop5049);
                    drop_trigger508=drop_trigger();
                    _fsp--;

                    adaptor.addChild(root_0, drop_trigger508.getTree());

                    }
                    break;
                case 4 :
                    // Cubrid.g:922:4: drop_deferred
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_drop_deferred_in_drop5054);
                    drop_deferred509=drop_deferred();
                    _fsp--;

                    adaptor.addChild(root_0, drop_deferred509.getTree());

                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end drop

    public static class drop_class_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start drop_class
    // Cubrid.g:925:1: drop_class : DROP ( class_type )? class_specification_comma_list ;
    public final drop_class_return drop_class() throws RecognitionException {
        drop_class_return retval = new drop_class_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token DROP510=null;
        class_type_return class_type511 = null;

        class_specification_comma_list_return class_specification_comma_list512 = null;


        Object DROP510_tree=null;

        try {
            // Cubrid.g:925:11: ( DROP ( class_type )? class_specification_comma_list )
            // Cubrid.g:926:2: DROP ( class_type )? class_specification_comma_list
            {
            root_0 = (Object)adaptor.nil();

            DROP510=(Token)input.LT(1);
            match(input,DROP,FOLLOW_DROP_in_drop_class5064); 
            DROP510_tree = (Object)adaptor.create(DROP510);
            adaptor.addChild(root_0, DROP510_tree);

            // Cubrid.g:926:7: ( class_type )?
            int alt134=2;
            int LA134_0 = input.LA(1);

            if ( (LA134_0==CLASS||LA134_0==TABLE||(LA134_0>=VCLASS && LA134_0<=VIEW)) ) {
                alt134=1;
            }
            switch (alt134) {
                case 1 :
                    // Cubrid.g:926:7: class_type
                    {
                    pushFollow(FOLLOW_class_type_in_drop_class5066);
                    class_type511=class_type();
                    _fsp--;

                    adaptor.addChild(root_0, class_type511.getTree());

                    }
                    break;

            }

            pushFollow(FOLLOW_class_specification_comma_list_in_drop_class5069);
            class_specification_comma_list512=class_specification_comma_list();
            _fsp--;

            adaptor.addChild(root_0, class_specification_comma_list512.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end drop_class

    public static class class_type_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start class_type
    // Cubrid.g:929:1: class_type : ( CLASS | TABLE | VCLASS | VIEW );
    public final class_type_return class_type() throws RecognitionException {
        class_type_return retval = new class_type_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set513=null;

        Object set513_tree=null;

        try {
            // Cubrid.g:929:11: ( CLASS | TABLE | VCLASS | VIEW )
            // Cubrid.g:
            {
            root_0 = (Object)adaptor.nil();

            set513=(Token)input.LT(1);
            if ( input.LA(1)==CLASS||input.LA(1)==TABLE||(input.LA(1)>=VCLASS && input.LA(1)<=VIEW) ) {
                input.consume();
                adaptor.addChild(root_0, adaptor.create(set513));
                errorRecovery=false;
            }
            else {
                MismatchedSetException mse =
                    new MismatchedSetException(null,input);
                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_class_type0);    throw mse;
            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end class_type

    public static class class_specification_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start class_specification_comma_list
    // Cubrid.g:936:1: class_specification_comma_list : class_specification ( COMMA class_specification )* ;
    public final class_specification_comma_list_return class_specification_comma_list() throws RecognitionException {
        class_specification_comma_list_return retval = new class_specification_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA515=null;
        class_specification_return class_specification514 = null;

        class_specification_return class_specification516 = null;


        Object COMMA515_tree=null;

        try {
            // Cubrid.g:936:31: ( class_specification ( COMMA class_specification )* )
            // Cubrid.g:937:2: class_specification ( COMMA class_specification )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_class_specification_in_class_specification_comma_list5105);
            class_specification514=class_specification();
            _fsp--;

            adaptor.addChild(root_0, class_specification514.getTree());
            // Cubrid.g:937:22: ( COMMA class_specification )*
            loop135:
            do {
                int alt135=2;
                int LA135_0 = input.LA(1);

                if ( (LA135_0==COMMA) ) {
                    alt135=1;
                }


                switch (alt135) {
            	case 1 :
            	    // Cubrid.g:937:23: COMMA class_specification
            	    {
            	    COMMA515=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_class_specification_comma_list5108); 
            	    COMMA515_tree = (Object)adaptor.create(COMMA515);
            	    adaptor.addChild(root_0, COMMA515_tree);

            	    pushFollow(FOLLOW_class_specification_in_class_specification_comma_list5110);
            	    class_specification516=class_specification();
            	    _fsp--;

            	    adaptor.addChild(root_0, class_specification516.getTree());

            	    }
            	    break;

            	default :
            	    break loop135;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end class_specification_comma_list

    public static class drop_index_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start drop_index
    // Cubrid.g:940:1: drop_index : DROP ( REVERSE )? ( UNIQUE )? INDEX ( index_name )? ON class_name ( attribute_comma_list_part )? -> DROP ( REVERSE )? ( UNIQUE )? INDEX ( index_name )? ON class_name ( attribute_comma_list_part )? ;
    public final drop_index_return drop_index() throws RecognitionException {
        drop_index_return retval = new drop_index_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token DROP517=null;
        Token REVERSE518=null;
        Token UNIQUE519=null;
        Token INDEX520=null;
        Token ON522=null;
        index_name_return index_name521 = null;

        class_name_return class_name523 = null;

        attribute_comma_list_part_return attribute_comma_list_part524 = null;


        Object DROP517_tree=null;
        Object REVERSE518_tree=null;
        Object UNIQUE519_tree=null;
        Object INDEX520_tree=null;
        Object ON522_tree=null;
        RewriteRuleTokenStream stream_INDEX=new RewriteRuleTokenStream(adaptor,"token INDEX");
        RewriteRuleTokenStream stream_ON=new RewriteRuleTokenStream(adaptor,"token ON");
        RewriteRuleTokenStream stream_UNIQUE=new RewriteRuleTokenStream(adaptor,"token UNIQUE");
        RewriteRuleTokenStream stream_DROP=new RewriteRuleTokenStream(adaptor,"token DROP");
        RewriteRuleTokenStream stream_REVERSE=new RewriteRuleTokenStream(adaptor,"token REVERSE");
        RewriteRuleSubtreeStream stream_index_name=new RewriteRuleSubtreeStream(adaptor,"rule index_name");
        RewriteRuleSubtreeStream stream_attribute_comma_list_part=new RewriteRuleSubtreeStream(adaptor,"rule attribute_comma_list_part");
        RewriteRuleSubtreeStream stream_class_name=new RewriteRuleSubtreeStream(adaptor,"rule class_name");
        try {
            // Cubrid.g:940:11: ( DROP ( REVERSE )? ( UNIQUE )? INDEX ( index_name )? ON class_name ( attribute_comma_list_part )? -> DROP ( REVERSE )? ( UNIQUE )? INDEX ( index_name )? ON class_name ( attribute_comma_list_part )? )
            // Cubrid.g:941:2: DROP ( REVERSE )? ( UNIQUE )? INDEX ( index_name )? ON class_name ( attribute_comma_list_part )?
            {
            DROP517=(Token)input.LT(1);
            match(input,DROP,FOLLOW_DROP_in_drop_index5125); 
            stream_DROP.add(DROP517);

            // Cubrid.g:941:7: ( REVERSE )?
            int alt136=2;
            int LA136_0 = input.LA(1);

            if ( (LA136_0==REVERSE) ) {
                alt136=1;
            }
            switch (alt136) {
                case 1 :
                    // Cubrid.g:941:7: REVERSE
                    {
                    REVERSE518=(Token)input.LT(1);
                    match(input,REVERSE,FOLLOW_REVERSE_in_drop_index5127); 
                    stream_REVERSE.add(REVERSE518);


                    }
                    break;

            }

            // Cubrid.g:941:16: ( UNIQUE )?
            int alt137=2;
            int LA137_0 = input.LA(1);

            if ( (LA137_0==UNIQUE) ) {
                alt137=1;
            }
            switch (alt137) {
                case 1 :
                    // Cubrid.g:941:16: UNIQUE
                    {
                    UNIQUE519=(Token)input.LT(1);
                    match(input,UNIQUE,FOLLOW_UNIQUE_in_drop_index5130); 
                    stream_UNIQUE.add(UNIQUE519);


                    }
                    break;

            }

            INDEX520=(Token)input.LT(1);
            match(input,INDEX,FOLLOW_INDEX_in_drop_index5133); 
            stream_INDEX.add(INDEX520);

            // Cubrid.g:941:30: ( index_name )?
            int alt138=2;
            int LA138_0 = input.LA(1);

            if ( (LA138_0==ID) ) {
                alt138=1;
            }
            switch (alt138) {
                case 1 :
                    // Cubrid.g:941:30: index_name
                    {
                    pushFollow(FOLLOW_index_name_in_drop_index5135);
                    index_name521=index_name();
                    _fsp--;

                    stream_index_name.add(index_name521.getTree());

                    }
                    break;

            }

            ON522=(Token)input.LT(1);
            match(input,ON,FOLLOW_ON_in_drop_index5138); 
            stream_ON.add(ON522);

            pushFollow(FOLLOW_class_name_in_drop_index5140);
            class_name523=class_name();
            _fsp--;

            stream_class_name.add(class_name523.getTree());
            // Cubrid.g:941:56: ( attribute_comma_list_part )?
            int alt139=2;
            int LA139_0 = input.LA(1);

            if ( (LA139_0==STARTBRACKET) ) {
                alt139=1;
            }
            switch (alt139) {
                case 1 :
                    // Cubrid.g:941:56: attribute_comma_list_part
                    {
                    pushFollow(FOLLOW_attribute_comma_list_part_in_drop_index5142);
                    attribute_comma_list_part524=attribute_comma_list_part();
                    _fsp--;

                    stream_attribute_comma_list_part.add(attribute_comma_list_part524.getTree());

                    }
                    break;

            }


            // AST REWRITE
            // elements: index_name, INDEX, attribute_comma_list_part, class_name, UNIQUE, DROP, ON, REVERSE
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 942:2: -> DROP ( REVERSE )? ( UNIQUE )? INDEX ( index_name )? ON class_name ( attribute_comma_list_part )?
            {
                adaptor.addChild(root_0, stream_DROP.next());
                // Cubrid.g:942:10: ( REVERSE )?
                if ( stream_REVERSE.hasNext() ) {
                    adaptor.addChild(root_0, stream_REVERSE.next());

                }
                stream_REVERSE.reset();
                // Cubrid.g:942:19: ( UNIQUE )?
                if ( stream_UNIQUE.hasNext() ) {
                    adaptor.addChild(root_0, stream_UNIQUE.next());

                }
                stream_UNIQUE.reset();
                adaptor.addChild(root_0, stream_INDEX.next());
                // Cubrid.g:942:33: ( index_name )?
                if ( stream_index_name.hasNext() ) {
                    adaptor.addChild(root_0, stream_index_name.next());

                }
                stream_index_name.reset();
                adaptor.addChild(root_0, stream_ON.next());
                adaptor.addChild(root_0, stream_class_name.next());
                // Cubrid.g:942:59: ( attribute_comma_list_part )?
                if ( stream_attribute_comma_list_part.hasNext() ) {
                    adaptor.addChild(root_0, stream_attribute_comma_list_part.next());

                }
                stream_attribute_comma_list_part.reset();

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end drop_index

    public static class index_name_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start index_name
    // Cubrid.g:945:1: index_name : ID ;
    public final index_name_return index_name() throws RecognitionException {
        index_name_return retval = new index_name_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ID525=null;

        Object ID525_tree=null;

        try {
            // Cubrid.g:945:11: ( ID )
            // Cubrid.g:946:2: ID
            {
            root_0 = (Object)adaptor.nil();

            ID525=(Token)input.LT(1);
            match(input,ID,FOLLOW_ID_in_index_name5177); 
            ID525_tree = (Object)adaptor.create(ID525);
            adaptor.addChild(root_0, ID525_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end index_name

    public static class drop_trigger_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start drop_trigger
    // Cubrid.g:949:1: drop_trigger : DROP TRIGGER trigger_name_comma_list ;
    public final drop_trigger_return drop_trigger() throws RecognitionException {
        drop_trigger_return retval = new drop_trigger_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token DROP526=null;
        Token TRIGGER527=null;
        trigger_name_comma_list_return trigger_name_comma_list528 = null;


        Object DROP526_tree=null;
        Object TRIGGER527_tree=null;

        try {
            // Cubrid.g:949:13: ( DROP TRIGGER trigger_name_comma_list )
            // Cubrid.g:950:2: DROP TRIGGER trigger_name_comma_list
            {
            root_0 = (Object)adaptor.nil();

            DROP526=(Token)input.LT(1);
            match(input,DROP,FOLLOW_DROP_in_drop_trigger5189); 
            DROP526_tree = (Object)adaptor.create(DROP526);
            adaptor.addChild(root_0, DROP526_tree);

            TRIGGER527=(Token)input.LT(1);
            match(input,TRIGGER,FOLLOW_TRIGGER_in_drop_trigger5191); 
            TRIGGER527_tree = (Object)adaptor.create(TRIGGER527);
            adaptor.addChild(root_0, TRIGGER527_tree);

            pushFollow(FOLLOW_trigger_name_comma_list_in_drop_trigger5193);
            trigger_name_comma_list528=trigger_name_comma_list();
            _fsp--;

            adaptor.addChild(root_0, trigger_name_comma_list528.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end drop_trigger

    public static class trigger_name_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start trigger_name_comma_list
    // Cubrid.g:953:1: trigger_name_comma_list : trigger_name ( COMMA trigger_name )* ;
    public final trigger_name_comma_list_return trigger_name_comma_list() throws RecognitionException {
        trigger_name_comma_list_return retval = new trigger_name_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA530=null;
        trigger_name_return trigger_name529 = null;

        trigger_name_return trigger_name531 = null;


        Object COMMA530_tree=null;

        try {
            // Cubrid.g:953:24: ( trigger_name ( COMMA trigger_name )* )
            // Cubrid.g:954:2: trigger_name ( COMMA trigger_name )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_trigger_name_in_trigger_name_comma_list5204);
            trigger_name529=trigger_name();
            _fsp--;

            adaptor.addChild(root_0, trigger_name529.getTree());
            // Cubrid.g:954:15: ( COMMA trigger_name )*
            loop140:
            do {
                int alt140=2;
                int LA140_0 = input.LA(1);

                if ( (LA140_0==COMMA) ) {
                    alt140=1;
                }


                switch (alt140) {
            	case 1 :
            	    // Cubrid.g:954:16: COMMA trigger_name
            	    {
            	    COMMA530=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_trigger_name_comma_list5207); 
            	    COMMA530_tree = (Object)adaptor.create(COMMA530);
            	    adaptor.addChild(root_0, COMMA530_tree);

            	    pushFollow(FOLLOW_trigger_name_in_trigger_name_comma_list5209);
            	    trigger_name531=trigger_name();
            	    _fsp--;

            	    adaptor.addChild(root_0, trigger_name531.getTree());

            	    }
            	    break;

            	default :
            	    break loop140;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end trigger_name_comma_list

    public static class trigger_name_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start trigger_name
    // Cubrid.g:957:1: trigger_name : ID ;
    public final trigger_name_return trigger_name() throws RecognitionException {
        trigger_name_return retval = new trigger_name_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ID532=null;

        Object ID532_tree=null;

        try {
            // Cubrid.g:957:13: ( ID )
            // Cubrid.g:958:2: ID
            {
            root_0 = (Object)adaptor.nil();

            ID532=(Token)input.LT(1);
            match(input,ID,FOLLOW_ID_in_trigger_name5222); 
            ID532_tree = (Object)adaptor.create(ID532);
            adaptor.addChild(root_0, ID532_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end trigger_name

    public static class drop_deferred_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start drop_deferred
    // Cubrid.g:961:1: drop_deferred : DROP DEFERRED TRIGGER trigger_spec ;
    public final drop_deferred_return drop_deferred() throws RecognitionException {
        drop_deferred_return retval = new drop_deferred_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token DROP533=null;
        Token DEFERRED534=null;
        Token TRIGGER535=null;
        trigger_spec_return trigger_spec536 = null;


        Object DROP533_tree=null;
        Object DEFERRED534_tree=null;
        Object TRIGGER535_tree=null;

        try {
            // Cubrid.g:961:14: ( DROP DEFERRED TRIGGER trigger_spec )
            // Cubrid.g:962:2: DROP DEFERRED TRIGGER trigger_spec
            {
            root_0 = (Object)adaptor.nil();

            DROP533=(Token)input.LT(1);
            match(input,DROP,FOLLOW_DROP_in_drop_deferred5235); 
            DROP533_tree = (Object)adaptor.create(DROP533);
            adaptor.addChild(root_0, DROP533_tree);

            DEFERRED534=(Token)input.LT(1);
            match(input,DEFERRED,FOLLOW_DEFERRED_in_drop_deferred5237); 
            DEFERRED534_tree = (Object)adaptor.create(DEFERRED534);
            adaptor.addChild(root_0, DEFERRED534_tree);

            TRIGGER535=(Token)input.LT(1);
            match(input,TRIGGER,FOLLOW_TRIGGER_in_drop_deferred5239); 
            TRIGGER535_tree = (Object)adaptor.create(TRIGGER535);
            adaptor.addChild(root_0, TRIGGER535_tree);

            pushFollow(FOLLOW_trigger_spec_in_drop_deferred5241);
            trigger_spec536=trigger_spec();
            _fsp--;

            adaptor.addChild(root_0, trigger_spec536.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end drop_deferred

    public static class trigger_spec_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start trigger_spec
    // Cubrid.g:965:1: trigger_spec : ( trigger_name_comma_list | ALL TRIGGERS );
    public final trigger_spec_return trigger_spec() throws RecognitionException {
        trigger_spec_return retval = new trigger_spec_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ALL538=null;
        Token TRIGGERS539=null;
        trigger_name_comma_list_return trigger_name_comma_list537 = null;


        Object ALL538_tree=null;
        Object TRIGGERS539_tree=null;

        try {
            // Cubrid.g:965:13: ( trigger_name_comma_list | ALL TRIGGERS )
            int alt141=2;
            int LA141_0 = input.LA(1);

            if ( (LA141_0==ID) ) {
                alt141=1;
            }
            else if ( (LA141_0==ALL) ) {
                alt141=2;
            }
            else {
                NoViableAltException nvae =
                    new NoViableAltException("965:1: trigger_spec : ( trigger_name_comma_list | ALL TRIGGERS );", 141, 0, input);

                throw nvae;
            }
            switch (alt141) {
                case 1 :
                    // Cubrid.g:966:2: trigger_name_comma_list
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_trigger_name_comma_list_in_trigger_spec5253);
                    trigger_name_comma_list537=trigger_name_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, trigger_name_comma_list537.getTree());

                    }
                    break;
                case 2 :
                    // Cubrid.g:967:4: ALL TRIGGERS
                    {
                    root_0 = (Object)adaptor.nil();

                    ALL538=(Token)input.LT(1);
                    match(input,ALL,FOLLOW_ALL_in_trigger_spec5258); 
                    ALL538_tree = (Object)adaptor.create(ALL538);
                    adaptor.addChild(root_0, ALL538_tree);

                    TRIGGERS539=(Token)input.LT(1);
                    match(input,TRIGGERS,FOLLOW_TRIGGERS_in_trigger_spec5260); 
                    TRIGGERS539_tree = (Object)adaptor.create(TRIGGERS539);
                    adaptor.addChild(root_0, TRIGGERS539_tree);


                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end trigger_spec

    public static class alter_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start alter
    // Cubrid.g:972:1: alter : ALTER ( class_type )? class_name alter_clause -> ALTER ( class_type )? class_name ENTER alter_clause ;
    public final alter_return alter() throws RecognitionException {
        alter_return retval = new alter_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ALTER540=null;
        class_type_return class_type541 = null;

        class_name_return class_name542 = null;

        alter_clause_return alter_clause543 = null;


        Object ALTER540_tree=null;
        RewriteRuleTokenStream stream_ALTER=new RewriteRuleTokenStream(adaptor,"token ALTER");
        RewriteRuleSubtreeStream stream_alter_clause=new RewriteRuleSubtreeStream(adaptor,"rule alter_clause");
        RewriteRuleSubtreeStream stream_class_type=new RewriteRuleSubtreeStream(adaptor,"rule class_type");
        RewriteRuleSubtreeStream stream_class_name=new RewriteRuleSubtreeStream(adaptor,"rule class_name");
        try {
            // Cubrid.g:972:6: ( ALTER ( class_type )? class_name alter_clause -> ALTER ( class_type )? class_name ENTER alter_clause )
            // Cubrid.g:973:2: ALTER ( class_type )? class_name alter_clause
            {
            ALTER540=(Token)input.LT(1);
            match(input,ALTER,FOLLOW_ALTER_in_alter5275); 
            stream_ALTER.add(ALTER540);

            // Cubrid.g:973:8: ( class_type )?
            int alt142=2;
            int LA142_0 = input.LA(1);

            if ( (LA142_0==CLASS||LA142_0==TABLE||(LA142_0>=VCLASS && LA142_0<=VIEW)) ) {
                alt142=1;
            }
            switch (alt142) {
                case 1 :
                    // Cubrid.g:973:8: class_type
                    {
                    pushFollow(FOLLOW_class_type_in_alter5277);
                    class_type541=class_type();
                    _fsp--;

                    stream_class_type.add(class_type541.getTree());

                    }
                    break;

            }

            pushFollow(FOLLOW_class_name_in_alter5280);
            class_name542=class_name();
            _fsp--;

            stream_class_name.add(class_name542.getTree());
            pushFollow(FOLLOW_alter_clause_in_alter5282);
            alter_clause543=alter_clause();
            _fsp--;

            stream_alter_clause.add(alter_clause543.getTree());

            // AST REWRITE
            // elements: alter_clause, ALTER, class_name, class_type
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 974:2: -> ALTER ( class_type )? class_name ENTER alter_clause
            {
                adaptor.addChild(root_0, stream_ALTER.next());
                // Cubrid.g:974:11: ( class_type )?
                if ( stream_class_type.hasNext() ) {
                    adaptor.addChild(root_0, stream_class_type.next());

                }
                stream_class_type.reset();
                adaptor.addChild(root_0, stream_class_name.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, stream_alter_clause.next());

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end alter

    public static class alter_clause_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start alter_clause
    // Cubrid.g:977:1: alter_clause : ( ADD alter_add ( INHERIT resolution_comma_list )? | DROP alter_drop ( INHERIT resolution_comma_list )? | RENAME alter_rename ( INHERIT resolution_comma_list )? | CHANGE alter_change | INHERIT resolution_comma_list );
    public final alter_clause_return alter_clause() throws RecognitionException {
        alter_clause_return retval = new alter_clause_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ADD544=null;
        Token INHERIT546=null;
        Token DROP548=null;
        Token INHERIT550=null;
        Token RENAME552=null;
        Token INHERIT554=null;
        Token CHANGE556=null;
        Token INHERIT558=null;
        alter_add_return alter_add545 = null;

        resolution_comma_list_return resolution_comma_list547 = null;

        alter_drop_return alter_drop549 = null;

        resolution_comma_list_return resolution_comma_list551 = null;

        alter_rename_return alter_rename553 = null;

        resolution_comma_list_return resolution_comma_list555 = null;

        alter_change_return alter_change557 = null;

        resolution_comma_list_return resolution_comma_list559 = null;


        Object ADD544_tree=null;
        Object INHERIT546_tree=null;
        Object DROP548_tree=null;
        Object INHERIT550_tree=null;
        Object RENAME552_tree=null;
        Object INHERIT554_tree=null;
        Object CHANGE556_tree=null;
        Object INHERIT558_tree=null;

        try {
            // Cubrid.g:977:13: ( ADD alter_add ( INHERIT resolution_comma_list )? | DROP alter_drop ( INHERIT resolution_comma_list )? | RENAME alter_rename ( INHERIT resolution_comma_list )? | CHANGE alter_change | INHERIT resolution_comma_list )
            int alt146=5;
            switch ( input.LA(1) ) {
            case ADD:
                {
                alt146=1;
                }
                break;
            case DROP:
                {
                alt146=2;
                }
                break;
            case RENAME:
                {
                alt146=3;
                }
                break;
            case CHANGE:
                {
                alt146=4;
                }
                break;
            case INHERIT:
                {
                alt146=5;
                }
                break;
            default:
                NoViableAltException nvae =
                    new NoViableAltException("977:1: alter_clause : ( ADD alter_add ( INHERIT resolution_comma_list )? | DROP alter_drop ( INHERIT resolution_comma_list )? | RENAME alter_rename ( INHERIT resolution_comma_list )? | CHANGE alter_change | INHERIT resolution_comma_list );", 146, 0, input);

                throw nvae;
            }

            switch (alt146) {
                case 1 :
                    // Cubrid.g:978:2: ADD alter_add ( INHERIT resolution_comma_list )?
                    {
                    root_0 = (Object)adaptor.nil();

                    ADD544=(Token)input.LT(1);
                    match(input,ADD,FOLLOW_ADD_in_alter_clause5307); 
                    ADD544_tree = (Object)adaptor.create(ADD544);
                    adaptor.addChild(root_0, ADD544_tree);

                    pushFollow(FOLLOW_alter_add_in_alter_clause5309);
                    alter_add545=alter_add();
                    _fsp--;

                    adaptor.addChild(root_0, alter_add545.getTree());
                    // Cubrid.g:978:16: ( INHERIT resolution_comma_list )?
                    int alt143=2;
                    int LA143_0 = input.LA(1);

                    if ( (LA143_0==INHERIT) ) {
                        alt143=1;
                    }
                    switch (alt143) {
                        case 1 :
                            // Cubrid.g:978:17: INHERIT resolution_comma_list
                            {
                            INHERIT546=(Token)input.LT(1);
                            match(input,INHERIT,FOLLOW_INHERIT_in_alter_clause5312); 
                            INHERIT546_tree = (Object)adaptor.create(INHERIT546);
                            adaptor.addChild(root_0, INHERIT546_tree);

                            pushFollow(FOLLOW_resolution_comma_list_in_alter_clause5314);
                            resolution_comma_list547=resolution_comma_list();
                            _fsp--;

                            adaptor.addChild(root_0, resolution_comma_list547.getTree());

                            }
                            break;

                    }


                    }
                    break;
                case 2 :
                    // Cubrid.g:979:4: DROP alter_drop ( INHERIT resolution_comma_list )?
                    {
                    root_0 = (Object)adaptor.nil();

                    DROP548=(Token)input.LT(1);
                    match(input,DROP,FOLLOW_DROP_in_alter_clause5321); 
                    DROP548_tree = (Object)adaptor.create(DROP548);
                    adaptor.addChild(root_0, DROP548_tree);

                    pushFollow(FOLLOW_alter_drop_in_alter_clause5323);
                    alter_drop549=alter_drop();
                    _fsp--;

                    adaptor.addChild(root_0, alter_drop549.getTree());
                    // Cubrid.g:979:20: ( INHERIT resolution_comma_list )?
                    int alt144=2;
                    int LA144_0 = input.LA(1);

                    if ( (LA144_0==INHERIT) ) {
                        alt144=1;
                    }
                    switch (alt144) {
                        case 1 :
                            // Cubrid.g:979:21: INHERIT resolution_comma_list
                            {
                            INHERIT550=(Token)input.LT(1);
                            match(input,INHERIT,FOLLOW_INHERIT_in_alter_clause5326); 
                            INHERIT550_tree = (Object)adaptor.create(INHERIT550);
                            adaptor.addChild(root_0, INHERIT550_tree);

                            pushFollow(FOLLOW_resolution_comma_list_in_alter_clause5328);
                            resolution_comma_list551=resolution_comma_list();
                            _fsp--;

                            adaptor.addChild(root_0, resolution_comma_list551.getTree());

                            }
                            break;

                    }


                    }
                    break;
                case 3 :
                    // Cubrid.g:980:4: RENAME alter_rename ( INHERIT resolution_comma_list )?
                    {
                    root_0 = (Object)adaptor.nil();

                    RENAME552=(Token)input.LT(1);
                    match(input,RENAME,FOLLOW_RENAME_in_alter_clause5335); 
                    RENAME552_tree = (Object)adaptor.create(RENAME552);
                    adaptor.addChild(root_0, RENAME552_tree);

                    pushFollow(FOLLOW_alter_rename_in_alter_clause5337);
                    alter_rename553=alter_rename();
                    _fsp--;

                    adaptor.addChild(root_0, alter_rename553.getTree());
                    // Cubrid.g:980:24: ( INHERIT resolution_comma_list )?
                    int alt145=2;
                    int LA145_0 = input.LA(1);

                    if ( (LA145_0==INHERIT) ) {
                        alt145=1;
                    }
                    switch (alt145) {
                        case 1 :
                            // Cubrid.g:980:25: INHERIT resolution_comma_list
                            {
                            INHERIT554=(Token)input.LT(1);
                            match(input,INHERIT,FOLLOW_INHERIT_in_alter_clause5340); 
                            INHERIT554_tree = (Object)adaptor.create(INHERIT554);
                            adaptor.addChild(root_0, INHERIT554_tree);

                            pushFollow(FOLLOW_resolution_comma_list_in_alter_clause5342);
                            resolution_comma_list555=resolution_comma_list();
                            _fsp--;

                            adaptor.addChild(root_0, resolution_comma_list555.getTree());

                            }
                            break;

                    }


                    }
                    break;
                case 4 :
                    // Cubrid.g:981:4: CHANGE alter_change
                    {
                    root_0 = (Object)adaptor.nil();

                    CHANGE556=(Token)input.LT(1);
                    match(input,CHANGE,FOLLOW_CHANGE_in_alter_clause5349); 
                    CHANGE556_tree = (Object)adaptor.create(CHANGE556);
                    adaptor.addChild(root_0, CHANGE556_tree);

                    pushFollow(FOLLOW_alter_change_in_alter_clause5351);
                    alter_change557=alter_change();
                    _fsp--;

                    adaptor.addChild(root_0, alter_change557.getTree());

                    }
                    break;
                case 5 :
                    // Cubrid.g:982:4: INHERIT resolution_comma_list
                    {
                    root_0 = (Object)adaptor.nil();

                    INHERIT558=(Token)input.LT(1);
                    match(input,INHERIT,FOLLOW_INHERIT_in_alter_clause5356); 
                    INHERIT558_tree = (Object)adaptor.create(INHERIT558);
                    adaptor.addChild(root_0, INHERIT558_tree);

                    pushFollow(FOLLOW_resolution_comma_list_in_alter_clause5358);
                    resolution_comma_list559=resolution_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, resolution_comma_list559.getTree());

                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end alter_clause

    public static class alter_add_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start alter_add
    // Cubrid.g:985:1: alter_add : ( ( ATTRIBUTE | COLUMN )? class_element_comma_list | CLASS ATTRIBUTE class_element_comma_list | FILE file_name_comma_list | METHOD method_definition_comma_list | QUERY select_statement | SUPERCLASS class_name_comma_list );
    public final alter_add_return alter_add() throws RecognitionException {
        alter_add_return retval = new alter_add_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set560=null;
        Token CLASS562=null;
        Token ATTRIBUTE563=null;
        Token FILE565=null;
        Token METHOD567=null;
        Token QUERY569=null;
        Token SUPERCLASS571=null;
        class_element_comma_list_return class_element_comma_list561 = null;

        class_element_comma_list_return class_element_comma_list564 = null;

        file_name_comma_list_return file_name_comma_list566 = null;

        method_definition_comma_list_return method_definition_comma_list568 = null;

        select_statement_return select_statement570 = null;

        class_name_comma_list_return class_name_comma_list572 = null;


        Object set560_tree=null;
        Object CLASS562_tree=null;
        Object ATTRIBUTE563_tree=null;
        Object FILE565_tree=null;
        Object METHOD567_tree=null;
        Object QUERY569_tree=null;
        Object SUPERCLASS571_tree=null;

        try {
            // Cubrid.g:985:10: ( ( ATTRIBUTE | COLUMN )? class_element_comma_list | CLASS ATTRIBUTE class_element_comma_list | FILE file_name_comma_list | METHOD method_definition_comma_list | QUERY select_statement | SUPERCLASS class_name_comma_list )
            int alt148=6;
            switch ( input.LA(1) ) {
            case ATTRIBUTE:
            case CONSTRAINT:
            case FOREIGN:
            case INHERIT:
            case PRIMARY:
            case UNIQUE:
            case END:
            case COMMA:
            case ID:
            case COLUMN:
                {
                alt148=1;
                }
                break;
            case CLASS:
                {
                int LA148_2 = input.LA(2);

                if ( (LA148_2==ATTRIBUTE) ) {
                    alt148=2;
                }
                else if ( ((LA148_2>=ID && LA148_2<=COLUMN)) ) {
                    alt148=1;
                }
                else {
                    NoViableAltException nvae =
                        new NoViableAltException("985:1: alter_add : ( ( ATTRIBUTE | COLUMN )? class_element_comma_list | CLASS ATTRIBUTE class_element_comma_list | FILE file_name_comma_list | METHOD method_definition_comma_list | QUERY select_statement | SUPERCLASS class_name_comma_list );", 148, 2, input);

                    throw nvae;
                }
                }
                break;
            case FILE:
                {
                alt148=3;
                }
                break;
            case METHOD:
                {
                alt148=4;
                }
                break;
            case QUERY:
                {
                alt148=5;
                }
                break;
            case SUPERCLASS:
                {
                alt148=6;
                }
                break;
            default:
                NoViableAltException nvae =
                    new NoViableAltException("985:1: alter_add : ( ( ATTRIBUTE | COLUMN )? class_element_comma_list | CLASS ATTRIBUTE class_element_comma_list | FILE file_name_comma_list | METHOD method_definition_comma_list | QUERY select_statement | SUPERCLASS class_name_comma_list );", 148, 0, input);

                throw nvae;
            }

            switch (alt148) {
                case 1 :
                    // Cubrid.g:986:2: ( ATTRIBUTE | COLUMN )? class_element_comma_list
                    {
                    root_0 = (Object)adaptor.nil();

                    // Cubrid.g:986:2: ( ATTRIBUTE | COLUMN )?
                    int alt147=2;
                    int LA147_0 = input.LA(1);

                    if ( (LA147_0==COLUMN) ) {
                        switch ( input.LA(2) ) {
                            case ID:
                                {
                                int LA147_4 = input.LA(3);

                                if ( (LA147_4==BIT||LA147_4==CHAR||(LA147_4>=DATE && LA147_4<=DECIMAL)||LA147_4==DOUBLE||LA147_4==FLOAT||(LA147_4>=INT && LA147_4<=INTEGER)||LA147_4==LIST||(LA147_4>=MONETARY && LA147_4<=MULTISET)||LA147_4==NCHAR||(LA147_4>=NUMERIC && LA147_4<=OBJECT)||LA147_4==REAL||LA147_4==SET||LA147_4==SMALLINT||LA147_4==STRING_STR||(LA147_4>=TIME && LA147_4<=TIMESTAMP)||LA147_4==VARCHAR||LA147_4==DOT||(LA147_4>=ID && LA147_4<=STARTBRACKET)) ) {
                                    alt147=1;
                                }
                                }
                                break;
                            case CLASS:
                            case CONSTRAINT:
                            case FOREIGN:
                            case INHERIT:
                            case PRIMARY:
                            case UNIQUE:
                            case END:
                            case COMMA:
                                {
                                alt147=1;
                                }
                                break;
                            case COLUMN:
                                {
                                int LA147_5 = input.LA(3);

                                if ( (LA147_5==BIT||LA147_5==CHAR||(LA147_5>=DATE && LA147_5<=DECIMAL)||LA147_5==DOUBLE||LA147_5==FLOAT||(LA147_5>=INT && LA147_5<=INTEGER)||LA147_5==LIST||(LA147_5>=MONETARY && LA147_5<=MULTISET)||LA147_5==NCHAR||(LA147_5>=NUMERIC && LA147_5<=OBJECT)||LA147_5==REAL||LA147_5==SET||LA147_5==SMALLINT||LA147_5==STRING_STR||(LA147_5>=TIME && LA147_5<=TIMESTAMP)||LA147_5==VARCHAR||(LA147_5>=ID && LA147_5<=COLUMN)) ) {
                                    alt147=1;
                                }
                                }
                                break;
                        }

                    }
                    else if ( (LA147_0==ATTRIBUTE) ) {
                        alt147=1;
                    }
                    switch (alt147) {
                        case 1 :
                            // Cubrid.g:
                            {
                            set560=(Token)input.LT(1);
                            if ( input.LA(1)==ATTRIBUTE||input.LA(1)==COLUMN ) {
                                input.consume();
                                adaptor.addChild(root_0, adaptor.create(set560));
                                errorRecovery=false;
                            }
                            else {
                                MismatchedSetException mse =
                                    new MismatchedSetException(null,input);
                                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_alter_add5369);    throw mse;
                            }


                            }
                            break;

                    }

                    pushFollow(FOLLOW_class_element_comma_list_in_alter_add5380);
                    class_element_comma_list561=class_element_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, class_element_comma_list561.getTree());

                    }
                    break;
                case 2 :
                    // Cubrid.g:987:4: CLASS ATTRIBUTE class_element_comma_list
                    {
                    root_0 = (Object)adaptor.nil();

                    CLASS562=(Token)input.LT(1);
                    match(input,CLASS,FOLLOW_CLASS_in_alter_add5385); 
                    CLASS562_tree = (Object)adaptor.create(CLASS562);
                    adaptor.addChild(root_0, CLASS562_tree);

                    ATTRIBUTE563=(Token)input.LT(1);
                    match(input,ATTRIBUTE,FOLLOW_ATTRIBUTE_in_alter_add5387); 
                    ATTRIBUTE563_tree = (Object)adaptor.create(ATTRIBUTE563);
                    adaptor.addChild(root_0, ATTRIBUTE563_tree);

                    pushFollow(FOLLOW_class_element_comma_list_in_alter_add5389);
                    class_element_comma_list564=class_element_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, class_element_comma_list564.getTree());

                    }
                    break;
                case 3 :
                    // Cubrid.g:988:4: FILE file_name_comma_list
                    {
                    root_0 = (Object)adaptor.nil();

                    FILE565=(Token)input.LT(1);
                    match(input,FILE,FOLLOW_FILE_in_alter_add5394); 
                    FILE565_tree = (Object)adaptor.create(FILE565);
                    adaptor.addChild(root_0, FILE565_tree);

                    pushFollow(FOLLOW_file_name_comma_list_in_alter_add5396);
                    file_name_comma_list566=file_name_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, file_name_comma_list566.getTree());

                    }
                    break;
                case 4 :
                    // Cubrid.g:989:4: METHOD method_definition_comma_list
                    {
                    root_0 = (Object)adaptor.nil();

                    METHOD567=(Token)input.LT(1);
                    match(input,METHOD,FOLLOW_METHOD_in_alter_add5401); 
                    METHOD567_tree = (Object)adaptor.create(METHOD567);
                    adaptor.addChild(root_0, METHOD567_tree);

                    pushFollow(FOLLOW_method_definition_comma_list_in_alter_add5403);
                    method_definition_comma_list568=method_definition_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, method_definition_comma_list568.getTree());

                    }
                    break;
                case 5 :
                    // Cubrid.g:990:4: QUERY select_statement
                    {
                    root_0 = (Object)adaptor.nil();

                    QUERY569=(Token)input.LT(1);
                    match(input,QUERY,FOLLOW_QUERY_in_alter_add5408); 
                    QUERY569_tree = (Object)adaptor.create(QUERY569);
                    adaptor.addChild(root_0, QUERY569_tree);

                    pushFollow(FOLLOW_select_statement_in_alter_add5410);
                    select_statement570=select_statement();
                    _fsp--;

                    adaptor.addChild(root_0, select_statement570.getTree());

                    }
                    break;
                case 6 :
                    // Cubrid.g:991:4: SUPERCLASS class_name_comma_list
                    {
                    root_0 = (Object)adaptor.nil();

                    SUPERCLASS571=(Token)input.LT(1);
                    match(input,SUPERCLASS,FOLLOW_SUPERCLASS_in_alter_add5415); 
                    SUPERCLASS571_tree = (Object)adaptor.create(SUPERCLASS571);
                    adaptor.addChild(root_0, SUPERCLASS571_tree);

                    pushFollow(FOLLOW_class_name_comma_list_in_alter_add5417);
                    class_name_comma_list572=class_name_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, class_name_comma_list572.getTree());

                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end alter_add

    public static class file_name_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start file_name_comma_list
    // Cubrid.g:994:1: file_name_comma_list : file_path_name ( COMMA file_path_name )* ;
    public final file_name_comma_list_return file_name_comma_list() throws RecognitionException {
        file_name_comma_list_return retval = new file_name_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA574=null;
        file_path_name_return file_path_name573 = null;

        file_path_name_return file_path_name575 = null;


        Object COMMA574_tree=null;

        try {
            // Cubrid.g:994:21: ( file_path_name ( COMMA file_path_name )* )
            // Cubrid.g:995:2: file_path_name ( COMMA file_path_name )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_file_path_name_in_file_name_comma_list5427);
            file_path_name573=file_path_name();
            _fsp--;

            adaptor.addChild(root_0, file_path_name573.getTree());
            // Cubrid.g:995:17: ( COMMA file_path_name )*
            loop149:
            do {
                int alt149=2;
                int LA149_0 = input.LA(1);

                if ( (LA149_0==COMMA) ) {
                    alt149=1;
                }


                switch (alt149) {
            	case 1 :
            	    // Cubrid.g:995:18: COMMA file_path_name
            	    {
            	    COMMA574=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_file_name_comma_list5430); 
            	    COMMA574_tree = (Object)adaptor.create(COMMA574);
            	    adaptor.addChild(root_0, COMMA574_tree);

            	    pushFollow(FOLLOW_file_path_name_in_file_name_comma_list5432);
            	    file_path_name575=file_path_name();
            	    _fsp--;

            	    adaptor.addChild(root_0, file_path_name575.getTree());

            	    }
            	    break;

            	default :
            	    break loop149;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end file_name_comma_list

    public static class alter_drop_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start alter_drop
    // Cubrid.g:998:1: alter_drop : ( ( ATTRIBUTE | COLUMN | METHOD )? general_attribute_name_comma_list | FILE file_name_comma_list | QUERY ( unsigned_integer_literal )? | SUPERCLASS class_name_comma_list | CONSTRAINT constraint_name );
    public final alter_drop_return alter_drop() throws RecognitionException {
        alter_drop_return retval = new alter_drop_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set576=null;
        Token FILE578=null;
        Token QUERY580=null;
        Token SUPERCLASS582=null;
        Token CONSTRAINT584=null;
        general_attribute_name_comma_list_return general_attribute_name_comma_list577 = null;

        file_name_comma_list_return file_name_comma_list579 = null;

        unsigned_integer_literal_return unsigned_integer_literal581 = null;

        class_name_comma_list_return class_name_comma_list583 = null;

        constraint_name_return constraint_name585 = null;


        Object set576_tree=null;
        Object FILE578_tree=null;
        Object QUERY580_tree=null;
        Object SUPERCLASS582_tree=null;
        Object CONSTRAINT584_tree=null;

        try {
            // Cubrid.g:998:11: ( ( ATTRIBUTE | COLUMN | METHOD )? general_attribute_name_comma_list | FILE file_name_comma_list | QUERY ( unsigned_integer_literal )? | SUPERCLASS class_name_comma_list | CONSTRAINT constraint_name )
            int alt152=5;
            switch ( input.LA(1) ) {
            case ATTRIBUTE:
            case CLASS:
            case METHOD:
            case ID:
            case COLUMN:
                {
                alt152=1;
                }
                break;
            case FILE:
                {
                alt152=2;
                }
                break;
            case QUERY:
                {
                alt152=3;
                }
                break;
            case SUPERCLASS:
                {
                alt152=4;
                }
                break;
            case CONSTRAINT:
                {
                alt152=5;
                }
                break;
            default:
                NoViableAltException nvae =
                    new NoViableAltException("998:1: alter_drop : ( ( ATTRIBUTE | COLUMN | METHOD )? general_attribute_name_comma_list | FILE file_name_comma_list | QUERY ( unsigned_integer_literal )? | SUPERCLASS class_name_comma_list | CONSTRAINT constraint_name );", 152, 0, input);

                throw nvae;
            }

            switch (alt152) {
                case 1 :
                    // Cubrid.g:999:2: ( ATTRIBUTE | COLUMN | METHOD )? general_attribute_name_comma_list
                    {
                    root_0 = (Object)adaptor.nil();

                    // Cubrid.g:999:2: ( ATTRIBUTE | COLUMN | METHOD )?
                    int alt150=2;
                    int LA150_0 = input.LA(1);

                    if ( (LA150_0==COLUMN) ) {
                        int LA150_1 = input.LA(2);

                        if ( (LA150_1==CLASS||(LA150_1>=ID && LA150_1<=COLUMN)) ) {
                            alt150=1;
                        }
                    }
                    else if ( (LA150_0==ATTRIBUTE||LA150_0==METHOD) ) {
                        alt150=1;
                    }
                    switch (alt150) {
                        case 1 :
                            // Cubrid.g:
                            {
                            set576=(Token)input.LT(1);
                            if ( input.LA(1)==ATTRIBUTE||input.LA(1)==METHOD||input.LA(1)==COLUMN ) {
                                input.consume();
                                adaptor.addChild(root_0, adaptor.create(set576));
                                errorRecovery=false;
                            }
                            else {
                                MismatchedSetException mse =
                                    new MismatchedSetException(null,input);
                                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_alter_drop5446);    throw mse;
                            }


                            }
                            break;

                    }

                    pushFollow(FOLLOW_general_attribute_name_comma_list_in_alter_drop5460);
                    general_attribute_name_comma_list577=general_attribute_name_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, general_attribute_name_comma_list577.getTree());

                    }
                    break;
                case 2 :
                    // Cubrid.g:1000:4: FILE file_name_comma_list
                    {
                    root_0 = (Object)adaptor.nil();

                    FILE578=(Token)input.LT(1);
                    match(input,FILE,FOLLOW_FILE_in_alter_drop5465); 
                    FILE578_tree = (Object)adaptor.create(FILE578);
                    adaptor.addChild(root_0, FILE578_tree);

                    pushFollow(FOLLOW_file_name_comma_list_in_alter_drop5467);
                    file_name_comma_list579=file_name_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, file_name_comma_list579.getTree());

                    }
                    break;
                case 3 :
                    // Cubrid.g:1001:4: QUERY ( unsigned_integer_literal )?
                    {
                    root_0 = (Object)adaptor.nil();

                    QUERY580=(Token)input.LT(1);
                    match(input,QUERY,FOLLOW_QUERY_in_alter_drop5472); 
                    QUERY580_tree = (Object)adaptor.create(QUERY580);
                    adaptor.addChild(root_0, QUERY580_tree);

                    // Cubrid.g:1001:10: ( unsigned_integer_literal )?
                    int alt151=2;
                    int LA151_0 = input.LA(1);

                    if ( (LA151_0==DECIMALLITERAL) ) {
                        alt151=1;
                    }
                    switch (alt151) {
                        case 1 :
                            // Cubrid.g:1001:10: unsigned_integer_literal
                            {
                            pushFollow(FOLLOW_unsigned_integer_literal_in_alter_drop5474);
                            unsigned_integer_literal581=unsigned_integer_literal();
                            _fsp--;

                            adaptor.addChild(root_0, unsigned_integer_literal581.getTree());

                            }
                            break;

                    }


                    }
                    break;
                case 4 :
                    // Cubrid.g:1002:4: SUPERCLASS class_name_comma_list
                    {
                    root_0 = (Object)adaptor.nil();

                    SUPERCLASS582=(Token)input.LT(1);
                    match(input,SUPERCLASS,FOLLOW_SUPERCLASS_in_alter_drop5481); 
                    SUPERCLASS582_tree = (Object)adaptor.create(SUPERCLASS582);
                    adaptor.addChild(root_0, SUPERCLASS582_tree);

                    pushFollow(FOLLOW_class_name_comma_list_in_alter_drop5483);
                    class_name_comma_list583=class_name_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, class_name_comma_list583.getTree());

                    }
                    break;
                case 5 :
                    // Cubrid.g:1003:4: CONSTRAINT constraint_name
                    {
                    root_0 = (Object)adaptor.nil();

                    CONSTRAINT584=(Token)input.LT(1);
                    match(input,CONSTRAINT,FOLLOW_CONSTRAINT_in_alter_drop5488); 
                    CONSTRAINT584_tree = (Object)adaptor.create(CONSTRAINT584);
                    adaptor.addChild(root_0, CONSTRAINT584_tree);

                    pushFollow(FOLLOW_constraint_name_in_alter_drop5490);
                    constraint_name585=constraint_name();
                    _fsp--;

                    adaptor.addChild(root_0, constraint_name585.getTree());

                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end alter_drop

    public static class general_attribute_name_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start general_attribute_name_comma_list
    // Cubrid.g:1006:1: general_attribute_name_comma_list : general_attribute_name ( COMMA general_attribute_name )* ;
    public final general_attribute_name_comma_list_return general_attribute_name_comma_list() throws RecognitionException {
        general_attribute_name_comma_list_return retval = new general_attribute_name_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA587=null;
        general_attribute_name_return general_attribute_name586 = null;

        general_attribute_name_return general_attribute_name588 = null;


        Object COMMA587_tree=null;

        try {
            // Cubrid.g:1006:34: ( general_attribute_name ( COMMA general_attribute_name )* )
            // Cubrid.g:1007:2: general_attribute_name ( COMMA general_attribute_name )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_general_attribute_name_in_general_attribute_name_comma_list5501);
            general_attribute_name586=general_attribute_name();
            _fsp--;

            adaptor.addChild(root_0, general_attribute_name586.getTree());
            // Cubrid.g:1007:25: ( COMMA general_attribute_name )*
            loop153:
            do {
                int alt153=2;
                int LA153_0 = input.LA(1);

                if ( (LA153_0==COMMA) ) {
                    alt153=1;
                }


                switch (alt153) {
            	case 1 :
            	    // Cubrid.g:1007:26: COMMA general_attribute_name
            	    {
            	    COMMA587=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_general_attribute_name_comma_list5504); 
            	    COMMA587_tree = (Object)adaptor.create(COMMA587);
            	    adaptor.addChild(root_0, COMMA587_tree);

            	    pushFollow(FOLLOW_general_attribute_name_in_general_attribute_name_comma_list5506);
            	    general_attribute_name588=general_attribute_name();
            	    _fsp--;

            	    adaptor.addChild(root_0, general_attribute_name588.getTree());

            	    }
            	    break;

            	default :
            	    break loop153;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end general_attribute_name_comma_list

    public static class alter_change_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start alter_change
    // Cubrid.g:1010:1: alter_change : ( FILE file_path_name AS file_path_name | METHOD method_definition_comma_list | QUERY ( unsigned_integer_literal )? select_statement | general_attribute_name DEFAULT value_specifiation );
    public final alter_change_return alter_change() throws RecognitionException {
        alter_change_return retval = new alter_change_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token FILE589=null;
        Token AS591=null;
        Token METHOD593=null;
        Token QUERY595=null;
        Token DEFAULT599=null;
        file_path_name_return file_path_name590 = null;

        file_path_name_return file_path_name592 = null;

        method_definition_comma_list_return method_definition_comma_list594 = null;

        unsigned_integer_literal_return unsigned_integer_literal596 = null;

        select_statement_return select_statement597 = null;

        general_attribute_name_return general_attribute_name598 = null;

        value_specifiation_return value_specifiation600 = null;


        Object FILE589_tree=null;
        Object AS591_tree=null;
        Object METHOD593_tree=null;
        Object QUERY595_tree=null;
        Object DEFAULT599_tree=null;

        try {
            // Cubrid.g:1010:13: ( FILE file_path_name AS file_path_name | METHOD method_definition_comma_list | QUERY ( unsigned_integer_literal )? select_statement | general_attribute_name DEFAULT value_specifiation )
            int alt155=4;
            switch ( input.LA(1) ) {
            case FILE:
                {
                alt155=1;
                }
                break;
            case METHOD:
                {
                alt155=2;
                }
                break;
            case QUERY:
                {
                alt155=3;
                }
                break;
            case CLASS:
            case ID:
            case COLUMN:
                {
                alt155=4;
                }
                break;
            default:
                NoViableAltException nvae =
                    new NoViableAltException("1010:1: alter_change : ( FILE file_path_name AS file_path_name | METHOD method_definition_comma_list | QUERY ( unsigned_integer_literal )? select_statement | general_attribute_name DEFAULT value_specifiation );", 155, 0, input);

                throw nvae;
            }

            switch (alt155) {
                case 1 :
                    // Cubrid.g:1011:2: FILE file_path_name AS file_path_name
                    {
                    root_0 = (Object)adaptor.nil();

                    FILE589=(Token)input.LT(1);
                    match(input,FILE,FOLLOW_FILE_in_alter_change5519); 
                    FILE589_tree = (Object)adaptor.create(FILE589);
                    adaptor.addChild(root_0, FILE589_tree);

                    pushFollow(FOLLOW_file_path_name_in_alter_change5521);
                    file_path_name590=file_path_name();
                    _fsp--;

                    adaptor.addChild(root_0, file_path_name590.getTree());
                    AS591=(Token)input.LT(1);
                    match(input,AS,FOLLOW_AS_in_alter_change5523); 
                    AS591_tree = (Object)adaptor.create(AS591);
                    adaptor.addChild(root_0, AS591_tree);

                    pushFollow(FOLLOW_file_path_name_in_alter_change5525);
                    file_path_name592=file_path_name();
                    _fsp--;

                    adaptor.addChild(root_0, file_path_name592.getTree());

                    }
                    break;
                case 2 :
                    // Cubrid.g:1012:4: METHOD method_definition_comma_list
                    {
                    root_0 = (Object)adaptor.nil();

                    METHOD593=(Token)input.LT(1);
                    match(input,METHOD,FOLLOW_METHOD_in_alter_change5531); 
                    METHOD593_tree = (Object)adaptor.create(METHOD593);
                    adaptor.addChild(root_0, METHOD593_tree);

                    pushFollow(FOLLOW_method_definition_comma_list_in_alter_change5533);
                    method_definition_comma_list594=method_definition_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, method_definition_comma_list594.getTree());

                    }
                    break;
                case 3 :
                    // Cubrid.g:1013:4: QUERY ( unsigned_integer_literal )? select_statement
                    {
                    root_0 = (Object)adaptor.nil();

                    QUERY595=(Token)input.LT(1);
                    match(input,QUERY,FOLLOW_QUERY_in_alter_change5538); 
                    QUERY595_tree = (Object)adaptor.create(QUERY595);
                    adaptor.addChild(root_0, QUERY595_tree);

                    // Cubrid.g:1013:10: ( unsigned_integer_literal )?
                    int alt154=2;
                    int LA154_0 = input.LA(1);

                    if ( (LA154_0==DECIMALLITERAL) ) {
                        alt154=1;
                    }
                    switch (alt154) {
                        case 1 :
                            // Cubrid.g:1013:10: unsigned_integer_literal
                            {
                            pushFollow(FOLLOW_unsigned_integer_literal_in_alter_change5540);
                            unsigned_integer_literal596=unsigned_integer_literal();
                            _fsp--;

                            adaptor.addChild(root_0, unsigned_integer_literal596.getTree());

                            }
                            break;

                    }

                    pushFollow(FOLLOW_select_statement_in_alter_change5543);
                    select_statement597=select_statement();
                    _fsp--;

                    adaptor.addChild(root_0, select_statement597.getTree());

                    }
                    break;
                case 4 :
                    // Cubrid.g:1014:4: general_attribute_name DEFAULT value_specifiation
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_general_attribute_name_in_alter_change5548);
                    general_attribute_name598=general_attribute_name();
                    _fsp--;

                    adaptor.addChild(root_0, general_attribute_name598.getTree());
                    DEFAULT599=(Token)input.LT(1);
                    match(input,DEFAULT,FOLLOW_DEFAULT_in_alter_change5550); 
                    DEFAULT599_tree = (Object)adaptor.create(DEFAULT599);
                    adaptor.addChild(root_0, DEFAULT599_tree);

                    pushFollow(FOLLOW_value_specifiation_in_alter_change5552);
                    value_specifiation600=value_specifiation();
                    _fsp--;

                    adaptor.addChild(root_0, value_specifiation600.getTree());

                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end alter_change

    public static class alter_rename_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start alter_rename
    // Cubrid.g:1017:1: alter_rename : ( ( ATTRIBUTE | COLUMN | METHOD )? general_attribute_name AS attribute_name | FUNCTION OF general_attribute_name AS function_name FILE file_path_name AS file_path_name );
    public final alter_rename_return alter_rename() throws RecognitionException {
        alter_rename_return retval = new alter_rename_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set601=null;
        Token AS603=null;
        Token FUNCTION605=null;
        Token OF606=null;
        Token AS608=null;
        Token FILE610=null;
        Token AS612=null;
        general_attribute_name_return general_attribute_name602 = null;

        attribute_name_return attribute_name604 = null;

        general_attribute_name_return general_attribute_name607 = null;

        function_name_return function_name609 = null;

        file_path_name_return file_path_name611 = null;

        file_path_name_return file_path_name613 = null;


        Object set601_tree=null;
        Object AS603_tree=null;
        Object FUNCTION605_tree=null;
        Object OF606_tree=null;
        Object AS608_tree=null;
        Object FILE610_tree=null;
        Object AS612_tree=null;

        try {
            // Cubrid.g:1017:13: ( ( ATTRIBUTE | COLUMN | METHOD )? general_attribute_name AS attribute_name | FUNCTION OF general_attribute_name AS function_name FILE file_path_name AS file_path_name )
            int alt157=2;
            int LA157_0 = input.LA(1);

            if ( (LA157_0==ATTRIBUTE||LA157_0==CLASS||LA157_0==METHOD||(LA157_0>=ID && LA157_0<=COLUMN)) ) {
                alt157=1;
            }
            else if ( (LA157_0==FUNCTION) ) {
                alt157=2;
            }
            else {
                NoViableAltException nvae =
                    new NoViableAltException("1017:1: alter_rename : ( ( ATTRIBUTE | COLUMN | METHOD )? general_attribute_name AS attribute_name | FUNCTION OF general_attribute_name AS function_name FILE file_path_name AS file_path_name );", 157, 0, input);

                throw nvae;
            }
            switch (alt157) {
                case 1 :
                    // Cubrid.g:1018:2: ( ATTRIBUTE | COLUMN | METHOD )? general_attribute_name AS attribute_name
                    {
                    root_0 = (Object)adaptor.nil();

                    // Cubrid.g:1018:2: ( ATTRIBUTE | COLUMN | METHOD )?
                    int alt156=2;
                    int LA156_0 = input.LA(1);

                    if ( (LA156_0==COLUMN) ) {
                        int LA156_1 = input.LA(2);

                        if ( (LA156_1==CLASS||(LA156_1>=ID && LA156_1<=COLUMN)) ) {
                            alt156=1;
                        }
                    }
                    else if ( (LA156_0==ATTRIBUTE||LA156_0==METHOD) ) {
                        alt156=1;
                    }
                    switch (alt156) {
                        case 1 :
                            // Cubrid.g:
                            {
                            set601=(Token)input.LT(1);
                            if ( input.LA(1)==ATTRIBUTE||input.LA(1)==METHOD||input.LA(1)==COLUMN ) {
                                input.consume();
                                adaptor.addChild(root_0, adaptor.create(set601));
                                errorRecovery=false;
                            }
                            else {
                                MismatchedSetException mse =
                                    new MismatchedSetException(null,input);
                                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_alter_rename5563);    throw mse;
                            }


                            }
                            break;

                    }

                    pushFollow(FOLLOW_general_attribute_name_in_alter_rename5577);
                    general_attribute_name602=general_attribute_name();
                    _fsp--;

                    adaptor.addChild(root_0, general_attribute_name602.getTree());
                    AS603=(Token)input.LT(1);
                    match(input,AS,FOLLOW_AS_in_alter_rename5579); 
                    AS603_tree = (Object)adaptor.create(AS603);
                    adaptor.addChild(root_0, AS603_tree);

                    pushFollow(FOLLOW_attribute_name_in_alter_rename5581);
                    attribute_name604=attribute_name();
                    _fsp--;

                    adaptor.addChild(root_0, attribute_name604.getTree());

                    }
                    break;
                case 2 :
                    // Cubrid.g:1020:4: FUNCTION OF general_attribute_name AS function_name FILE file_path_name AS file_path_name
                    {
                    root_0 = (Object)adaptor.nil();

                    FUNCTION605=(Token)input.LT(1);
                    match(input,FUNCTION,FOLLOW_FUNCTION_in_alter_rename5586); 
                    FUNCTION605_tree = (Object)adaptor.create(FUNCTION605);
                    adaptor.addChild(root_0, FUNCTION605_tree);

                    OF606=(Token)input.LT(1);
                    match(input,OF,FOLLOW_OF_in_alter_rename5588); 
                    OF606_tree = (Object)adaptor.create(OF606);
                    adaptor.addChild(root_0, OF606_tree);

                    pushFollow(FOLLOW_general_attribute_name_in_alter_rename5590);
                    general_attribute_name607=general_attribute_name();
                    _fsp--;

                    adaptor.addChild(root_0, general_attribute_name607.getTree());
                    AS608=(Token)input.LT(1);
                    match(input,AS,FOLLOW_AS_in_alter_rename5592); 
                    AS608_tree = (Object)adaptor.create(AS608);
                    adaptor.addChild(root_0, AS608_tree);

                    pushFollow(FOLLOW_function_name_in_alter_rename5594);
                    function_name609=function_name();
                    _fsp--;

                    adaptor.addChild(root_0, function_name609.getTree());
                    FILE610=(Token)input.LT(1);
                    match(input,FILE,FOLLOW_FILE_in_alter_rename5597); 
                    FILE610_tree = (Object)adaptor.create(FILE610);
                    adaptor.addChild(root_0, FILE610_tree);

                    pushFollow(FOLLOW_file_path_name_in_alter_rename5599);
                    file_path_name611=file_path_name();
                    _fsp--;

                    adaptor.addChild(root_0, file_path_name611.getTree());
                    AS612=(Token)input.LT(1);
                    match(input,AS,FOLLOW_AS_in_alter_rename5601); 
                    AS612_tree = (Object)adaptor.create(AS612);
                    adaptor.addChild(root_0, AS612_tree);

                    pushFollow(FOLLOW_file_path_name_in_alter_rename5603);
                    file_path_name613=file_path_name();
                    _fsp--;

                    adaptor.addChild(root_0, file_path_name613.getTree());

                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end alter_rename

    public static class value_specifiation_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start value_specifiation
    // Cubrid.g:1024:1: value_specifiation : value ;
    public final value_specifiation_return value_specifiation() throws RecognitionException {
        value_specifiation_return retval = new value_specifiation_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        value_return value614 = null;



        try {
            // Cubrid.g:1024:19: ( value )
            // Cubrid.g:1025:2: value
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_value_in_value_specifiation5616);
            value614=value();
            _fsp--;

            adaptor.addChild(root_0, value614.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end value_specifiation

    public static class file_path_name_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start file_path_name
    // Cubrid.g:1028:1: file_path_name : PATH ;
    public final file_path_name_return file_path_name() throws RecognitionException {
        file_path_name_return retval = new file_path_name_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token PATH615=null;

        Object PATH615_tree=null;

        try {
            // Cubrid.g:1028:15: ( PATH )
            // Cubrid.g:1029:2: PATH
            {
            root_0 = (Object)adaptor.nil();

            PATH615=(Token)input.LT(1);
            match(input,PATH,FOLLOW_PATH_in_file_path_name5627); 
            PATH615_tree = (Object)adaptor.create(PATH615);
            adaptor.addChild(root_0, PATH615_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end file_path_name

    public static class call_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start call
    // Cubrid.g:1034:1: call : CALL method_name STARTBRACKET ( argument_comma_list )? ENDBRACKET ON call_target ( to_variable )? ;
    public final call_return call() throws RecognitionException {
        call_return retval = new call_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token CALL616=null;
        Token STARTBRACKET618=null;
        Token ENDBRACKET620=null;
        Token ON621=null;
        method_name_return method_name617 = null;

        argument_comma_list_return argument_comma_list619 = null;

        call_target_return call_target622 = null;

        to_variable_return to_variable623 = null;


        Object CALL616_tree=null;
        Object STARTBRACKET618_tree=null;
        Object ENDBRACKET620_tree=null;
        Object ON621_tree=null;

        try {
            // Cubrid.g:1034:5: ( CALL method_name STARTBRACKET ( argument_comma_list )? ENDBRACKET ON call_target ( to_variable )? )
            // Cubrid.g:1035:2: CALL method_name STARTBRACKET ( argument_comma_list )? ENDBRACKET ON call_target ( to_variable )?
            {
            root_0 = (Object)adaptor.nil();

            CALL616=(Token)input.LT(1);
            match(input,CALL,FOLLOW_CALL_in_call5640); 
            CALL616_tree = (Object)adaptor.create(CALL616);
            adaptor.addChild(root_0, CALL616_tree);

            pushFollow(FOLLOW_method_name_in_call5642);
            method_name617=method_name();
            _fsp--;

            adaptor.addChild(root_0, method_name617.getTree());
            STARTBRACKET618=(Token)input.LT(1);
            match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_call5644); 
            STARTBRACKET618_tree = (Object)adaptor.create(STARTBRACKET618);
            adaptor.addChild(root_0, STARTBRACKET618_tree);

            // Cubrid.g:1036:2: ( argument_comma_list )?
            int alt158=2;
            int LA158_0 = input.LA(1);

            if ( (LA158_0==CASE||LA158_0==EXISTS||(LA158_0>=NOT && LA158_0<=NULL)||LA158_0==STAR||LA158_0==QUOTA||(LA158_0>=DOLLAR && LA158_0<=Q_MARK)||(LA158_0>=ID && LA158_0<=STARTBRACKET)||LA158_0==DECIMALLITERAL||LA158_0==STRING||LA158_0==172) ) {
                alt158=1;
            }
            switch (alt158) {
                case 1 :
                    // Cubrid.g:1036:2: argument_comma_list
                    {
                    pushFollow(FOLLOW_argument_comma_list_in_call5648);
                    argument_comma_list619=argument_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, argument_comma_list619.getTree());

                    }
                    break;

            }

            ENDBRACKET620=(Token)input.LT(1);
            match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_call5652); 
            ENDBRACKET620_tree = (Object)adaptor.create(ENDBRACKET620);
            adaptor.addChild(root_0, ENDBRACKET620_tree);

            ON621=(Token)input.LT(1);
            match(input,ON,FOLLOW_ON_in_call5655); 
            ON621_tree = (Object)adaptor.create(ON621);
            adaptor.addChild(root_0, ON621_tree);

            pushFollow(FOLLOW_call_target_in_call5657);
            call_target622=call_target();
            _fsp--;

            adaptor.addChild(root_0, call_target622.getTree());
            // Cubrid.g:1038:17: ( to_variable )?
            int alt159=2;
            int LA159_0 = input.LA(1);

            if ( (LA159_0==TO) ) {
                alt159=1;
            }
            switch (alt159) {
                case 1 :
                    // Cubrid.g:1038:18: to_variable
                    {
                    pushFollow(FOLLOW_to_variable_in_call5660);
                    to_variable623=to_variable();
                    _fsp--;

                    adaptor.addChild(root_0, to_variable623.getTree());

                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end call

    public static class method_name_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start method_name
    // Cubrid.g:1041:1: method_name : ID ;
    public final method_name_return method_name() throws RecognitionException {
        method_name_return retval = new method_name_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ID624=null;

        Object ID624_tree=null;

        try {
            // Cubrid.g:1041:12: ( ID )
            // Cubrid.g:1042:2: ID
            {
            root_0 = (Object)adaptor.nil();

            ID624=(Token)input.LT(1);
            match(input,ID,FOLLOW_ID_in_method_name5672); 
            ID624_tree = (Object)adaptor.create(ID624);
            adaptor.addChild(root_0, ID624_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end method_name

    public static class call_target_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start call_target
    // Cubrid.g:1045:1: call_target : ( variable_name | metaclass_specification );
    public final call_target_return call_target() throws RecognitionException {
        call_target_return retval = new call_target_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        variable_name_return variable_name625 = null;

        metaclass_specification_return metaclass_specification626 = null;



        try {
            // Cubrid.g:1045:12: ( variable_name | metaclass_specification )
            int alt160=2;
            int LA160_0 = input.LA(1);

            if ( (LA160_0==ID) ) {
                alt160=1;
            }
            else if ( (LA160_0==CLASS) ) {
                alt160=2;
            }
            else {
                NoViableAltException nvae =
                    new NoViableAltException("1045:1: call_target : ( variable_name | metaclass_specification );", 160, 0, input);

                throw nvae;
            }
            switch (alt160) {
                case 1 :
                    // Cubrid.g:1046:2: variable_name
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_variable_name_in_call_target5683);
                    variable_name625=variable_name();
                    _fsp--;

                    adaptor.addChild(root_0, variable_name625.getTree());

                    }
                    break;
                case 2 :
                    // Cubrid.g:1046:18: metaclass_specification
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_metaclass_specification_in_call_target5687);
                    metaclass_specification626=metaclass_specification();
                    _fsp--;

                    adaptor.addChild(root_0, metaclass_specification626.getTree());

                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end call_target

    public static class variable_name_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start variable_name
    // Cubrid.g:1049:1: variable_name : ID ;
    public final variable_name_return variable_name() throws RecognitionException {
        variable_name_return retval = new variable_name_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token ID627=null;

        Object ID627_tree=null;

        try {
            // Cubrid.g:1049:14: ( ID )
            // Cubrid.g:1050:2: ID
            {
            root_0 = (Object)adaptor.nil();

            ID627=(Token)input.LT(1);
            match(input,ID,FOLLOW_ID_in_variable_name5698); 
            ID627_tree = (Object)adaptor.create(ID627);
            adaptor.addChild(root_0, ID627_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end variable_name

    public static class to_variable_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start to_variable
    // Cubrid.g:1053:1: to_variable : TO variable ;
    public final to_variable_return to_variable() throws RecognitionException {
        to_variable_return retval = new to_variable_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token TO628=null;
        variable_return variable629 = null;


        Object TO628_tree=null;

        try {
            // Cubrid.g:1053:12: ( TO variable )
            // Cubrid.g:1054:2: TO variable
            {
            root_0 = (Object)adaptor.nil();

            TO628=(Token)input.LT(1);
            match(input,TO,FOLLOW_TO_in_to_variable5709); 
            TO628_tree = (Object)adaptor.create(TO628);
            adaptor.addChild(root_0, TO628_tree);

            pushFollow(FOLLOW_variable_in_to_variable5711);
            variable629=variable();
            _fsp--;

            adaptor.addChild(root_0, variable629.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end to_variable

    public static class where_clause_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start where_clause
    // Cubrid.g:1058:1: where_clause : ( WHERE search_condition )? -> ( UNTAB WHERE ENTER TAB search_condition )? ENTER ;
    public final where_clause_return where_clause() throws RecognitionException {
        where_clause_return retval = new where_clause_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token WHERE630=null;
        search_condition_return search_condition631 = null;


        Object WHERE630_tree=null;
        RewriteRuleTokenStream stream_WHERE=new RewriteRuleTokenStream(adaptor,"token WHERE");
        RewriteRuleSubtreeStream stream_search_condition=new RewriteRuleSubtreeStream(adaptor,"rule search_condition");
        try {
            // Cubrid.g:1058:13: ( ( WHERE search_condition )? -> ( UNTAB WHERE ENTER TAB search_condition )? ENTER )
            // Cubrid.g:1059:2: ( WHERE search_condition )?
            {
            // Cubrid.g:1059:2: ( WHERE search_condition )?
            int alt161=2;
            int LA161_0 = input.LA(1);

            if ( (LA161_0==WHERE) ) {
                alt161=1;
            }
            switch (alt161) {
                case 1 :
                    // Cubrid.g:1059:3: WHERE search_condition
                    {
                    WHERE630=(Token)input.LT(1);
                    match(input,WHERE,FOLLOW_WHERE_in_where_clause5727); 
                    stream_WHERE.add(WHERE630);

                    pushFollow(FOLLOW_search_condition_in_where_clause5729);
                    search_condition631=search_condition();
                    _fsp--;

                    stream_search_condition.add(search_condition631.getTree());

                    }
                    break;

            }


            // AST REWRITE
            // elements: search_condition, WHERE
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 1060:2: -> ( UNTAB WHERE ENTER TAB search_condition )? ENTER
            {
                // Cubrid.g:1061:2: ( UNTAB WHERE ENTER TAB search_condition )?
                if ( stream_search_condition.hasNext()||stream_WHERE.hasNext() ) {
                    adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));
                    adaptor.addChild(root_0, stream_WHERE.next());
                    adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                    adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));
                    adaptor.addChild(root_0, stream_search_condition.next());

                }
                stream_search_condition.reset();
                stream_WHERE.reset();
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end where_clause

    public static class function_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start function
    // Cubrid.g:1064:1: function : function_name STARTBRACKET ( argument_comma_list )? ENDBRACKET ;
    public final function_return function() throws RecognitionException {
        function_return retval = new function_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token STARTBRACKET633=null;
        Token ENDBRACKET635=null;
        function_name_return function_name632 = null;

        argument_comma_list_return argument_comma_list634 = null;


        Object STARTBRACKET633_tree=null;
        Object ENDBRACKET635_tree=null;

        try {
            // Cubrid.g:1064:9: ( function_name STARTBRACKET ( argument_comma_list )? ENDBRACKET )
            // Cubrid.g:1065:2: function_name STARTBRACKET ( argument_comma_list )? ENDBRACKET
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_function_name_in_function5762);
            function_name632=function_name();
            _fsp--;

            adaptor.addChild(root_0, function_name632.getTree());
            STARTBRACKET633=(Token)input.LT(1);
            match(input,STARTBRACKET,FOLLOW_STARTBRACKET_in_function5766); 
            STARTBRACKET633_tree = (Object)adaptor.create(STARTBRACKET633);
            adaptor.addChild(root_0, STARTBRACKET633_tree);

            // Cubrid.g:1067:2: ( argument_comma_list )?
            int alt162=2;
            int LA162_0 = input.LA(1);

            if ( (LA162_0==CASE||LA162_0==EXISTS||(LA162_0>=NOT && LA162_0<=NULL)||LA162_0==STAR||LA162_0==QUOTA||(LA162_0>=DOLLAR && LA162_0<=Q_MARK)||(LA162_0>=ID && LA162_0<=STARTBRACKET)||LA162_0==DECIMALLITERAL||LA162_0==STRING||LA162_0==172) ) {
                alt162=1;
            }
            switch (alt162) {
                case 1 :
                    // Cubrid.g:1067:2: argument_comma_list
                    {
                    pushFollow(FOLLOW_argument_comma_list_in_function5770);
                    argument_comma_list634=argument_comma_list();
                    _fsp--;

                    adaptor.addChild(root_0, argument_comma_list634.getTree());

                    }
                    break;

            }

            ENDBRACKET635=(Token)input.LT(1);
            match(input,ENDBRACKET,FOLLOW_ENDBRACKET_in_function5774); 
            ENDBRACKET635_tree = (Object)adaptor.create(ENDBRACKET635);
            adaptor.addChild(root_0, ENDBRACKET635_tree);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end function

    public static class argument_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start argument_comma_list
    // Cubrid.g:1070:1: argument_comma_list : argument ( COMMA argument )* ;
    public final argument_comma_list_return argument_comma_list() throws RecognitionException {
        argument_comma_list_return retval = new argument_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA637=null;
        argument_return argument636 = null;

        argument_return argument638 = null;


        Object COMMA637_tree=null;

        try {
            // Cubrid.g:1070:20: ( argument ( COMMA argument )* )
            // Cubrid.g:1071:2: argument ( COMMA argument )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_argument_in_argument_comma_list5783);
            argument636=argument();
            _fsp--;

            adaptor.addChild(root_0, argument636.getTree());
            // Cubrid.g:1071:11: ( COMMA argument )*
            loop163:
            do {
                int alt163=2;
                int LA163_0 = input.LA(1);

                if ( (LA163_0==COMMA) ) {
                    alt163=1;
                }


                switch (alt163) {
            	case 1 :
            	    // Cubrid.g:1071:12: COMMA argument
            	    {
            	    COMMA637=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_argument_comma_list5786); 
            	    COMMA637_tree = (Object)adaptor.create(COMMA637);
            	    adaptor.addChild(root_0, COMMA637_tree);

            	    pushFollow(FOLLOW_argument_in_argument_comma_list5788);
            	    argument638=argument();
            	    _fsp--;

            	    adaptor.addChild(root_0, argument638.getTree());

            	    }
            	    break;

            	default :
            	    break loop163;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end argument_comma_list

    public static class argument_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start argument
    // Cubrid.g:1074:1: argument : expression ( AS privative_type )? ;
    public final argument_return argument() throws RecognitionException {
        argument_return retval = new argument_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token AS640=null;
        expression_return expression639 = null;

        privative_type_return privative_type641 = null;


        Object AS640_tree=null;

        try {
            // Cubrid.g:1074:9: ( expression ( AS privative_type )? )
            // Cubrid.g:1075:2: expression ( AS privative_type )?
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_expression_in_argument5801);
            expression639=expression();
            _fsp--;

            adaptor.addChild(root_0, expression639.getTree());
            // Cubrid.g:1075:13: ( AS privative_type )?
            int alt164=2;
            int LA164_0 = input.LA(1);

            if ( (LA164_0==AS) ) {
                alt164=1;
            }
            switch (alt164) {
                case 1 :
                    // Cubrid.g:1075:14: AS privative_type
                    {
                    AS640=(Token)input.LT(1);
                    match(input,AS,FOLLOW_AS_in_argument5804); 
                    AS640_tree = (Object)adaptor.create(AS640);
                    adaptor.addChild(root_0, AS640_tree);

                    pushFollow(FOLLOW_privative_type_in_argument5806);
                    privative_type641=privative_type();
                    _fsp--;

                    adaptor.addChild(root_0, privative_type641.getTree());

                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end argument

    public static class operation_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start operation
    // Cubrid.g:1089:1: operation : ( AND | OR );
    public final operation_return operation() throws RecognitionException {
        operation_return retval = new operation_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token set642=null;

        Object set642_tree=null;

        try {
            // Cubrid.g:1089:10: ( AND | OR )
            // Cubrid.g:
            {
            root_0 = (Object)adaptor.nil();

            set642=(Token)input.LT(1);
            if ( input.LA(1)==AND||input.LA(1)==OR ) {
                input.consume();
                adaptor.addChild(root_0, adaptor.create(set642));
                errorRecovery=false;
            }
            else {
                MismatchedSetException mse =
                    new MismatchedSetException(null,input);
                recoverFromMismatchedSet(input,mse,FOLLOW_set_in_operation0);    throw mse;
            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end operation

    public static class value_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start value
    // Cubrid.g:1093:1: value : ( STRING | ( QUOTA number QUOTA ) | number | currency | Q_MARK );
    public final value_return value() throws RecognitionException {
        value_return retval = new value_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token STRING643=null;
        Token QUOTA644=null;
        Token QUOTA646=null;
        Token Q_MARK649=null;
        number_return number645 = null;

        number_return number647 = null;

        currency_return currency648 = null;


        Object STRING643_tree=null;
        Object QUOTA644_tree=null;
        Object QUOTA646_tree=null;
        Object Q_MARK649_tree=null;

        try {
            // Cubrid.g:1093:6: ( STRING | ( QUOTA number QUOTA ) | number | currency | Q_MARK )
            int alt165=5;
            switch ( input.LA(1) ) {
            case STRING:
                {
                alt165=1;
                }
                break;
            case QUOTA:
                {
                alt165=2;
                }
                break;
            case DECIMALLITERAL:
            case 172:
                {
                alt165=3;
                }
                break;
            case DOLLAR:
                {
                alt165=4;
                }
                break;
            case Q_MARK:
                {
                alt165=5;
                }
                break;
            default:
                NoViableAltException nvae =
                    new NoViableAltException("1093:1: value : ( STRING | ( QUOTA number QUOTA ) | number | currency | Q_MARK );", 165, 0, input);

                throw nvae;
            }

            switch (alt165) {
                case 1 :
                    // Cubrid.g:1094:2: STRING
                    {
                    root_0 = (Object)adaptor.nil();

                    STRING643=(Token)input.LT(1);
                    match(input,STRING,FOLLOW_STRING_in_value5842); 
                    STRING643_tree = (Object)adaptor.create(STRING643);
                    adaptor.addChild(root_0, STRING643_tree);


                    }
                    break;
                case 2 :
                    // Cubrid.g:1095:3: ( QUOTA number QUOTA )
                    {
                    root_0 = (Object)adaptor.nil();

                    // Cubrid.g:1095:3: ( QUOTA number QUOTA )
                    // Cubrid.g:1095:4: QUOTA number QUOTA
                    {
                    QUOTA644=(Token)input.LT(1);
                    match(input,QUOTA,FOLLOW_QUOTA_in_value5847); 
                    QUOTA644_tree = (Object)adaptor.create(QUOTA644);
                    adaptor.addChild(root_0, QUOTA644_tree);

                    pushFollow(FOLLOW_number_in_value5849);
                    number645=number();
                    _fsp--;

                    adaptor.addChild(root_0, number645.getTree());
                    QUOTA646=(Token)input.LT(1);
                    match(input,QUOTA,FOLLOW_QUOTA_in_value5851); 
                    QUOTA646_tree = (Object)adaptor.create(QUOTA646);
                    adaptor.addChild(root_0, QUOTA646_tree);


                    }


                    }
                    break;
                case 3 :
                    // Cubrid.g:1096:4: number
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_number_in_value5859);
                    number647=number();
                    _fsp--;

                    adaptor.addChild(root_0, number647.getTree());

                    }
                    break;
                case 4 :
                    // Cubrid.g:1097:4: currency
                    {
                    root_0 = (Object)adaptor.nil();

                    pushFollow(FOLLOW_currency_in_value5864);
                    currency648=currency();
                    _fsp--;

                    adaptor.addChild(root_0, currency648.getTree());

                    }
                    break;
                case 5 :
                    // Cubrid.g:1098:4: Q_MARK
                    {
                    root_0 = (Object)adaptor.nil();

                    Q_MARK649=(Token)input.LT(1);
                    match(input,Q_MARK,FOLLOW_Q_MARK_in_value5869); 
                    Q_MARK649_tree = (Object)adaptor.create(Q_MARK649);
                    adaptor.addChild(root_0, Q_MARK649_tree);


                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end value

    public static class value_comma_list_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start value_comma_list
    // Cubrid.g:1101:1: value_comma_list : value ( COMMA value )* ;
    public final value_comma_list_return value_comma_list() throws RecognitionException {
        value_comma_list_return retval = new value_comma_list_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token COMMA651=null;
        value_return value650 = null;

        value_return value652 = null;


        Object COMMA651_tree=null;

        try {
            // Cubrid.g:1101:17: ( value ( COMMA value )* )
            // Cubrid.g:1102:2: value ( COMMA value )*
            {
            root_0 = (Object)adaptor.nil();

            pushFollow(FOLLOW_value_in_value_comma_list5879);
            value650=value();
            _fsp--;

            adaptor.addChild(root_0, value650.getTree());
            // Cubrid.g:1102:8: ( COMMA value )*
            loop166:
            do {
                int alt166=2;
                int LA166_0 = input.LA(1);

                if ( (LA166_0==COMMA) ) {
                    alt166=1;
                }


                switch (alt166) {
            	case 1 :
            	    // Cubrid.g:1102:9: COMMA value
            	    {
            	    COMMA651=(Token)input.LT(1);
            	    match(input,COMMA,FOLLOW_COMMA_in_value_comma_list5882); 
            	    COMMA651_tree = (Object)adaptor.create(COMMA651);
            	    adaptor.addChild(root_0, COMMA651_tree);

            	    pushFollow(FOLLOW_value_in_value_comma_list5884);
            	    value652=value();
            	    _fsp--;

            	    adaptor.addChild(root_0, value652.getTree());

            	    }
            	    break;

            	default :
            	    break loop166;
                }
            } while (true);


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end value_comma_list

    public static class currency_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start currency
    // Cubrid.g:1111:1: currency : DOLLAR number ;
    public final currency_return currency() throws RecognitionException {
        currency_return retval = new currency_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token DOLLAR653=null;
        number_return number654 = null;


        Object DOLLAR653_tree=null;

        try {
            // Cubrid.g:1111:9: ( DOLLAR number )
            // Cubrid.g:1112:2: DOLLAR number
            {
            root_0 = (Object)adaptor.nil();

            DOLLAR653=(Token)input.LT(1);
            match(input,DOLLAR,FOLLOW_DOLLAR_in_currency5914); 
            DOLLAR653_tree = (Object)adaptor.create(DOLLAR653);
            adaptor.addChild(root_0, DOLLAR653_tree);

            pushFollow(FOLLOW_number_in_currency5916);
            number654=number();
            _fsp--;

            adaptor.addChild(root_0, number654.getTree());

            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end currency

    public static class express_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start express
    // Cubrid.g:1115:1: express : ( '=' | '<' | '>' | '<=' | '>=' | '<>' | '+' | '-' | STAR | '/' | ( ( NOT )? ) EXISTS | ( ( NOT )? ) IN | CONNECT | LIKE );
    public final express_return express() throws RecognitionException {
        express_return retval = new express_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token char_literal655=null;
        Token char_literal656=null;
        Token char_literal657=null;
        Token string_literal658=null;
        Token string_literal659=null;
        Token string_literal660=null;
        Token char_literal661=null;
        Token char_literal662=null;
        Token STAR663=null;
        Token char_literal664=null;
        Token NOT665=null;
        Token EXISTS666=null;
        Token NOT667=null;
        Token IN668=null;
        Token CONNECT669=null;
        Token LIKE670=null;

        Object char_literal655_tree=null;
        Object char_literal656_tree=null;
        Object char_literal657_tree=null;
        Object string_literal658_tree=null;
        Object string_literal659_tree=null;
        Object string_literal660_tree=null;
        Object char_literal661_tree=null;
        Object char_literal662_tree=null;
        Object STAR663_tree=null;
        Object char_literal664_tree=null;
        Object NOT665_tree=null;
        Object EXISTS666_tree=null;
        Object NOT667_tree=null;
        Object IN668_tree=null;
        Object CONNECT669_tree=null;
        Object LIKE670_tree=null;

        try {
            // Cubrid.g:1115:8: ( '=' | '<' | '>' | '<=' | '>=' | '<>' | '+' | '-' | STAR | '/' | ( ( NOT )? ) EXISTS | ( ( NOT )? ) IN | CONNECT | LIKE )
            int alt169=14;
            switch ( input.LA(1) ) {
            case EQUAL:
                {
                alt169=1;
                }
                break;
            case 170:
                {
                alt169=2;
                }
                break;
            case 171:
                {
                alt169=3;
                }
                break;
            case 168:
                {
                alt169=4;
                }
                break;
            case 169:
                {
                alt169=5;
                }
                break;
            case 167:
                {
                alt169=6;
                }
                break;
            case 166:
                {
                alt169=7;
                }
                break;
            case 172:
                {
                alt169=8;
                }
                break;
            case STAR:
                {
                alt169=9;
                }
                break;
            case 173:
                {
                alt169=10;
                }
                break;
            case NOT:
                {
                int LA169_11 = input.LA(2);

                if ( (LA169_11==EXISTS) ) {
                    alt169=11;
                }
                else if ( (LA169_11==IN) ) {
                    alt169=12;
                }
                else {
                    NoViableAltException nvae =
                        new NoViableAltException("1115:1: express : ( '=' | '<' | '>' | '<=' | '>=' | '<>' | '+' | '-' | STAR | '/' | ( ( NOT )? ) EXISTS | ( ( NOT )? ) IN | CONNECT | LIKE );", 169, 11, input);

                    throw nvae;
                }
                }
                break;
            case EXISTS:
                {
                alt169=11;
                }
                break;
            case IN:
                {
                alt169=12;
                }
                break;
            case CONNECT:
                {
                alt169=13;
                }
                break;
            case LIKE:
                {
                alt169=14;
                }
                break;
            default:
                NoViableAltException nvae =
                    new NoViableAltException("1115:1: express : ( '=' | '<' | '>' | '<=' | '>=' | '<>' | '+' | '-' | STAR | '/' | ( ( NOT )? ) EXISTS | ( ( NOT )? ) IN | CONNECT | LIKE );", 169, 0, input);

                throw nvae;
            }

            switch (alt169) {
                case 1 :
                    // Cubrid.g:1116:2: '='
                    {
                    root_0 = (Object)adaptor.nil();

                    char_literal655=(Token)input.LT(1);
                    match(input,EQUAL,FOLLOW_EQUAL_in_express5928); 
                    char_literal655_tree = (Object)adaptor.create(char_literal655);
                    adaptor.addChild(root_0, char_literal655_tree);


                    }
                    break;
                case 2 :
                    // Cubrid.g:1117:4: '<'
                    {
                    root_0 = (Object)adaptor.nil();

                    char_literal656=(Token)input.LT(1);
                    match(input,170,FOLLOW_170_in_express5934); 
                    char_literal656_tree = (Object)adaptor.create(char_literal656);
                    adaptor.addChild(root_0, char_literal656_tree);


                    }
                    break;
                case 3 :
                    // Cubrid.g:1118:4: '>'
                    {
                    root_0 = (Object)adaptor.nil();

                    char_literal657=(Token)input.LT(1);
                    match(input,171,FOLLOW_171_in_express5940); 
                    char_literal657_tree = (Object)adaptor.create(char_literal657);
                    adaptor.addChild(root_0, char_literal657_tree);


                    }
                    break;
                case 4 :
                    // Cubrid.g:1119:4: '<='
                    {
                    root_0 = (Object)adaptor.nil();

                    string_literal658=(Token)input.LT(1);
                    match(input,168,FOLLOW_168_in_express5946); 
                    string_literal658_tree = (Object)adaptor.create(string_literal658);
                    adaptor.addChild(root_0, string_literal658_tree);


                    }
                    break;
                case 5 :
                    // Cubrid.g:1120:4: '>='
                    {
                    root_0 = (Object)adaptor.nil();

                    string_literal659=(Token)input.LT(1);
                    match(input,169,FOLLOW_169_in_express5952); 
                    string_literal659_tree = (Object)adaptor.create(string_literal659);
                    adaptor.addChild(root_0, string_literal659_tree);


                    }
                    break;
                case 6 :
                    // Cubrid.g:1121:4: '<>'
                    {
                    root_0 = (Object)adaptor.nil();

                    string_literal660=(Token)input.LT(1);
                    match(input,167,FOLLOW_167_in_express5958); 
                    string_literal660_tree = (Object)adaptor.create(string_literal660);
                    adaptor.addChild(root_0, string_literal660_tree);


                    }
                    break;
                case 7 :
                    // Cubrid.g:1122:4: '+'
                    {
                    root_0 = (Object)adaptor.nil();

                    char_literal661=(Token)input.LT(1);
                    match(input,166,FOLLOW_166_in_express5963); 
                    char_literal661_tree = (Object)adaptor.create(char_literal661);
                    adaptor.addChild(root_0, char_literal661_tree);


                    }
                    break;
                case 8 :
                    // Cubrid.g:1123:4: '-'
                    {
                    root_0 = (Object)adaptor.nil();

                    char_literal662=(Token)input.LT(1);
                    match(input,172,FOLLOW_172_in_express5968); 
                    char_literal662_tree = (Object)adaptor.create(char_literal662);
                    adaptor.addChild(root_0, char_literal662_tree);


                    }
                    break;
                case 9 :
                    // Cubrid.g:1124:4: STAR
                    {
                    root_0 = (Object)adaptor.nil();

                    STAR663=(Token)input.LT(1);
                    match(input,STAR,FOLLOW_STAR_in_express5973); 
                    STAR663_tree = (Object)adaptor.create(STAR663);
                    adaptor.addChild(root_0, STAR663_tree);


                    }
                    break;
                case 10 :
                    // Cubrid.g:1125:4: '/'
                    {
                    root_0 = (Object)adaptor.nil();

                    char_literal664=(Token)input.LT(1);
                    match(input,173,FOLLOW_173_in_express5978); 
                    char_literal664_tree = (Object)adaptor.create(char_literal664);
                    adaptor.addChild(root_0, char_literal664_tree);


                    }
                    break;
                case 11 :
                    // Cubrid.g:1126:4: ( ( NOT )? ) EXISTS
                    {
                    root_0 = (Object)adaptor.nil();

                    // Cubrid.g:1126:4: ( ( NOT )? )
                    // Cubrid.g:1126:5: ( NOT )?
                    {
                    // Cubrid.g:1126:5: ( NOT )?
                    int alt167=2;
                    int LA167_0 = input.LA(1);

                    if ( (LA167_0==NOT) ) {
                        alt167=1;
                    }
                    switch (alt167) {
                        case 1 :
                            // Cubrid.g:1126:5: NOT
                            {
                            NOT665=(Token)input.LT(1);
                            match(input,NOT,FOLLOW_NOT_in_express5984); 
                            NOT665_tree = (Object)adaptor.create(NOT665);
                            adaptor.addChild(root_0, NOT665_tree);


                            }
                            break;

                    }


                    }

                    EXISTS666=(Token)input.LT(1);
                    match(input,EXISTS,FOLLOW_EXISTS_in_express5988); 
                    EXISTS666_tree = (Object)adaptor.create(EXISTS666);
                    adaptor.addChild(root_0, EXISTS666_tree);


                    }
                    break;
                case 12 :
                    // Cubrid.g:1127:4: ( ( NOT )? ) IN
                    {
                    root_0 = (Object)adaptor.nil();

                    // Cubrid.g:1127:4: ( ( NOT )? )
                    // Cubrid.g:1127:5: ( NOT )?
                    {
                    // Cubrid.g:1127:5: ( NOT )?
                    int alt168=2;
                    int LA168_0 = input.LA(1);

                    if ( (LA168_0==NOT) ) {
                        alt168=1;
                    }
                    switch (alt168) {
                        case 1 :
                            // Cubrid.g:1127:5: NOT
                            {
                            NOT667=(Token)input.LT(1);
                            match(input,NOT,FOLLOW_NOT_in_express5994); 
                            NOT667_tree = (Object)adaptor.create(NOT667);
                            adaptor.addChild(root_0, NOT667_tree);


                            }
                            break;

                    }


                    }

                    IN668=(Token)input.LT(1);
                    match(input,IN,FOLLOW_IN_in_express5998); 
                    IN668_tree = (Object)adaptor.create(IN668);
                    adaptor.addChild(root_0, IN668_tree);


                    }
                    break;
                case 13 :
                    // Cubrid.g:1128:4: CONNECT
                    {
                    root_0 = (Object)adaptor.nil();

                    CONNECT669=(Token)input.LT(1);
                    match(input,CONNECT,FOLLOW_CONNECT_in_express6003); 
                    CONNECT669_tree = (Object)adaptor.create(CONNECT669);
                    adaptor.addChild(root_0, CONNECT669_tree);


                    }
                    break;
                case 14 :
                    // Cubrid.g:1129:4: LIKE
                    {
                    root_0 = (Object)adaptor.nil();

                    LIKE670=(Token)input.LT(1);
                    match(input,LIKE,FOLLOW_LIKE_in_express6008); 
                    LIKE670_tree = (Object)adaptor.create(LIKE670);
                    adaptor.addChild(root_0, LIKE670_tree);


                    }
                    break;

            }
            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end express

    public static class number_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start number
    // Cubrid.g:1131:1: number : ( '-' )? DECIMALLITERAL ( DOT DECIMALLITERAL )? ;
    public final number_return number() throws RecognitionException {
        number_return retval = new number_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token char_literal671=null;
        Token DECIMALLITERAL672=null;
        Token DOT673=null;
        Token DECIMALLITERAL674=null;

        Object char_literal671_tree=null;
        Object DECIMALLITERAL672_tree=null;
        Object DOT673_tree=null;
        Object DECIMALLITERAL674_tree=null;

        try {
            // Cubrid.g:1131:7: ( ( '-' )? DECIMALLITERAL ( DOT DECIMALLITERAL )? )
            // Cubrid.g:1132:3: ( '-' )? DECIMALLITERAL ( DOT DECIMALLITERAL )?
            {
            root_0 = (Object)adaptor.nil();

            // Cubrid.g:1132:3: ( '-' )?
            int alt170=2;
            int LA170_0 = input.LA(1);

            if ( (LA170_0==172) ) {
                alt170=1;
            }
            switch (alt170) {
                case 1 :
                    // Cubrid.g:1132:4: '-'
                    {
                    char_literal671=(Token)input.LT(1);
                    match(input,172,FOLLOW_172_in_number6022); 
                    char_literal671_tree = (Object)adaptor.create(char_literal671);
                    adaptor.addChild(root_0, char_literal671_tree);


                    }
                    break;

            }

            DECIMALLITERAL672=(Token)input.LT(1);
            match(input,DECIMALLITERAL,FOLLOW_DECIMALLITERAL_in_number6025); 
            DECIMALLITERAL672_tree = (Object)adaptor.create(DECIMALLITERAL672);
            adaptor.addChild(root_0, DECIMALLITERAL672_tree);

            // Cubrid.g:1132:24: ( DOT DECIMALLITERAL )?
            int alt171=2;
            int LA171_0 = input.LA(1);

            if ( (LA171_0==DOT) ) {
                alt171=1;
            }
            switch (alt171) {
                case 1 :
                    // Cubrid.g:1132:26: DOT DECIMALLITERAL
                    {
                    DOT673=(Token)input.LT(1);
                    match(input,DOT,FOLLOW_DOT_in_number6029); 
                    DOT673_tree = (Object)adaptor.create(DOT673);
                    adaptor.addChild(root_0, DOT673_tree);

                    DECIMALLITERAL674=(Token)input.LT(1);
                    match(input,DECIMALLITERAL,FOLLOW_DECIMALLITERAL_in_number6031); 
                    DECIMALLITERAL674_tree = (Object)adaptor.create(DECIMALLITERAL674);
                    adaptor.addChild(root_0, DECIMALLITERAL674_tree);


                    }
                    break;

            }


            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end number

    public static class end_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start end
    // Cubrid.g:1140:1: end : END -> END CLEAR ENTER ;
    public final end_return end() throws RecognitionException {
        end_return retval = new end_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token END675=null;

        Object END675_tree=null;
        RewriteRuleTokenStream stream_END=new RewriteRuleTokenStream(adaptor,"token END");

        try {
            // Cubrid.g:1140:5: ( END -> END CLEAR ENTER )
            // Cubrid.g:1140:7: END
            {
            END675=(Token)input.LT(1);
            match(input,END,FOLLOW_END_in_end6062); 
            stream_END.add(END675);


            // AST REWRITE
            // elements: END
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 1140:11: -> END CLEAR ENTER
            {
                adaptor.addChild(root_0, stream_END.next());
                adaptor.addChild(root_0, adaptor.create(CLEAR, "CLEAR"));
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end end

    public static class select_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start select
    // Cubrid.g:1142:1: select : SELECT -> SELECT ENTER TAB ;
    public final select_return select() throws RecognitionException {
        select_return retval = new select_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token SELECT676=null;

        Object SELECT676_tree=null;
        RewriteRuleTokenStream stream_SELECT=new RewriteRuleTokenStream(adaptor,"token SELECT");

        try {
            // Cubrid.g:1142:8: ( SELECT -> SELECT ENTER TAB )
            // Cubrid.g:1142:10: SELECT
            {
            SELECT676=(Token)input.LT(1);
            match(input,SELECT,FOLLOW_SELECT_in_select6078); 
            stream_SELECT.add(SELECT676);


            // AST REWRITE
            // elements: SELECT
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 1142:17: -> SELECT ENTER TAB
            {
                adaptor.addChild(root_0, stream_SELECT.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end select

    public static class from_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start from
    // Cubrid.g:1144:1: from : FROM -> UNTAB FROM ENTER TAB ;
    public final from_return from() throws RecognitionException {
        from_return retval = new from_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token FROM677=null;

        Object FROM677_tree=null;
        RewriteRuleTokenStream stream_FROM=new RewriteRuleTokenStream(adaptor,"token FROM");

        try {
            // Cubrid.g:1144:6: ( FROM -> UNTAB FROM ENTER TAB )
            // Cubrid.g:1144:8: FROM
            {
            FROM677=(Token)input.LT(1);
            match(input,FROM,FOLLOW_FROM_in_from6094); 
            stream_FROM.add(FROM677);


            // AST REWRITE
            // elements: FROM
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 1144:13: -> UNTAB FROM ENTER TAB
            {
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));
                adaptor.addChild(root_0, stream_FROM.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end from

    public static class where_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start where
    // Cubrid.g:1146:1: where : WHERE -> UNTAB WHERE ENTER TAB ;
    public final where_return where() throws RecognitionException {
        where_return retval = new where_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token WHERE678=null;

        Object WHERE678_tree=null;
        RewriteRuleTokenStream stream_WHERE=new RewriteRuleTokenStream(adaptor,"token WHERE");

        try {
            // Cubrid.g:1146:7: ( WHERE -> UNTAB WHERE ENTER TAB )
            // Cubrid.g:1146:9: WHERE
            {
            WHERE678=(Token)input.LT(1);
            match(input,WHERE,FOLLOW_WHERE_in_where6112); 
            stream_WHERE.add(WHERE678);


            // AST REWRITE
            // elements: WHERE
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 1146:15: -> UNTAB WHERE ENTER TAB
            {
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));
                adaptor.addChild(root_0, stream_WHERE.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end where

    public static class group_by_return extends ParserRuleReturnScope {
        Object tree;
        public Object getTree() { return tree; }
    };

    // $ANTLR start group_by
    // Cubrid.g:1148:1: group_by : GROUP BY -> ENTER UNTAB GROUP BY ENTER TAB ;
    public final group_by_return group_by() throws RecognitionException {
        group_by_return retval = new group_by_return();
        retval.start = input.LT(1);

        Object root_0 = null;

        Token GROUP679=null;
        Token BY680=null;

        Object GROUP679_tree=null;
        Object BY680_tree=null;
        RewriteRuleTokenStream stream_GROUP=new RewriteRuleTokenStream(adaptor,"token GROUP");
        RewriteRuleTokenStream stream_BY=new RewriteRuleTokenStream(adaptor,"token BY");

        try {
            // Cubrid.g:1148:10: ( GROUP BY -> ENTER UNTAB GROUP BY ENTER TAB )
            // Cubrid.g:1148:12: GROUP BY
            {
            GROUP679=(Token)input.LT(1);
            match(input,GROUP,FOLLOW_GROUP_in_group_by6130); 
            stream_GROUP.add(GROUP679);

            BY680=(Token)input.LT(1);
            match(input,BY,FOLLOW_BY_in_group_by6132); 
            stream_BY.add(BY680);


            // AST REWRITE
            // elements: GROUP, BY
            // token labels: 
            // rule labels: retval
            // token list labels: 
            // rule list labels: 
            retval.tree = root_0;
            RewriteRuleSubtreeStream stream_retval=new RewriteRuleSubtreeStream(adaptor,"token retval",retval!=null?retval.tree:null);

            root_0 = (Object)adaptor.nil();
            // 1148:21: -> ENTER UNTAB GROUP BY ENTER TAB
            {
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(UNTAB, "UNTAB"));
                adaptor.addChild(root_0, stream_GROUP.next());
                adaptor.addChild(root_0, stream_BY.next());
                adaptor.addChild(root_0, adaptor.create(ENTER, "ENTER"));
                adaptor.addChild(root_0, adaptor.create(TAB, "TAB"));

            }



            }

            retval.stop = input.LT(-1);

                retval.tree = (Object)adaptor.rulePostProcessing(root_0);
                adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

        }

        catch (RecognitionException e) {
            throw e;
        }

        finally {
        }
        return retval;
    }
    // $ANTLR end group_by


    protected DFA66 dfa66 = new DFA66(this);
    static final String DFA66_eotS =
        "\10\uffff";
    static final String DFA66_eofS =
        "\10\uffff";
    static final String DFA66_minS =
        "\1\42\1\u008d\2\uffff\1\u0081\1\u008d\1\42\1\u0081";
    static final String DFA66_maxS =
        "\1\u008f\1\u008d\2\uffff\1\u0090\1\u008d\1\157\1\u0090";
    static final String DFA66_acceptS =
        "\2\uffff\1\1\1\2\4\uffff";
    static final String DFA66_specialS =
        "\10\uffff}>";
    static final String[] DFA66_transitionS = {
            "\1\3\114\uffff\1\2\37\uffff\1\1",
            "\1\4",
            "",
            "",
            "\1\5\16\uffff\1\6",
            "\1\7",
            "\1\3\114\uffff\1\2",
            "\1\5\16\uffff\1\6"
    };

    static final short[] DFA66_eot = DFA.unpackEncodedString(DFA66_eotS);
    static final short[] DFA66_eof = DFA.unpackEncodedString(DFA66_eofS);
    static final char[] DFA66_min = DFA.unpackEncodedStringToUnsignedChars(DFA66_minS);
    static final char[] DFA66_max = DFA.unpackEncodedStringToUnsignedChars(DFA66_maxS);
    static final short[] DFA66_accept = DFA.unpackEncodedString(DFA66_acceptS);
    static final short[] DFA66_special = DFA.unpackEncodedString(DFA66_specialS);
    static final short[][] DFA66_transition;

    static {
        int numStates = DFA66_transitionS.length;
        DFA66_transition = new short[numStates][];
        for (int i=0; i<numStates; i++) {
            DFA66_transition[i] = DFA.unpackEncodedString(DFA66_transitionS[i]);
        }
    }

    class DFA66 extends DFA {

        public DFA66(BaseRecognizer recognizer) {
            this.recognizer = recognizer;
            this.decisionNumber = 66;
            this.eot = DFA66_eot;
            this.eof = DFA66_eof;
            this.min = DFA66_min;
            this.max = DFA66_max;
            this.accept = DFA66_accept;
            this.special = DFA66_special;
            this.transition = DFA66_transition;
        }
        public String getDescription() {
            return "491:1: insert_spec : ( ( attributes )? value_clause -> ( attributes )? value_clause | ( attributes )? DEFAULT VALUES -> ( attributes )? DEFAULT VALUES );";
        }
    }
 

    public static final BitSet FOLLOW_PATH_in_execute1153 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000001L});
    public static final BitSet FOLLOW_select_statement_in_execute1156 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000001L});
    public static final BitSet FOLLOW_insert_in_execute1160 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000001L});
    public static final BitSet FOLLOW_update_in_execute1165 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000001L});
    public static final BitSet FOLLOW_delete_in_execute1169 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000001L});
    public static final BitSet FOLLOW_create_in_execute1173 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000001L});
    public static final BitSet FOLLOW_create_virtual_class_in_execute1177 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000001L});
    public static final BitSet FOLLOW_alter_in_execute1181 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000001L});
    public static final BitSet FOLLOW_drop_in_execute1185 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000001L});
    public static final BitSet FOLLOW_call_in_execute1189 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000001L});
    public static final BitSet FOLLOW_autocommit_in_execute1193 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000001L});
    public static final BitSet FOLLOW_rollback_in_execute1196 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000001L});
    public static final BitSet FOLLOW_commit_in_execute1200 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000001L});
    public static final BitSet FOLLOW_end_in_execute1203 = new BitSet(new long[]{0x0080008828042082L,0x0004000060000000L,0x0000000000001000L});
    public static final BitSet FOLLOW_AUTOCOMMIT_in_autocommit1218 = new BitSet(new long[]{0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_set_in_autocommit1220 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ROLLBACK_in_rollback1235 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_COMMIT_in_commit1247 = new BitSet(new long[]{0x0000000000000000L,0x0800000000000000L});
    public static final BitSet FOLLOW_WORK_in_commit1249 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_query_specification_in_select_statement1260 = new BitSet(new long[]{0x0000000000000002L,0x0001000000040000L});
    public static final BitSet FOLLOW_UNION_in_select_statement1263 = new BitSet(new long[]{0x0000000000000040L,0x0000000040000000L});
    public static final BitSet FOLLOW_ALL_in_select_statement1265 = new BitSet(new long[]{0x0000000000000000L,0x0000000040000000L});
    public static final BitSet FOLLOW_query_specification_in_select_statement1268 = new BitSet(new long[]{0x0000000000000002L,0x0001000000040000L});
    public static final BitSet FOLLOW_ORDER_in_select_statement1274 = new BitSet(new long[]{0x0000000000020000L});
    public static final BitSet FOLLOW_BY_in_select_statement1276 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000026000L});
    public static final BitSet FOLLOW_sort_specification_comma_list_in_select_statement1278 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_select_in_query_specification1291 = new BitSet(new long[]{0x0000082000080040L,0x0002000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_qualifier_in_query_specification1294 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_select_expressions_in_query_specification1298 = new BitSet(new long[]{0x0800800000000000L,0x0000400000000000L});
    public static final BitSet FOLLOW_set_in_query_specification1304 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000100000222C40L});
    public static final BitSet FOLLOW_variable_comma_list_in_query_specification1312 = new BitSet(new long[]{0x0000800000000000L});
    public static final BitSet FOLLOW_from_in_query_specification1317 = new BitSet(new long[]{0x0000000004000040L,0x0000010000008000L,0x000000000000E000L});
    public static final BitSet FOLLOW_table_specification_comma_list_in_query_specification1319 = new BitSet(new long[]{0x0006000000000002L,0x0208000000000000L});
    public static final BitSet FOLLOW_where_clause_in_query_specification1322 = new BitSet(new long[]{0x0006000000000002L,0x0008000000000000L});
    public static final BitSet FOLLOW_USING_in_query_specification1327 = new BitSet(new long[]{0x0010000000000000L});
    public static final BitSet FOLLOW_INDEX_in_query_specification1329 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_index_comma_list_in_query_specification1331 = new BitSet(new long[]{0x0006000000000002L});
    public static final BitSet FOLLOW_group_by_in_query_specification1337 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_path_expression_comma_list_in_query_specification1339 = new BitSet(new long[]{0x0004000000000002L});
    public static final BitSet FOLLOW_HAVING_in_query_specification1345 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_search_condition_in_query_specification1347 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_set_in_qualifier0 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_expression_comma_list_in_select_expressions1379 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_expression_co_in_expression_comma_list1392 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_expression_comma_list1395 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_expression_co_in_expression_comma_list1397 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_expression_in_expression_co1426 = new BitSet(new long[]{0x0000000000000402L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_correlation_in_expression_co1429 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ID_in_attribute_name1446 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000020L});
    public static final BitSet FOLLOW_DOT_in_attribute_name1449 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_set_in_attribute_name1452 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_COLUMN_in_attribute_name1465 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_attribute_name_in_attribute_comma_list1477 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_attribute_comma_list1480 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_attribute_name_in_attribute_comma_list1482 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_STARTBRACKET_in_attribute_comma_list_part1510 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_attribute_comma_list_in_attribute_comma_list_part1512 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_attribute_comma_list_part1514 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ID_in_left_expression1546 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000020L});
    public static final BitSet FOLLOW_DOT_in_left_expression1549 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_ID_in_left_expression1551 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_expression_in_right_expression1565 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_STARTBRACKET_in_set1576 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000100000220C40L});
    public static final BitSet FOLLOW_value_comma_list_in_set1578 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_set1580 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_variable_in_variable_comma_list1590 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_variable_comma_list1593 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000100000222C40L});
    public static final BitSet FOLLOW_variable_in_variable_comma_list1595 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_ID_in_variable1608 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_value_in_variable1612 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_set_in_class_name0 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_table_specification_in_table_specification_comma_list1637 = new BitSet(new long[]{0x0000000000000002L,0x0000000010000002L,0x0000000000000002L});
    public static final BitSet FOLLOW_comma_join_spec_in_table_specification_comma_list1640 = new BitSet(new long[]{0x0000000000000002L,0x0000000010000002L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_comma_join_spec1671 = new BitSet(new long[]{0x0000000004000040L,0x0000010000008000L,0x000000000000E000L});
    public static final BitSet FOLLOW_table_specification_in_comma_join_spec1673 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_qualified_join_specification_in_comma_join_spec1687 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_set_in_qualified_join_specification1698 = new BitSet(new long[]{0x2000000000000000L,0x0000000000080000L});
    public static final BitSet FOLLOW_OUTER_in_qualified_join_specification1706 = new BitSet(new long[]{0x2000000000000000L});
    public static final BitSet FOLLOW_JOIN_in_qualified_join_specification1709 = new BitSet(new long[]{0x0000000004000040L,0x0000010000008000L,0x000000000000E000L});
    public static final BitSet FOLLOW_table_specification_in_qualified_join_specification1711 = new BitSet(new long[]{0x0000000000000000L,0x0000000000004000L});
    public static final BitSet FOLLOW_join_condition_in_qualified_join_specification1713 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ON_in_join_condition1723 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_search_condition_in_join_condition1725 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_class_specification_in_table_specification1736 = new BitSet(new long[]{0x0000000000000402L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_correlation_in_table_specification1739 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_metaclass_specification_in_table_specification1747 = new BitSet(new long[]{0x0000000000000402L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_correlation_in_table_specification1750 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_subquery_in_table_specification1758 = new BitSet(new long[]{0x0000000000000402L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_correlation_in_table_specification1761 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_TABLE_in_table_specification1770 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_expression_in_table_specification1772 = new BitSet(new long[]{0x0000000000000402L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_correlation_in_table_specification1775 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_AS_in_correlation1788 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_ID_in_correlation1791 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000008000L});
    public static final BitSet FOLLOW_STARTBRACKET_in_correlation1794 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_id_comma_list_in_correlation1796 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_correlation1798 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ID_in_id_comma_list1811 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_id_comma_list1814 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_ID_in_id_comma_list1816 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_class_hierarchy_in_class_specification1831 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_STARTBRACKET_in_class_specification1836 = new BitSet(new long[]{0x0000000000000040L,0x0000000000008000L,0x0000000000006000L});
    public static final BitSet FOLLOW_class_hierarchy_comma_list_in_class_specification1838 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_class_specification1840 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ONLY_in_class_hierarchy1851 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_class_name_in_class_hierarchy1854 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ALL_in_class_hierarchy1858 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_class_name_in_class_hierarchy1860 = new BitSet(new long[]{0x0000040000000002L});
    public static final BitSet FOLLOW_EXCEPT_in_class_hierarchy1863 = new BitSet(new long[]{0x0000000000000040L,0x0000000000008000L,0x000000000000E000L});
    public static final BitSet FOLLOW_class_specification_in_class_hierarchy1865 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_class_hierarchy_in_class_hierarchy_comma_list1879 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_class_hierarchy_comma_list1882 = new BitSet(new long[]{0x0000000000000040L,0x0000000000008000L,0x0000000000006000L});
    public static final BitSet FOLLOW_class_hierarchy_in_class_hierarchy_comma_list1884 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_CLASS_in_metaclass_specification1897 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_class_name_in_metaclass_specification1899 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_query_expression_in_query_statement1909 = new BitSet(new long[]{0x0000000000000002L,0x0000000000040000L});
    public static final BitSet FOLLOW_ORDER_in_query_statement1913 = new BitSet(new long[]{0x0000000000020000L});
    public static final BitSet FOLLOW_BY_in_query_statement1915 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000026000L});
    public static final BitSet FOLLOW_sort_specification_comma_list_in_query_statement1917 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_sort_specification_in_sort_specification_comma_list1931 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_sort_specification_comma_list1934 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000026000L});
    public static final BitSet FOLLOW_sort_specification_in_sort_specification_comma_list1936 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_path_expression_in_sort_specification1950 = new BitSet(new long[]{0x0000000200000802L});
    public static final BitSet FOLLOW_set_in_sort_specification1952 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_unsigned_integer_literal_in_sort_specification1966 = new BitSet(new long[]{0x0000000200000802L});
    public static final BitSet FOLLOW_set_in_sort_specification1969 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_attribute_name_in_path_expression1987 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_query_term_in_query_expression1999 = new BitSet(new long[]{0x0400001000000002L,0x0001000000000000L});
    public static final BitSet FOLLOW_table_connect_in_query_expression2002 = new BitSet(new long[]{0x0000000000000000L,0x0000000040000000L});
    public static final BitSet FOLLOW_query_term_in_query_expression2004 = new BitSet(new long[]{0x0400001000000002L,0x0001000000000000L});
    public static final BitSet FOLLOW_table_operator_in_table_connect2033 = new BitSet(new long[]{0x0000002000000042L,0x0002000000000000L});
    public static final BitSet FOLLOW_qualifier_in_table_connect2036 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_query_specification_in_query_term2048 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_STARTBRACKET_in_subquery2059 = new BitSet(new long[]{0x0000000000000000L,0x0000000040000000L});
    public static final BitSet FOLLOW_query_statement_in_subquery2061 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_subquery2063 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_path_expression_in_path_expression_comma_list2092 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_path_expression_comma_list2095 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_path_expression_in_path_expression_comma_list2097 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_set_in_table_operator0 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_DECIMALLITERAL_in_unsigned_integer_literal2144 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_condition_in_search_condition2155 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_expression_in_condition2176 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_STARTBRACKET_in_parExpression2186 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_expression_in_parExpression2188 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_parExpression2190 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_conditionalOrExpression_in_expression2203 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000FF0000000L});
    public static final BitSet FOLLOW_assignmentOperator_in_expression2206 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_expression_in_expression2208 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_set_in_assignmentOperator0 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_conditionalAndExpression_in_conditionalOrExpression2317 = new BitSet(new long[]{0x0000000000000002L,0x0000000000020000L});
    public static final BitSet FOLLOW_OR_in_conditionalOrExpression2321 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_conditionalAndExpression_in_conditionalOrExpression2323 = new BitSet(new long[]{0x0000000000000002L,0x0000000000020000L});
    public static final BitSet FOLLOW_inclusiveOrExpression_in_conditionalAndExpression2365 = new BitSet(new long[]{0x0000000000000202L});
    public static final BitSet FOLLOW_AND_in_conditionalAndExpression2369 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_inclusiveOrExpression_in_conditionalAndExpression2371 = new BitSet(new long[]{0x0000000000000202L});
    public static final BitSet FOLLOW_connectExpression_in_inclusiveOrExpression2419 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000001000000000L});
    public static final BitSet FOLLOW_164_in_inclusiveOrExpression2423 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_connectExpression_in_inclusiveOrExpression2425 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000001000000000L});
    public static final BitSet FOLLOW_andExpression_in_connectExpression2447 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000200L});
    public static final BitSet FOLLOW_CONNECT_in_connectExpression2451 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_andExpression_in_connectExpression2453 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000200L});
    public static final BitSet FOLLOW_equalityExpression_in_andExpression2475 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000002000000000L});
    public static final BitSet FOLLOW_165_in_andExpression2479 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_equalityExpression_in_andExpression2481 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000002000000000L});
    public static final BitSet FOLLOW_additiveExpression_in_equalityExpression2503 = new BitSet(new long[]{0x9008000000000002L,0x0000000000000100L,0x00000F8000000100L});
    public static final BitSet FOLLOW_relationalOp_in_equalityExpression2506 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_additiveExpression_in_equalityExpression2508 = new BitSet(new long[]{0x9008000000000002L,0x0000000000000100L,0x00000F8000000100L});
    public static final BitSet FOLLOW_STARTBRACE_in_outer_join2528 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000004000000000L});
    public static final BitSet FOLLOW_166_in_outer_join2530 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_outer_join2532 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_EQUAL_in_relationalOp2542 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_IS_in_relationalOp2547 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_LIKE_in_relationalOp2552 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_NOT_in_relationalOp2558 = new BitSet(new long[]{0x0008000000000000L});
    public static final BitSet FOLLOW_IN_in_relationalOp2561 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_167_in_relationalOp2567 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_168_in_relationalOp2573 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_169_in_relationalOp2585 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_170_in_relationalOp2596 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_171_in_relationalOp2607 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_multiplicativeExpression_in_additiveExpression2625 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000104000000000L});
    public static final BitSet FOLLOW_set_in_additiveExpression2629 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_multiplicativeExpression_in_additiveExpression2637 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000104000000000L});
    public static final BitSet FOLLOW_between_expression_in_multiplicativeExpression2659 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000600000000004L});
    public static final BitSet FOLLOW_set_in_multiplicativeExpression2663 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_between_expression_in_multiplicativeExpression2677 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000600000000004L});
    public static final BitSet FOLLOW_unaryExpression_in_between_expression2697 = new BitSet(new long[]{0x0000000000008002L,0x0000000000000100L});
    public static final BitSet FOLLOW_NOT_in_between_expression2702 = new BitSet(new long[]{0x0000000000008000L});
    public static final BitSet FOLLOW_BETWEEN_in_between_expression2706 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_unaryExpression_in_between_expression2708 = new BitSet(new long[]{0x0000000000000200L});
    public static final BitSet FOLLOW_AND_in_between_expression2710 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_unaryExpression_in_between_expression2712 = new BitSet(new long[]{0x0000000000008002L,0x0000000000000100L});
    public static final BitSet FOLLOW_NOT_in_unaryExpression2729 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000200L,0x000010000022EC44L});
    public static final BitSet FOLLOW_EXISTS_in_unaryExpression2732 = new BitSet(new long[]{0x0000000000080000L,0x0000000000000200L,0x000010000022EC44L});
    public static final BitSet FOLLOW_primary_in_unaryExpression2735 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_parExpression_in_primary2745 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_attribute_name_in_primary2749 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000008L});
    public static final BitSet FOLLOW_outer_join_in_primary2752 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_NULL_in_primary2759 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_value_in_primary2764 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_STAR_in_primary2769 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_set_in_primary2774 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_function_in_primary2779 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_subquery_in_primary2784 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_CASE_in_primary2797 = new BitSet(new long[]{0x0000000000000000L,0x0100000000000000L});
    public static final BitSet FOLLOW_when_expression_in_primary2800 = new BitSet(new long[]{0x0000030000000000L,0x0100000000000000L});
    public static final BitSet FOLLOW_else_expression_in_primary2805 = new BitSet(new long[]{0x0000020000000000L});
    public static final BitSet FOLLOW_END_STRING_in_primary2809 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_WHEN_in_when_expression2849 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_expression_in_when_expression2851 = new BitSet(new long[]{0x0000000000000000L,0x0000080000000000L});
    public static final BitSet FOLLOW_THEN_in_when_expression2853 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_expression_in_when_expression2855 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ELSE_in_else_expression2879 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_expression_in_else_expression2881 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_index_in_index_comma_list2901 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_index_comma_list2904 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_index_in_index_comma_list2906 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_ID_in_index2919 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000020L});
    public static final BitSet FOLLOW_DOT_in_index2922 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_ID_in_index2924 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_INSERT_in_insert2941 = new BitSet(new long[]{0x0800000000000000L});
    public static final BitSet FOLLOW_INTO_in_insert2943 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_class_name_in_insert2945 = new BitSet(new long[]{0x0000000400000000L,0x0000800000000000L,0x0000000000008000L});
    public static final BitSet FOLLOW_insert_spec_in_insert2947 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_attributes_in_insert_spec2969 = new BitSet(new long[]{0x0000000000000000L,0x0000800000000000L});
    public static final BitSet FOLLOW_value_clause_in_insert_spec2973 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_attributes_in_insert_spec2989 = new BitSet(new long[]{0x0000000400000000L});
    public static final BitSet FOLLOW_DEFAULT_in_insert_spec2993 = new BitSet(new long[]{0x0000000000000000L,0x0000800000000000L});
    public static final BitSet FOLLOW_VALUES_in_insert_spec2995 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_STARTBRACKET_in_attributes3016 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_attribute_in_attributes3018 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010002L});
    public static final BitSet FOLLOW_COMMA_in_attributes3021 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_attribute_in_attributes3023 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010002L});
    public static final BitSet FOLLOW_ENDBRACKET_in_attributes3027 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ID_in_attribute3066 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_VALUES_in_value_clause3077 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000008000L});
    public static final BitSet FOLLOW_STARTBRACKET_in_value_clause3080 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_insert_item_comma_list_in_value_clause3082 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_value_clause3084 = new BitSet(new long[]{0x0000000000000002L,0x0000400000000000L});
    public static final BitSet FOLLOW_TO_in_value_clause3088 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000100000222C40L});
    public static final BitSet FOLLOW_variable_in_value_clause3090 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_insert_item_in_insert_item_comma_list3134 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_insert_item_comma_list3137 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_insert_item_in_insert_item_comma_list3139 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_expression_in_insert_item3168 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_UPDATE_in_update3186 = new BitSet(new long[]{0x0000000004000040L,0x0000000000008000L,0x000000000000E000L});
    public static final BitSet FOLLOW_class_all_spec_in_update3190 = new BitSet(new long[]{0x0000000000000000L,0x0000000200000000L});
    public static final BitSet FOLLOW_SET_in_update3193 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_assignment_comma_list_in_update3195 = new BitSet(new long[]{0x0000000000000002L,0x0200000000000000L});
    public static final BitSet FOLLOW_where_clause_in_update3198 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_class_specification_in_class_all_spec3237 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_metaclass_specification_in_class_all_spec3243 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_assignment_in_assignment_comma_list3254 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_assignment_comma_list3257 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_assignment_in_assignment_comma_list3259 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_attribute_name_in_assignment3289 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000100L});
    public static final BitSet FOLLOW_EQUAL_in_assignment3291 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_expression_in_assignment3293 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_DELETE_in_delete3307 = new BitSet(new long[]{0x0000800000000000L});
    public static final BitSet FOLLOW_FROM_in_delete3309 = new BitSet(new long[]{0x0000000000000040L,0x0000000000008000L,0x000000000000E000L});
    public static final BitSet FOLLOW_class_specification_in_delete3311 = new BitSet(new long[]{0x0000000000000002L,0x0200000000000000L});
    public static final BitSet FOLLOW_where_clause_in_delete3314 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_CREATE_in_create3348 = new BitSet(new long[]{0x0000000004000000L,0x0000010000000000L});
    public static final BitSet FOLLOW_class_or_table_in_create3350 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_class_name_in_create3352 = new BitSet(new long[]{0x0020100004000402L,0x0000000000000004L,0x0000000000008000L});
    public static final BitSet FOLLOW_subclass_definition_in_create3355 = new BitSet(new long[]{0x0020100004000002L,0x0000000000000004L,0x0000000000008000L});
    public static final BitSet FOLLOW_class_element_definition_part_in_create3359 = new BitSet(new long[]{0x0020100004000002L,0x0000000000000004L});
    public static final BitSet FOLLOW_CLASS_in_create3364 = new BitSet(new long[]{0x0000000000001000L});
    public static final BitSet FOLLOW_ATTRIBUTE_in_create3366 = new BitSet(new long[]{0x0000000004000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_attribute_definition_comma_list_in_create3369 = new BitSet(new long[]{0x0020100000000002L,0x0000000000000004L});
    public static final BitSet FOLLOW_METHOD_in_create3375 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_method_definition_comma_list_in_create3377 = new BitSet(new long[]{0x0020100000000002L});
    public static final BitSet FOLLOW_FILE_in_create3383 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000001000L});
    public static final BitSet FOLLOW_method_file_comma_list_in_create3385 = new BitSet(new long[]{0x0020000000000002L});
    public static final BitSet FOLLOW_INHERIT_in_create3391 = new BitSet(new long[]{0x0000000004000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_resolution_comma_list_in_create3393 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_CREATE_in_create_virtual_class3406 = new BitSet(new long[]{0x0000000000000000L,0x00C0000000000000L});
    public static final BitSet FOLLOW_vclass_or_view_in_create_virtual_class3408 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_class_name_in_create_virtual_class3410 = new BitSet(new long[]{0x0020100004000402L,0x0400000000000004L,0x0000000000008000L});
    public static final BitSet FOLLOW_subclass_definition_in_create_virtual_class3414 = new BitSet(new long[]{0x0020100004000402L,0x0400000000000004L,0x0000000000008000L});
    public static final BitSet FOLLOW_view_attribute_definition_part_in_create_virtual_class3418 = new BitSet(new long[]{0x0020100004000402L,0x0400000000000004L});
    public static final BitSet FOLLOW_CLASS_in_create_virtual_class3423 = new BitSet(new long[]{0x0000000000001000L});
    public static final BitSet FOLLOW_ATTRIBUTE_in_create_virtual_class3425 = new BitSet(new long[]{0x0000000004000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_attribute_definition_comma_list_in_create_virtual_class3428 = new BitSet(new long[]{0x0020100000000402L,0x0400000000000004L});
    public static final BitSet FOLLOW_METHOD_in_create_virtual_class3434 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_method_definition_comma_list_in_create_virtual_class3436 = new BitSet(new long[]{0x0020100000000402L,0x0400000000000000L});
    public static final BitSet FOLLOW_FILE_in_create_virtual_class3442 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000001000L});
    public static final BitSet FOLLOW_method_file_comma_list_in_create_virtual_class3444 = new BitSet(new long[]{0x0020000000000402L,0x0400000000000000L});
    public static final BitSet FOLLOW_INHERIT_in_create_virtual_class3450 = new BitSet(new long[]{0x0000000004000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_resolution_comma_list_in_create_virtual_class3452 = new BitSet(new long[]{0x0000000000000402L,0x0400000000000000L});
    public static final BitSet FOLLOW_AS_in_create_virtual_class3458 = new BitSet(new long[]{0x0000000000000000L,0x0000000040000000L});
    public static final BitSet FOLLOW_query_statement_in_create_virtual_class3460 = new BitSet(new long[]{0x0000000000000002L,0x0400000000000000L});
    public static final BitSet FOLLOW_WITH_in_create_virtual_class3466 = new BitSet(new long[]{0x0000000002000000L});
    public static final BitSet FOLLOW_CHECK_in_create_virtual_class3468 = new BitSet(new long[]{0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_OPTION_in_create_virtual_class3470 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_set_in_class_or_table0 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_set_in_vclass_or_view0 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_AS_in_subclass_definition3514 = new BitSet(new long[]{0x0000000000000000L,0x0000004000000000L});
    public static final BitSet FOLLOW_SUBCLASS_in_subclass_definition3516 = new BitSet(new long[]{0x0000000000000000L,0x0000000000001000L});
    public static final BitSet FOLLOW_OF_in_subclass_definition3518 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_class_name_comma_list_in_subclass_definition3520 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_class_name_in_class_name_comma_list3530 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_class_name_comma_list3533 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_class_name_in_class_name_comma_list3535 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_STARTBRACKET_in_class_element_definition_part3569 = new BitSet(new long[]{0x0000400014000000L,0x0002000000400000L,0x0000000000016002L});
    public static final BitSet FOLLOW_class_element_comma_list_in_class_element_definition_part3573 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_class_element_definition_part3577 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_class_element_in_class_element_comma_list3604 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_class_element_comma_list3607 = new BitSet(new long[]{0x0000400014000002L,0x0002000000400000L,0x0000000000006002L});
    public static final BitSet FOLLOW_class_element_in_class_element_comma_list3609 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_attribute_definition_in_class_element3645 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_class_constraint_in_class_element3651 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_CONSTRAINT_in_class_constraint3664 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_constraint_name_in_class_constraint3666 = new BitSet(new long[]{0x0000000000000000L,0x0002000000000000L});
    public static final BitSet FOLLOW_UNIQUE_in_class_constraint3671 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000008000L});
    public static final BitSet FOLLOW_attribute_comma_list_part_in_class_constraint3673 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_PRIMARY_in_class_constraint3678 = new BitSet(new long[]{0x4000000000000000L});
    public static final BitSet FOLLOW_KEY_in_class_constraint3680 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000008000L});
    public static final BitSet FOLLOW_attribute_comma_list_part_in_class_constraint3682 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_referential_constraint_in_class_constraint3689 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ID_in_constraint_name3701 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_FOREIGN_in_referential_constraint3711 = new BitSet(new long[]{0x4000000000000000L});
    public static final BitSet FOLLOW_KEY_in_referential_constraint3713 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x000000000000A000L});
    public static final BitSet FOLLOW_constraint_name_in_referential_constraint3716 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000008000L});
    public static final BitSet FOLLOW_attribute_comma_list_part_in_referential_constraint3720 = new BitSet(new long[]{0x0000000000000000L,0x0000000002000000L});
    public static final BitSet FOLLOW_REFERENCES_in_referential_constraint3723 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x000000000000A000L});
    public static final BitSet FOLLOW_referenced_table_name_in_referential_constraint3726 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000008000L});
    public static final BitSet FOLLOW_attribute_comma_list_part_in_referential_constraint3730 = new BitSet(new long[]{0x0000000000000002L,0x0000000000004000L});
    public static final BitSet FOLLOW_referential_triggered_action_in_referential_constraint3733 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ID_in_referenced_table_name3744 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_update_rule_in_referential_triggered_action3755 = new BitSet(new long[]{0x0000000000000002L,0x0000000000004000L});
    public static final BitSet FOLLOW_delete_rule_in_referential_triggered_action3759 = new BitSet(new long[]{0x0000000000000002L,0x0000000000004000L});
    public static final BitSet FOLLOW_cache_object_rule_in_referential_triggered_action3761 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ON_in_update_rule3775 = new BitSet(new long[]{0x0000000000000000L,0x0004000000000000L});
    public static final BitSet FOLLOW_UPDATE_in_update_rule3777 = new BitSet(new long[]{0x0000000000200000L,0x0000000008000080L});
    public static final BitSet FOLLOW_referential_action_in_update_rule3779 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ON_in_delete_rule3789 = new BitSet(new long[]{0x0000000800000000L});
    public static final BitSet FOLLOW_DELETE_in_delete_rule3791 = new BitSet(new long[]{0x0000000000200000L,0x0000000008000080L});
    public static final BitSet FOLLOW_referential_action_in_delete_rule3793 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_CASCADE_in_referential_action3804 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_RESTRICT_in_referential_action3810 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_NO_in_referential_action3816 = new BitSet(new long[]{0x0000000000000010L});
    public static final BitSet FOLLOW_ACTION_in_referential_action3818 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ON_in_cache_object_rule3828 = new BitSet(new long[]{0x0000000000100000L});
    public static final BitSet FOLLOW_CACHE_in_cache_object_rule3830 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000800L});
    public static final BitSet FOLLOW_OBJECT_in_cache_object_rule3832 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_cache_object_column_name_in_cache_object_rule3834 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_attribute_name_in_cache_object_column_name3845 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_STARTBRACKET_in_view_attribute_definition_part3857 = new BitSet(new long[]{0x0000000004000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_view_attribute_def_comma_list_in_view_attribute_definition_part3861 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_view_attribute_definition_part3865 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_view_attribute_definition_in_view_attribute_def_comma_list3876 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_view_attribute_def_comma_list3879 = new BitSet(new long[]{0x0000000004000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_view_attribute_definition_in_view_attribute_def_comma_list3881 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_attribute_definition_in_view_attribute_definition3917 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_attribute_name_in_view_attribute_definition3922 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_general_attribute_name_in_attribute_definition3933 = new BitSet(new long[]{0x03002040C0810000L,0x0010062A01000C59L,0x0000000000006000L});
    public static final BitSet FOLLOW_attribute_type_in_attribute_definition3935 = new BitSet(new long[]{0x0000000400004002L,0x0002000400400100L});
    public static final BitSet FOLLOW_default_or_shared_in_attribute_definition3938 = new BitSet(new long[]{0x0000000000004002L,0x0002000000400100L});
    public static final BitSet FOLLOW_auto_increment_in_attribute_definition3942 = new BitSet(new long[]{0x0000000000000002L,0x0002000000400100L});
    public static final BitSet FOLLOW_attribute_constraint_list_in_attribute_definition3946 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_function_in_attribute_definition3953 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_AUTO_INCREMENT_in_auto_increment3963 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000008000L});
    public static final BitSet FOLLOW_STARTBRACKET_in_auto_increment3966 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000020000L});
    public static final BitSet FOLLOW_DECIMALLITERAL_in_auto_increment3968 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_auto_increment3970 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000020000L});
    public static final BitSet FOLLOW_DECIMALLITERAL_in_auto_increment3972 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_auto_increment3974 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_CLASS_in_general_attribute_name3986 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_attribute_name_in_general_attribute_name3989 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_domain_in_attribute_type4000 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_privative_type_in_domain4012 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_collections_in_domain4017 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_domain_in_domain_comma_list4026 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_domain_comma_list4029 = new BitSet(new long[]{0x03002040C0810000L,0x0010062A01000C59L,0x0000000000006000L});
    public static final BitSet FOLLOW_domain_in_domain_comma_list4031 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_SET_in_collections4044 = new BitSet(new long[]{0x03002040C0810000L,0x0010062A01000C59L,0x0000000000006000L});
    public static final BitSet FOLLOW_domain_in_collections4046 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_SET_in_collections4051 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000008000L});
    public static final BitSet FOLLOW_STARTBRACKET_in_collections4053 = new BitSet(new long[]{0x03002040C0810000L,0x0010062A01000C59L,0x0000000000006000L});
    public static final BitSet FOLLOW_domain_comma_list_in_collections4055 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_collections4057 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_LIST_in_collections4062 = new BitSet(new long[]{0x0000000000000000L,0x0000000000020000L});
    public static final BitSet FOLLOW_OR_in_collections4064 = new BitSet(new long[]{0x0000000000000000L,0x0000000080000000L});
    public static final BitSet FOLLOW_SEQUENCE_in_collections4066 = new BitSet(new long[]{0x03002040C0810000L,0x0010062A01000C59L,0x0000000000006000L});
    public static final BitSet FOLLOW_domain_in_collections4068 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_LIST_in_collections4073 = new BitSet(new long[]{0x0000000000000000L,0x0000000000020000L});
    public static final BitSet FOLLOW_OR_in_collections4075 = new BitSet(new long[]{0x0000000000000000L,0x0000000080000000L});
    public static final BitSet FOLLOW_SEQUENCE_in_collections4077 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000008000L});
    public static final BitSet FOLLOW_STARTBRACKET_in_collections4079 = new BitSet(new long[]{0x03002040C0810000L,0x0010062A01000C59L,0x0000000000006000L});
    public static final BitSet FOLLOW_domain_comma_list_in_collections4081 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_collections4083 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_MULTISET_in_collections4088 = new BitSet(new long[]{0x03002040C0810000L,0x0010062A01000C59L,0x0000000000006000L});
    public static final BitSet FOLLOW_domain_in_collections4090 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_MULTISET_in_collections4095 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000008000L});
    public static final BitSet FOLLOW_STARTBRACKET_in_collections4097 = new BitSet(new long[]{0x03002040C0810000L,0x0010062A01000C59L,0x0000000000006000L});
    public static final BitSet FOLLOW_domain_comma_list_in_collections4099 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_collections4101 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_char__in_privative_type4111 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_varchar_in_privative_type4116 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_nchar_in_privative_type4121 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ncharvarying_in_privative_type4126 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_bit_in_privative_type4131 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_bitvarying_in_privative_type4136 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_numeric_in_privative_type4141 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_integer__in_privative_type4147 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_smallint_in_privative_type4152 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_monetary_in_privative_type4157 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_float__in_privative_type4162 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_doubleprecision_in_privative_type4167 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_date__in_privative_type4172 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_time__in_privative_type4177 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_timestamp_in_privative_type4182 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_string_in_privative_type4187 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_class_name_in_privative_type4192 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_OBJECT_in_privative_type4197 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_STRING_STR_in_string4207 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_CHAR_in_char_4219 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000040000L});
    public static final BitSet FOLLOW_LENGTH_in_char_4221 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_NCHAR_in_nchar4233 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000040000L});
    public static final BitSet FOLLOW_LENGTH_in_nchar4235 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_VARCHAR_in_varchar4247 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000040000L});
    public static final BitSet FOLLOW_LENGTH_in_varchar4249 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_NCHAR_in_ncharvarying4261 = new BitSet(new long[]{0x0000000000000000L,0x0020000000000000L});
    public static final BitSet FOLLOW_VARYING_in_ncharvarying4263 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000040000L});
    public static final BitSet FOLLOW_LENGTH_in_ncharvarying4265 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_BIT_in_bit4277 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000040000L});
    public static final BitSet FOLLOW_LENGTH_in_bit4279 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_BIT_in_bitvarying4290 = new BitSet(new long[]{0x0000000000000000L,0x0020000000000000L});
    public static final BitSet FOLLOW_VARYING_in_bitvarying4292 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000040000L});
    public static final BitSet FOLLOW_LENGTH_in_bitvarying4294 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_set_in_numeric4306 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000008000L});
    public static final BitSet FOLLOW_STARTBRACKET_in_numeric4319 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000020000L});
    public static final BitSet FOLLOW_DECIMALLITERAL_in_numeric4321 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010002L});
    public static final BitSet FOLLOW_COMMA_in_numeric4324 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000020000L});
    public static final BitSet FOLLOW_DECIMALLITERAL_in_numeric4326 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_numeric4331 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_set_in_integer_0 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_SMALLINT_in_smallint4362 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_MONETARY_in_monetary4373 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_set_in_float_4384 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000008000L});
    public static final BitSet FOLLOW_STARTBRACKET_in_float_4394 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000020000L});
    public static final BitSet FOLLOW_DECIMALLITERAL_in_float_4396 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_float_4398 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_DOUBLE_in_doubleprecision4411 = new BitSet(new long[]{0x0000000000000000L,0x0000000000200000L});
    public static final BitSet FOLLOW_PRECISION_in_doubleprecision4413 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_DATE_in_date_4424 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000040L});
    public static final BitSet FOLLOW_QUOTA_in_date_4429 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000080000L});
    public static final BitSet FOLLOW_DATE_FORMAT_in_date_4431 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000040L});
    public static final BitSet FOLLOW_QUOTA_in_date_4433 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_TIME_in_time_4564 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000040L});
    public static final BitSet FOLLOW_QUOTA_in_time_4569 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000100000L});
    public static final BitSet FOLLOW_TIME_FORMAT_in_time_4571 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000040L});
    public static final BitSet FOLLOW_QUOTA_in_time_4573 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_TIMESTAMP_in_timestamp4586 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000040L});
    public static final BitSet FOLLOW_QUOTA_in_timestamp4590 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000180000L});
    public static final BitSet FOLLOW_DATE_FORMAT_in_timestamp4597 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000100000L});
    public static final BitSet FOLLOW_TIME_FORMAT_in_timestamp4599 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000040L});
    public static final BitSet FOLLOW_TIME_FORMAT_in_timestamp4605 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000080000L});
    public static final BitSet FOLLOW_DATE_FORMAT_in_timestamp4607 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000040L});
    public static final BitSet FOLLOW_QUOTA_in_timestamp4613 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_SHARE_in_default_or_shared4648 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000100000220C40L});
    public static final BitSet FOLLOW_value_specification_in_default_or_shared4650 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_DEFAULT_in_default_or_shared4655 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000100000220C40L});
    public static final BitSet FOLLOW_value_specification_in_default_or_shared4657 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_attribute_constraint_in_attribute_constraint_list4668 = new BitSet(new long[]{0x0000000000000002L,0x0002000000400100L});
    public static final BitSet FOLLOW_NOT_in_attribute_constraint4680 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000200L});
    public static final BitSet FOLLOW_NULL_in_attribute_constraint4682 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_UNIQUE_in_attribute_constraint4687 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_PRIMARY_in_attribute_constraint4692 = new BitSet(new long[]{0x4000000000000000L});
    public static final BitSet FOLLOW_KEY_in_attribute_constraint4694 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_value_in_value_specification4706 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_attribute_definition_in_attribute_definition_comma_list4717 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_attribute_definition_comma_list4720 = new BitSet(new long[]{0x0000000004000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_attribute_definition_in_attribute_definition_comma_list4722 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_method_definition_in_method_definition_comma_list4759 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_method_definition_comma_list4762 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_method_definition_in_method_definition_comma_list4764 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_general_method_name_in_method_definition4801 = new BitSet(new long[]{0x03012040C0810002L,0x0010062A01000C59L,0x000000000000E000L});
    public static final BitSet FOLLOW_argument_type_part_in_method_definition4804 = new BitSet(new long[]{0x03012040C0810002L,0x0010062A01000C59L,0x0000000000006000L});
    public static final BitSet FOLLOW_result_type_in_method_definition4808 = new BitSet(new long[]{0x0001000000000002L});
    public static final BitSet FOLLOW_FUNCTION_in_method_definition4813 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_function_name_in_method_definition4815 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ID_in_general_method_name4828 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_STARTBRACKET_in_argument_type_part4839 = new BitSet(new long[]{0x03002040C0810000L,0x0010062A01000C59L,0x0000000000016000L});
    public static final BitSet FOLLOW_argument_type_comma_list_in_argument_type_part4843 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_argument_type_part4848 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_argument_type_in_argument_type_comma_list4858 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_argument_type_comma_list4861 = new BitSet(new long[]{0x03002040C0810000L,0x0010062A01000C59L,0x0000000000006000L});
    public static final BitSet FOLLOW_argument_type_in_argument_type_comma_list4863 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_domain_in_argument_type4899 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_domain_in_result_type4909 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ID_in_function_name4920 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_PATH_in_method_file_comma_list4932 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_method_file_comma_list4935 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000001000L});
    public static final BitSet FOLLOW_PATH_in_method_file_comma_list4937 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_resolution_in_resolution_comma_list4973 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_resolution_comma_list4976 = new BitSet(new long[]{0x0000000004000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_resolution_in_resolution_comma_list4978 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_general_attribute_name_in_resolution5014 = new BitSet(new long[]{0x0000000000000000L,0x0000000000001000L});
    public static final BitSet FOLLOW_OF_in_resolution5016 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_class_name_in_resolution5018 = new BitSet(new long[]{0x0000000000000402L});
    public static final BitSet FOLLOW_AS_in_resolution5022 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_attribute_name_in_resolution5024 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_drop_class_in_drop5039 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_drop_index_in_drop5044 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_drop_trigger_in_drop5049 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_drop_deferred_in_drop5054 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_DROP_in_drop_class5064 = new BitSet(new long[]{0x0000000004000040L,0x00C0010000008000L,0x000000000000E000L});
    public static final BitSet FOLLOW_class_type_in_drop_class5066 = new BitSet(new long[]{0x0000000000000040L,0x0000000000008000L,0x000000000000E000L});
    public static final BitSet FOLLOW_class_specification_comma_list_in_drop_class5069 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_set_in_class_type0 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_class_specification_in_class_specification_comma_list5105 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_class_specification_comma_list5108 = new BitSet(new long[]{0x0000000000000040L,0x0000000000008000L,0x000000000000E000L});
    public static final BitSet FOLLOW_class_specification_in_class_specification_comma_list5110 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_DROP_in_drop_index5125 = new BitSet(new long[]{0x0010000000000000L,0x0002001000000000L});
    public static final BitSet FOLLOW_REVERSE_in_drop_index5127 = new BitSet(new long[]{0x0010000000000000L,0x0002000000000000L});
    public static final BitSet FOLLOW_UNIQUE_in_drop_index5130 = new BitSet(new long[]{0x0010000000000000L});
    public static final BitSet FOLLOW_INDEX_in_drop_index5133 = new BitSet(new long[]{0x0000000000000000L,0x0000000000004000L,0x0000000000002000L});
    public static final BitSet FOLLOW_index_name_in_drop_index5135 = new BitSet(new long[]{0x0000000000000000L,0x0000000000004000L});
    public static final BitSet FOLLOW_ON_in_drop_index5138 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_class_name_in_drop_index5140 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000008000L});
    public static final BitSet FOLLOW_attribute_comma_list_part_in_drop_index5142 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ID_in_index_name5177 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_DROP_in_drop_trigger5189 = new BitSet(new long[]{0x0000000000000000L,0x0000100000000000L});
    public static final BitSet FOLLOW_TRIGGER_in_drop_trigger5191 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_trigger_name_comma_list_in_drop_trigger5193 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_trigger_name_in_trigger_name_comma_list5204 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_trigger_name_comma_list5207 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_trigger_name_in_trigger_name_comma_list5209 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_ID_in_trigger_name5222 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_DROP_in_drop_deferred5235 = new BitSet(new long[]{0x0000000100000000L});
    public static final BitSet FOLLOW_DEFERRED_in_drop_deferred5237 = new BitSet(new long[]{0x0000000000000000L,0x0000100000000000L});
    public static final BitSet FOLLOW_TRIGGER_in_drop_deferred5239 = new BitSet(new long[]{0x0000000000000040L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_trigger_spec_in_drop_deferred5241 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_trigger_name_comma_list_in_trigger_spec5253 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ALL_in_trigger_spec5258 = new BitSet(new long[]{0x0000000000000000L,0x0000200000000000L});
    public static final BitSet FOLLOW_TRIGGERS_in_trigger_spec5260 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ALTER_in_alter5275 = new BitSet(new long[]{0x0000000004000000L,0x00C0010000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_class_type_in_alter5277 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_class_name_in_alter5280 = new BitSet(new long[]{0x0020008000400020L,0x0000000004000000L});
    public static final BitSet FOLLOW_alter_clause_in_alter5282 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ADD_in_alter_clause5307 = new BitSet(new long[]{0x0020500014001002L,0x0002008000C00004L,0x0000000000006002L});
    public static final BitSet FOLLOW_alter_add_in_alter_clause5309 = new BitSet(new long[]{0x0020000000000002L});
    public static final BitSet FOLLOW_INHERIT_in_alter_clause5312 = new BitSet(new long[]{0x0000000004000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_resolution_comma_list_in_alter_clause5314 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_DROP_in_alter_clause5321 = new BitSet(new long[]{0x0000100014001000L,0x0000008000800004L,0x0000000000006000L});
    public static final BitSet FOLLOW_alter_drop_in_alter_clause5323 = new BitSet(new long[]{0x0020000000000002L});
    public static final BitSet FOLLOW_INHERIT_in_alter_clause5326 = new BitSet(new long[]{0x0000000004000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_resolution_comma_list_in_alter_clause5328 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_RENAME_in_alter_clause5335 = new BitSet(new long[]{0x0001000004001000L,0x0000000000000004L,0x0000000000006000L});
    public static final BitSet FOLLOW_alter_rename_in_alter_clause5337 = new BitSet(new long[]{0x0020000000000002L});
    public static final BitSet FOLLOW_INHERIT_in_alter_clause5340 = new BitSet(new long[]{0x0000000004000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_resolution_comma_list_in_alter_clause5342 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_CHANGE_in_alter_clause5349 = new BitSet(new long[]{0x0000100004000000L,0x0000000000800004L,0x0000000000006000L});
    public static final BitSet FOLLOW_alter_change_in_alter_clause5351 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_INHERIT_in_alter_clause5356 = new BitSet(new long[]{0x0000000004000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_resolution_comma_list_in_alter_clause5358 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_set_in_alter_add5369 = new BitSet(new long[]{0x0000400014000002L,0x0002000000400000L,0x0000000000006002L});
    public static final BitSet FOLLOW_class_element_comma_list_in_alter_add5380 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_CLASS_in_alter_add5385 = new BitSet(new long[]{0x0000000000001000L});
    public static final BitSet FOLLOW_ATTRIBUTE_in_alter_add5387 = new BitSet(new long[]{0x0000400014000002L,0x0002000000400000L,0x0000000000006002L});
    public static final BitSet FOLLOW_class_element_comma_list_in_alter_add5389 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_FILE_in_alter_add5394 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000001000L});
    public static final BitSet FOLLOW_file_name_comma_list_in_alter_add5396 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_METHOD_in_alter_add5401 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_method_definition_comma_list_in_alter_add5403 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_QUERY_in_alter_add5408 = new BitSet(new long[]{0x0000000000000000L,0x0000000040000000L});
    public static final BitSet FOLLOW_select_statement_in_alter_add5410 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_SUPERCLASS_in_alter_add5415 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_class_name_comma_list_in_alter_add5417 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_file_path_name_in_file_name_comma_list5427 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_file_name_comma_list5430 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000001000L});
    public static final BitSet FOLLOW_file_path_name_in_file_name_comma_list5432 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_set_in_alter_drop5446 = new BitSet(new long[]{0x0000000004000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_general_attribute_name_comma_list_in_alter_drop5460 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_FILE_in_alter_drop5465 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000001000L});
    public static final BitSet FOLLOW_file_name_comma_list_in_alter_drop5467 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_QUERY_in_alter_drop5472 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000020000L});
    public static final BitSet FOLLOW_unsigned_integer_literal_in_alter_drop5474 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_SUPERCLASS_in_alter_drop5481 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_class_name_comma_list_in_alter_drop5483 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_CONSTRAINT_in_alter_drop5488 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_constraint_name_in_alter_drop5490 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_general_attribute_name_in_general_attribute_name_comma_list5501 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_general_attribute_name_comma_list5504 = new BitSet(new long[]{0x0000000004000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_general_attribute_name_in_general_attribute_name_comma_list5506 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_FILE_in_alter_change5519 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000001000L});
    public static final BitSet FOLLOW_file_path_name_in_alter_change5521 = new BitSet(new long[]{0x0000000000000400L});
    public static final BitSet FOLLOW_AS_in_alter_change5523 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000001000L});
    public static final BitSet FOLLOW_file_path_name_in_alter_change5525 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_METHOD_in_alter_change5531 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_method_definition_comma_list_in_alter_change5533 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_QUERY_in_alter_change5538 = new BitSet(new long[]{0x0000000000000000L,0x0000000040000000L,0x0000000000020000L});
    public static final BitSet FOLLOW_unsigned_integer_literal_in_alter_change5540 = new BitSet(new long[]{0x0000000000000000L,0x0000000040000000L});
    public static final BitSet FOLLOW_select_statement_in_alter_change5543 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_general_attribute_name_in_alter_change5548 = new BitSet(new long[]{0x0000000400000000L});
    public static final BitSet FOLLOW_DEFAULT_in_alter_change5550 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000100000220C40L});
    public static final BitSet FOLLOW_value_specifiation_in_alter_change5552 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_set_in_alter_rename5563 = new BitSet(new long[]{0x0000000004000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_general_attribute_name_in_alter_rename5577 = new BitSet(new long[]{0x0000000000000400L});
    public static final BitSet FOLLOW_AS_in_alter_rename5579 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_attribute_name_in_alter_rename5581 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_FUNCTION_in_alter_rename5586 = new BitSet(new long[]{0x0000000000000000L,0x0000000000001000L});
    public static final BitSet FOLLOW_OF_in_alter_rename5588 = new BitSet(new long[]{0x0000000004000000L,0x0000000000000000L,0x0000000000006000L});
    public static final BitSet FOLLOW_general_attribute_name_in_alter_rename5590 = new BitSet(new long[]{0x0000000000000400L});
    public static final BitSet FOLLOW_AS_in_alter_rename5592 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_function_name_in_alter_rename5594 = new BitSet(new long[]{0x0000100000000000L});
    public static final BitSet FOLLOW_FILE_in_alter_rename5597 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000001000L});
    public static final BitSet FOLLOW_file_path_name_in_alter_rename5599 = new BitSet(new long[]{0x0000000000000400L});
    public static final BitSet FOLLOW_AS_in_alter_rename5601 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000001000L});
    public static final BitSet FOLLOW_file_path_name_in_alter_rename5603 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_value_in_value_specifiation5616 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_PATH_in_file_path_name5627 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_CALL_in_call5640 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_method_name_in_call5642 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000008000L});
    public static final BitSet FOLLOW_STARTBRACKET_in_call5644 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000023EC44L});
    public static final BitSet FOLLOW_argument_comma_list_in_call5648 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_call5652 = new BitSet(new long[]{0x0000000000000000L,0x0000000000004000L});
    public static final BitSet FOLLOW_ON_in_call5655 = new BitSet(new long[]{0x0000000004000000L,0x0000000000000000L,0x0000000000002000L});
    public static final BitSet FOLLOW_call_target_in_call5657 = new BitSet(new long[]{0x0000000000000002L,0x0000400000000000L});
    public static final BitSet FOLLOW_to_variable_in_call5660 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ID_in_method_name5672 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_variable_name_in_call_target5683 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_metaclass_specification_in_call_target5687 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_ID_in_variable_name5698 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_TO_in_to_variable5709 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000100000222C40L});
    public static final BitSet FOLLOW_variable_in_to_variable5711 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_WHERE_in_where_clause5727 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_search_condition_in_where_clause5729 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_function_name_in_function5762 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000008000L});
    public static final BitSet FOLLOW_STARTBRACKET_in_function5766 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000023EC44L});
    public static final BitSet FOLLOW_argument_comma_list_in_function5770 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000010000L});
    public static final BitSet FOLLOW_ENDBRACKET_in_function5774 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_argument_in_argument_comma_list5783 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_argument_comma_list5786 = new BitSet(new long[]{0x0000080000080000L,0x0000000000000300L,0x000010000022EC44L});
    public static final BitSet FOLLOW_argument_in_argument_comma_list5788 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_expression_in_argument5801 = new BitSet(new long[]{0x0000000000000402L});
    public static final BitSet FOLLOW_AS_in_argument5804 = new BitSet(new long[]{0x03002040C0810000L,0x0010062801000C48L,0x0000000000006000L});
    public static final BitSet FOLLOW_privative_type_in_argument5806 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_set_in_operation0 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_STRING_in_value5842 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_QUOTA_in_value5847 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000100000020000L});
    public static final BitSet FOLLOW_number_in_value5849 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000000040L});
    public static final BitSet FOLLOW_QUOTA_in_value5851 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_number_in_value5859 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_currency_in_value5864 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_Q_MARK_in_value5869 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_value_in_value_comma_list5879 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_COMMA_in_value_comma_list5882 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000100000220C40L});
    public static final BitSet FOLLOW_value_in_value_comma_list5884 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000002L});
    public static final BitSet FOLLOW_DOLLAR_in_currency5914 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000100000020000L});
    public static final BitSet FOLLOW_number_in_currency5916 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_EQUAL_in_express5928 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_170_in_express5934 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_171_in_express5940 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_168_in_express5946 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_169_in_express5952 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_167_in_express5958 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_166_in_express5963 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_172_in_express5968 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_STAR_in_express5973 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_173_in_express5978 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_NOT_in_express5984 = new BitSet(new long[]{0x0000080000000000L});
    public static final BitSet FOLLOW_EXISTS_in_express5988 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_NOT_in_express5994 = new BitSet(new long[]{0x0008000000000000L});
    public static final BitSet FOLLOW_IN_in_express5998 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_CONNECT_in_express6003 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_LIKE_in_express6008 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_172_in_number6022 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000020000L});
    public static final BitSet FOLLOW_DECIMALLITERAL_in_number6025 = new BitSet(new long[]{0x0000000000000002L,0x0000000000000000L,0x0000000000000020L});
    public static final BitSet FOLLOW_DOT_in_number6029 = new BitSet(new long[]{0x0000000000000000L,0x0000000000000000L,0x0000000000020000L});
    public static final BitSet FOLLOW_DECIMALLITERAL_in_number6031 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_END_in_end6062 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_SELECT_in_select6078 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_FROM_in_from6094 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_WHERE_in_where6112 = new BitSet(new long[]{0x0000000000000002L});
    public static final BitSet FOLLOW_GROUP_in_group_by6130 = new BitSet(new long[]{0x0000000000020000L});
    public static final BitSet FOLLOW_BY_in_group_by6132 = new BitSet(new long[]{0x0000000000000002L});

}