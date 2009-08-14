// $ANTLR 3.0.1 Upper.g 2009-07-10 16:32:04
package com.cubrid.cubridmanager.ui.query.grammar;

import org.antlr.runtime.BaseRecognizer;
import org.antlr.runtime.CharStream;
import org.antlr.runtime.DFA;
import org.antlr.runtime.EarlyExitException;
import org.antlr.runtime.Lexer;
import org.antlr.runtime.MismatchedSetException;
import org.antlr.runtime.NoViableAltException;
import org.antlr.runtime.RecognitionException;

public class UpperLexer extends Lexer {
    public static final int DECIMALLITERAL=6;
    public static final int MARKS=7;
    public static final int T29=29;
    public static final int T28=28;
    public static final int T27=27;
    public static final int T26=26;
    public static final int T25=25;
    public static final int ID=4;
    public static final int Tokens=33;
    public static final int T24=24;
    public static final int EOF=-1;
    public static final int T23=23;
    public static final int T22=22;
    public static final int T21=21;
    public static final int T20=20;
    public static final int ML_COMMENT=8;
    public static final int QUOTA=11;
    public static final int JAPAN=14;
    public static final int CHINESE=13;
    public static final int FROM=10;
    public static final int T15=15;
    public static final int T16=16;
    public static final int SELECT=9;
    public static final int T17=17;
    public static final int T18=18;
    public static final int T30=30;
    public static final int T19=19;
    public static final int KOREA=12;
    public static final int T32=32;
    public static final int STRING=5;
    public static final int T31=31;
    public UpperLexer() {;} 
    public UpperLexer(CharStream input) {
        super(input);
    }
    public String getGrammarFileName() { return "Upper.g"; }

    // $ANTLR start T15
    public final void mT15() throws RecognitionException {
        try {
            int _type = T15;
            // Upper.g:8:5: ( '<' )
            // Upper.g:8:7: '<'
            {
            match('<'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T15

    // $ANTLR start T16
    public final void mT16() throws RecognitionException {
        try {
            int _type = T16;
            // Upper.g:9:5: ( '>' )
            // Upper.g:9:7: '>'
            {
            match('>'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T16

    // $ANTLR start T17
    public final void mT17() throws RecognitionException {
        try {
            int _type = T17;
            // Upper.g:10:5: ( '+' )
            // Upper.g:10:7: '+'
            {
            match('+'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T17

    // $ANTLR start T18
    public final void mT18() throws RecognitionException {
        try {
            int _type = T18;
            // Upper.g:11:5: ( '|' )
            // Upper.g:11:7: '|'
            {
            match('|'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T18

    // $ANTLR start T19
    public final void mT19() throws RecognitionException {
        try {
            int _type = T19;
            // Upper.g:12:5: ( '-' )
            // Upper.g:12:7: '-'
            {
            match('-'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T19

    // $ANTLR start T20
    public final void mT20() throws RecognitionException {
        try {
            int _type = T20;
            // Upper.g:13:5: ( '=' )
            // Upper.g:13:7: '='
            {
            match('='); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T20

    // $ANTLR start T21
    public final void mT21() throws RecognitionException {
        try {
            int _type = T21;
            // Upper.g:14:5: ( '*' )
            // Upper.g:14:7: '*'
            {
            match('*'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T21

    // $ANTLR start T22
    public final void mT22() throws RecognitionException {
        try {
            int _type = T22;
            // Upper.g:15:5: ( ';' )
            // Upper.g:15:7: ';'
            {
            match(';'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T22

    // $ANTLR start T23
    public final void mT23() throws RecognitionException {
        try {
            int _type = T23;
            // Upper.g:16:5: ( ':' )
            // Upper.g:16:7: ':'
            {
            match(':'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T23

    // $ANTLR start T24
    public final void mT24() throws RecognitionException {
        try {
            int _type = T24;
            // Upper.g:17:5: ( '.' )
            // Upper.g:17:7: '.'
            {
            match('.'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T24

    // $ANTLR start T25
    public final void mT25() throws RecognitionException {
        try {
            int _type = T25;
            // Upper.g:18:5: ( ',' )
            // Upper.g:18:7: ','
            {
            match(','); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T25

    // $ANTLR start T26
    public final void mT26() throws RecognitionException {
        try {
            int _type = T26;
            // Upper.g:19:5: ( '\\n' )
            // Upper.g:19:7: '\\n'
            {
            match('\n'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T26

    // $ANTLR start T27
    public final void mT27() throws RecognitionException {
        try {
            int _type = T27;
            // Upper.g:20:5: ( '\\r' )
            // Upper.g:20:7: '\\r'
            {
            match('\r'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T27

    // $ANTLR start T28
    public final void mT28() throws RecognitionException {
        try {
            int _type = T28;
            // Upper.g:21:5: ( '\\t' )
            // Upper.g:21:7: '\\t'
            {
            match('\t'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T28

    // $ANTLR start T29
    public final void mT29() throws RecognitionException {
        try {
            int _type = T29;
            // Upper.g:22:5: ( ' ' )
            // Upper.g:22:7: ' '
            {
            match(' '); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T29

    // $ANTLR start T30
    public final void mT30() throws RecognitionException {
        try {
            int _type = T30;
            // Upper.g:23:5: ( '$' )
            // Upper.g:23:7: '$'
            {
            match('$'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T30

    // $ANTLR start T31
    public final void mT31() throws RecognitionException {
        try {
            int _type = T31;
            // Upper.g:24:5: ( '?' )
            // Upper.g:24:7: '?'
            {
            match('?'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T31

    // $ANTLR start T32
    public final void mT32() throws RecognitionException {
        try {
            int _type = T32;
            // Upper.g:25:5: ( '/' )
            // Upper.g:25:7: '/'
            {
            match('/'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end T32

    // $ANTLR start SELECT
    public final void mSELECT() throws RecognitionException {
        try {
            int _type = SELECT;
            // Upper.g:62:8: ( 'select' )
            // Upper.g:62:10: 'select'
            {
            match("select"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end SELECT

    // $ANTLR start FROM
    public final void mFROM() throws RecognitionException {
        try {
            int _type = FROM;
            // Upper.g:63:6: ( 'from' )
            // Upper.g:63:8: 'from'
            {
            match("from"); 


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end FROM

    // $ANTLR start DECIMALLITERAL
    public final void mDECIMALLITERAL() throws RecognitionException {
        try {
            int _type = DECIMALLITERAL;
            // Upper.g:68:15: ( ( '0' .. '9' )+ )
            // Upper.g:69:2: ( '0' .. '9' )+
            {
            // Upper.g:69:2: ( '0' .. '9' )+
            int cnt1=0;
            loop1:
            do {
                int alt1=2;
                int LA1_0 = input.LA(1);

                if ( ((LA1_0>='0' && LA1_0<='9')) ) {
                    alt1=1;
                }


                switch (alt1) {
            	case 1 :
            	    // Upper.g:69:2: '0' .. '9'
            	    {
            	    matchRange('0','9'); 

            	    }
            	    break;

            	default :
            	    if ( cnt1 >= 1 ) break loop1;
                        EarlyExitException eee =
                            new EarlyExitException(1, input);
                        throw eee;
                }
                cnt1++;
            } while (true);


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end DECIMALLITERAL

    // $ANTLR start MARKS
    public final void mMARKS() throws RecognitionException {
        try {
            int _type = MARKS;
            // Upper.g:72:7: ( '(' | ')' | '[' | ']' | '{' | '}' )
            // Upper.g:
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

    // $ANTLR start STRING
    public final void mSTRING() throws RecognitionException {
        try {
            int _type = STRING;
            // Upper.g:74:8: ( QUOTA ( 'a' .. 'z' | 'A' .. 'Z' | '_' | '0' .. '9' | ' ' | '.' | ',' | '/' | '\\\\' | '-' | ':' | MARKS | KOREA | CHINESE | JAPAN )* QUOTA )
            // Upper.g:74:10: QUOTA ( 'a' .. 'z' | 'A' .. 'Z' | '_' | '0' .. '9' | ' ' | '.' | ',' | '/' | '\\\\' | '-' | ':' | MARKS | KOREA | CHINESE | JAPAN )* QUOTA
            {
            mQUOTA(); 
            // Upper.g:74:16: ( 'a' .. 'z' | 'A' .. 'Z' | '_' | '0' .. '9' | ' ' | '.' | ',' | '/' | '\\\\' | '-' | ':' | MARKS | KOREA | CHINESE | JAPAN )*
            loop2:
            do {
                int alt2=2;
                int LA2_0 = input.LA(1);

                if ( (LA2_0==' '||(LA2_0>='(' && LA2_0<=')')||(LA2_0>=',' && LA2_0<=':')||(LA2_0>='A' && LA2_0<=']')||LA2_0=='_'||(LA2_0>='a' && LA2_0<='{')||LA2_0=='}'||(LA2_0>='\u1100' && LA2_0<='\u11FF')||(LA2_0>='\u3040' && LA2_0<='\u31FF')||(LA2_0>='\u4E00' && LA2_0<='\u9FA5')||(LA2_0>='\uAC00' && LA2_0<='\uD7AF')) ) {
                    alt2=1;
                }


                switch (alt2) {
            	case 1 :
            	    // Upper.g:
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
            	    break loop2;
                }
            } while (true);

            mQUOTA(); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end STRING

    // $ANTLR start KOREA
    public final void mKOREA() throws RecognitionException {
        try {
            int _type = KOREA;
            // Upper.g:77:7: ( '\\uAC00' .. '\\uD7AF' | '\\u1100' .. '\\u11FF' | '\\u3130' .. '\\u318F' )
            // Upper.g:
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
            // Upper.g:79:9: ( '\\u4E00' .. '\\u9FA5' )
            // Upper.g:79:11: '\\u4E00' .. '\\u9FA5'
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
            // Upper.g:81:7: ( '\\u3040' .. '\\u31FF' )
            // Upper.g:81:10: '\\u3040' .. '\\u31FF'
            {
            matchRange('\u3040','\u31FF'); 

            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end JAPAN

    // $ANTLR start QUOTA
    public final void mQUOTA() throws RecognitionException {
        try {
            int _type = QUOTA;
            // Upper.g:83:6: ( '\\'' | '\"' )
            // Upper.g:
            {
            if ( input.LA(1)=='\"'||input.LA(1)=='\'' ) {
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
    // $ANTLR end QUOTA

    // $ANTLR start ID
    public final void mID() throws RecognitionException {
        try {
            int _type = ID;
            // Upper.g:85:3: ( ( 'a' .. 'z' | 'A' .. 'Z' | '_' | '0' .. '9' )+ )
            // Upper.g:85:5: ( 'a' .. 'z' | 'A' .. 'Z' | '_' | '0' .. '9' )+
            {
            // Upper.g:85:5: ( 'a' .. 'z' | 'A' .. 'Z' | '_' | '0' .. '9' )+
            int cnt3=0;
            loop3:
            do {
                int alt3=2;
                int LA3_0 = input.LA(1);

                if ( ((LA3_0>='0' && LA3_0<='9')||(LA3_0>='A' && LA3_0<='Z')||LA3_0=='_'||(LA3_0>='a' && LA3_0<='z')) ) {
                    alt3=1;
                }


                switch (alt3) {
            	case 1 :
            	    // Upper.g:
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
            	    if ( cnt3 >= 1 ) break loop3;
                        EarlyExitException eee =
                            new EarlyExitException(3, input);
                        throw eee;
                }
                cnt3++;
            } while (true);


            }

            this.type = _type;
        }
        finally {
        }
    }
    // $ANTLR end ID

    // $ANTLR start ML_COMMENT
    public final void mML_COMMENT() throws RecognitionException {
        try {
            int _type = ML_COMMENT;
            // Upper.g:87:11: ( '/*' ( options {greedy=false; } : . )* '*/' | '--' ( options {greedy=false; } : . )* '\\n' )
            int alt6=2;
            int LA6_0 = input.LA(1);

            if ( (LA6_0=='/') ) {
                alt6=1;
            }
            else if ( (LA6_0=='-') ) {
                alt6=2;
            }
            else {
                NoViableAltException nvae =
                    new NoViableAltException("87:1: ML_COMMENT : ( '/*' ( options {greedy=false; } : . )* '*/' | '--' ( options {greedy=false; } : . )* '\\n' );", 6, 0, input);

                throw nvae;
            }
            switch (alt6) {
                case 1 :
                    // Upper.g:88:2: '/*' ( options {greedy=false; } : . )* '*/'
                    {
                    match("/*"); 

                    // Upper.g:88:7: ( options {greedy=false; } : . )*
                    loop4:
                    do {
                        int alt4=2;
                        int LA4_0 = input.LA(1);

                        if ( (LA4_0=='*') ) {
                            int LA4_1 = input.LA(2);

                            if ( (LA4_1=='/') ) {
                                alt4=2;
                            }
                            else if ( ((LA4_1>='\u0000' && LA4_1<='.')||(LA4_1>='0' && LA4_1<='\uFFFE')) ) {
                                alt4=1;
                            }


                        }
                        else if ( ((LA4_0>='\u0000' && LA4_0<=')')||(LA4_0>='+' && LA4_0<='\uFFFE')) ) {
                            alt4=1;
                        }


                        switch (alt4) {
                    	case 1 :
                    	    // Upper.g:88:35: .
                    	    {
                    	    matchAny(); 

                    	    }
                    	    break;

                    	default :
                    	    break loop4;
                        }
                    } while (true);

                    match("*/"); 


                    }
                    break;
                case 2 :
                    // Upper.g:89:7: '--' ( options {greedy=false; } : . )* '\\n'
                    {
                    match("--"); 

                    // Upper.g:89:12: ( options {greedy=false; } : . )*
                    loop5:
                    do {
                        int alt5=2;
                        int LA5_0 = input.LA(1);

                        if ( (LA5_0=='\n') ) {
                            alt5=2;
                        }
                        else if ( ((LA5_0>='\u0000' && LA5_0<='\t')||(LA5_0>='\u000B' && LA5_0<='\uFFFE')) ) {
                            alt5=1;
                        }


                        switch (alt5) {
                    	case 1 :
                    	    // Upper.g:89:40: .
                    	    {
                    	    matchAny(); 

                    	    }
                    	    break;

                    	default :
                    	    break loop5;
                        }
                    } while (true);

                    match('\n'); 

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
        // Upper.g:1:8: ( T15 | T16 | T17 | T18 | T19 | T20 | T21 | T22 | T23 | T24 | T25 | T26 | T27 | T28 | T29 | T30 | T31 | T32 | SELECT | FROM | DECIMALLITERAL | MARKS | STRING | KOREA | CHINESE | JAPAN | QUOTA | ID | ML_COMMENT )
        int alt7=29;
        alt7 = dfa7.predict(input);
        switch (alt7) {
            case 1 :
                // Upper.g:1:10: T15
                {
                mT15(); 

                }
                break;
            case 2 :
                // Upper.g:1:14: T16
                {
                mT16(); 

                }
                break;
            case 3 :
                // Upper.g:1:18: T17
                {
                mT17(); 

                }
                break;
            case 4 :
                // Upper.g:1:22: T18
                {
                mT18(); 

                }
                break;
            case 5 :
                // Upper.g:1:26: T19
                {
                mT19(); 

                }
                break;
            case 6 :
                // Upper.g:1:30: T20
                {
                mT20(); 

                }
                break;
            case 7 :
                // Upper.g:1:34: T21
                {
                mT21(); 

                }
                break;
            case 8 :
                // Upper.g:1:38: T22
                {
                mT22(); 

                }
                break;
            case 9 :
                // Upper.g:1:42: T23
                {
                mT23(); 

                }
                break;
            case 10 :
                // Upper.g:1:46: T24
                {
                mT24(); 

                }
                break;
            case 11 :
                // Upper.g:1:50: T25
                {
                mT25(); 

                }
                break;
            case 12 :
                // Upper.g:1:54: T26
                {
                mT26(); 

                }
                break;
            case 13 :
                // Upper.g:1:58: T27
                {
                mT27(); 

                }
                break;
            case 14 :
                // Upper.g:1:62: T28
                {
                mT28(); 

                }
                break;
            case 15 :
                // Upper.g:1:66: T29
                {
                mT29(); 

                }
                break;
            case 16 :
                // Upper.g:1:70: T30
                {
                mT30(); 

                }
                break;
            case 17 :
                // Upper.g:1:74: T31
                {
                mT31(); 

                }
                break;
            case 18 :
                // Upper.g:1:78: T32
                {
                mT32(); 

                }
                break;
            case 19 :
                // Upper.g:1:82: SELECT
                {
                mSELECT(); 

                }
                break;
            case 20 :
                // Upper.g:1:89: FROM
                {
                mFROM(); 

                }
                break;
            case 21 :
                // Upper.g:1:94: DECIMALLITERAL
                {
                mDECIMALLITERAL(); 

                }
                break;
            case 22 :
                // Upper.g:1:109: MARKS
                {
                mMARKS(); 

                }
                break;
            case 23 :
                // Upper.g:1:115: STRING
                {
                mSTRING(); 

                }
                break;
            case 24 :
                // Upper.g:1:122: KOREA
                {
                mKOREA(); 

                }
                break;
            case 25 :
                // Upper.g:1:128: CHINESE
                {
                mCHINESE(); 

                }
                break;
            case 26 :
                // Upper.g:1:136: JAPAN
                {
                mJAPAN(); 

                }
                break;
            case 27 :
                // Upper.g:1:142: QUOTA
                {
                mQUOTA(); 

                }
                break;
            case 28 :
                // Upper.g:1:148: ID
                {
                mID(); 

                }
                break;
            case 29 :
                // Upper.g:1:151: ML_COMMENT
                {
                mML_COMMENT(); 

                }
                break;

        }

    }


    protected DFA7 dfa7 = new DFA7(this);
    static final String DFA7_eotS =
        "\5\uffff\1\36\14\uffff\1\37\2\34\1\42\1\uffff\1\43\10\uffff\2\34"+
        "\3\uffff\3\34\1\52\1\34\1\uffff\1\54\1\uffff";
    static final String DFA7_eofS =
        "\55\uffff";
    static final String DFA7_minS =
        "\1\11\4\uffff\1\55\14\uffff\1\52\1\145\1\162\1\60\1\uffff\1\40\10"+
        "\uffff\1\154\1\157\3\uffff\1\145\1\155\1\143\1\60\1\164\1\uffff"+
        "\1\60\1\uffff";
    static final String DFA7_maxS =
        "\1\ud7af\4\uffff\1\55\14\uffff\1\52\1\145\1\162\1\172\1\uffff\1"+
        "\ud7af\10\uffff\1\154\1\157\3\uffff\1\145\1\155\1\143\1\172\1\164"+
        "\1\uffff\1\172\1\uffff";
    static final String DFA7_acceptS =
        "\1\uffff\1\1\1\2\1\3\1\4\1\uffff\1\6\1\7\1\10\1\11\1\12\1\13\1\14"+
        "\1\15\1\16\1\17\1\20\1\21\4\uffff\1\26\1\uffff\1\30\1\31\1\30\1"+
        "\32\1\34\1\35\1\5\1\22\2\uffff\1\25\1\33\1\27\5\uffff\1\24\1\uffff"+
        "\1\23";
    static final String DFA7_specialS =
        "\55\uffff}>";
    static final String[] DFA7_transitionS = {
            "\1\16\1\14\2\uffff\1\15\22\uffff\1\17\1\uffff\1\27\1\uffff\1"+
            "\20\2\uffff\1\27\2\26\1\7\1\3\1\13\1\5\1\12\1\22\12\25\1\11"+
            "\1\10\1\1\1\6\1\2\1\21\1\uffff\32\34\1\26\1\uffff\1\26\1\uffff"+
            "\1\34\1\uffff\5\34\1\24\14\34\1\23\7\34\1\26\1\4\1\26\u1082"+
            "\uffff\u0100\32\u1e40\uffff\u00f0\33\140\30\160\33\u1c00\uffff"+
            "\u51a6\31\u0c5a\uffff\u2bb0\32",
            "",
            "",
            "",
            "",
            "\1\35",
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
            "\1\35",
            "\1\40",
            "\1\41",
            "\12\25\7\uffff\32\34\4\uffff\1\34\1\uffff\32\34",
            "",
            "\1\44\1\uffff\1\44\4\uffff\3\44\2\uffff\17\44\6\uffff\35\44"+
            "\1\uffff\1\44\1\uffff\33\44\1\uffff\1\44\u1082\uffff\u0100\44"+
            "\u1e40\uffff\u01c0\44\u1c00\uffff\u51a6\44\u0c5a\uffff\u2bb0"+
            "\44",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "\1\45",
            "\1\46",
            "",
            "",
            "",
            "\1\47",
            "\1\50",
            "\1\51",
            "\12\34\7\uffff\32\34\4\uffff\1\34\1\uffff\32\34",
            "\1\53",
            "",
            "\12\34\7\uffff\32\34\4\uffff\1\34\1\uffff\32\34",
            ""
    };

    static final short[] DFA7_eot = DFA.unpackEncodedString(DFA7_eotS);
    static final short[] DFA7_eof = DFA.unpackEncodedString(DFA7_eofS);
    static final char[] DFA7_min = DFA.unpackEncodedStringToUnsignedChars(DFA7_minS);
    static final char[] DFA7_max = DFA.unpackEncodedStringToUnsignedChars(DFA7_maxS);
    static final short[] DFA7_accept = DFA.unpackEncodedString(DFA7_acceptS);
    static final short[] DFA7_special = DFA.unpackEncodedString(DFA7_specialS);
    static final short[][] DFA7_transition;

    static {
        int numStates = DFA7_transitionS.length;
        DFA7_transition = new short[numStates][];
        for (int i=0; i<numStates; i++) {
            DFA7_transition[i] = DFA.unpackEncodedString(DFA7_transitionS[i]);
        }
    }

    class DFA7 extends DFA {

        public DFA7(BaseRecognizer recognizer) {
            this.recognizer = recognizer;
            this.decisionNumber = 7;
            this.eot = DFA7_eot;
            this.eof = DFA7_eof;
            this.min = DFA7_min;
            this.max = DFA7_max;
            this.accept = DFA7_accept;
            this.special = DFA7_special;
            this.transition = DFA7_transition;
        }
        public String getDescription() {
            return "1:1: Tokens : ( T15 | T16 | T17 | T18 | T19 | T20 | T21 | T22 | T23 | T24 | T25 | T26 | T27 | T28 | T29 | T30 | T31 | T32 | SELECT | FROM | DECIMALLITERAL | MARKS | STRING | KOREA | CHINESE | JAPAN | QUOTA | ID | ML_COMMENT );";
        }
    }
 

}