// $ANTLR 3.0.1 Upper.g 2009-07-10 16:32:03
package com.cubrid.cubridmanager.ui.query.grammar;

import org.antlr.runtime.BitSet;
import org.antlr.runtime.IntStream;
import org.antlr.runtime.Parser;
import org.antlr.runtime.ParserRuleReturnScope;
import org.antlr.runtime.RecognitionException;
import org.antlr.runtime.Token;
import org.antlr.runtime.TokenStream;
import org.antlr.runtime.tree.CommonTreeAdaptor;
import org.antlr.runtime.tree.TreeAdaptor;

public class UpperParser extends
		Parser {
	public static final String[] tokenNames = new String[] { "<invalid>",
			"<EOR>", "<DOWN>", "<UP>", "ID", "STRING", "DECIMALLITERAL",
			"MARKS", "ML_COMMENT", "SELECT", "FROM", "QUOTA", "KOREA",
			"CHINESE", "JAPAN", "'<'", "'>'", "'+'", "'|'", "'-'", "'='",
			"'*'", "';'", "':'", "'.'", "','", "'\\n'", "'\\r'", "'\\t'",
			"' '", "'$'", "'?'", "'/'" };
	public static final int DECIMALLITERAL = 6;
	public static final int ML_COMMENT = 8;
	public static final int QUOTA = 11;
	public static final int JAPAN = 14;
	public static final int MARKS = 7;
	public static final int CHINESE = 13;
	public static final int FROM = 10;
	public static final int ID = 4;
	public static final int EOF = -1;
	public static final int SELECT = 9;
	public static final int KOREA = 12;
	public static final int STRING = 5;

	public UpperParser(TokenStream input) {
		super(input);
	}

	protected TreeAdaptor adaptor = new CommonTreeAdaptor();

	public void setTreeAdaptor(TreeAdaptor adaptor) {
		this.adaptor = adaptor;
	}

	public TreeAdaptor getTreeAdaptor() {
		return adaptor;
	}

	public String[] getTokenNames() {
		return tokenNames;
	}

	public String getGrammarFileName() {
		return "Upper.g";
	}

	String temp = "";

	protected void mismatch(IntStream input, int ttype, BitSet follow) throws RecognitionException {
		throw new RecognitionException();
	}

	public void recoverFromMismatchedSet(IntStream input,
			RecognitionException e, BitSet follow) throws RecognitionException {
		throw e;
	}

	public static class divide_return extends
			ParserRuleReturnScope {
		Object tree;

		public Object getTree() {
			return tree;
		}
	};

	// $ANTLR start divide
	// Upper.g:30:1: divide : ( ID | STRING | DECIMALLITERAL | '<' | '>' | '+' | '|' | '-' | '=' | MARKS | '*' | ';' | ':' | ID '.' ID | ',' | '\\n' | '\\r' | '\\t' | ' ' | '$' | ML_COMMENT | '?' | '/' )* ;
	public final divide_return divide() throws RecognitionException {
		divide_return retval = new divide_return();
		retval.start = input.LT(1);

		Object root_0 = null;

		Token ID1 = null;
		Token STRING2 = null;
		Token DECIMALLITERAL3 = null;
		Token char_literal4 = null;
		Token char_literal5 = null;
		Token char_literal6 = null;
		Token char_literal7 = null;
		Token char_literal8 = null;
		Token char_literal9 = null;
		Token MARKS10 = null;
		Token char_literal11 = null;
		Token char_literal12 = null;
		Token char_literal13 = null;
		Token ID14 = null;
		Token char_literal15 = null;
		Token ID16 = null;
		Token char_literal17 = null;
		Token char_literal18 = null;
		Token char_literal19 = null;
		Token char_literal20 = null;
		Token char_literal21 = null;
		Token char_literal22 = null;
		Token ML_COMMENT23 = null;
		Token char_literal24 = null;
		Token char_literal25 = null;

		Object ID1_tree = null;
		Object STRING2_tree = null;
		Object DECIMALLITERAL3_tree = null;
		Object char_literal4_tree = null;
		Object char_literal5_tree = null;
		Object char_literal6_tree = null;
		Object char_literal7_tree = null;
		Object char_literal8_tree = null;
		Object char_literal9_tree = null;
		Object MARKS10_tree = null;
		Object char_literal11_tree = null;
		Object char_literal12_tree = null;
		Object char_literal13_tree = null;
		Object ID14_tree = null;
		Object char_literal15_tree = null;
		Object ID16_tree = null;
		Object char_literal17_tree = null;
		Object char_literal18_tree = null;
		Object char_literal19_tree = null;
		Object char_literal20_tree = null;
		Object char_literal21_tree = null;
		Object char_literal22_tree = null;
		Object ML_COMMENT23_tree = null;
		Object char_literal24_tree = null;
		Object char_literal25_tree = null;

		try {
			// Upper.g:30:7: ( ( ID | STRING | DECIMALLITERAL | '<' | '>' | '+' | '|' | '-' | '=' | MARKS | '*' | ';' | ':' | ID '.' ID | ',' | '\\n' | '\\r' | '\\t' | ' ' | '$' | ML_COMMENT | '?' | '/' )* )
			// Upper.g:31:2: ( ID | STRING | DECIMALLITERAL | '<' | '>' | '+' | '|' | '-' | '=' | MARKS | '*' | ';' | ':' | ID '.' ID | ',' | '\\n' | '\\r' | '\\t' | ' ' | '$' | ML_COMMENT | '?' | '/' )*
			{
				root_0 = (Object) adaptor.nil();

				// Upper.g:31:2: ( ID | STRING | DECIMALLITERAL | '<' | '>' | '+' | '|' | '-' | '=' | MARKS | '*' | ';' | ':' | ID '.' ID | ',' | '\\n' | '\\r' | '\\t' | ' ' | '$' | ML_COMMENT | '?' | '/' )*
				loop1: do {
					int alt1 = 24;
					switch (input.LA(1)) {
					case ID: {
						int LA1_2 = input.LA(2);

						if ((LA1_2 == 24)) {
							alt1 = 14;
						} else if ((LA1_2 == EOF
								|| (LA1_2 >= ID && LA1_2 <= ML_COMMENT)
								|| (LA1_2 >= 15 && LA1_2 <= 23) || (LA1_2 >= 25 && LA1_2 <= 32))) {
							alt1 = 1;
						}

					}
						break;
					case STRING: {
						alt1 = 2;
					}
						break;
					case DECIMALLITERAL: {
						alt1 = 3;
					}
						break;
					case 15: {
						alt1 = 4;
					}
						break;
					case 16: {
						alt1 = 5;
					}
						break;
					case 17: {
						alt1 = 6;
					}
						break;
					case 18: {
						alt1 = 7;
					}
						break;
					case 19: {
						alt1 = 8;
					}
						break;
					case 20: {
						alt1 = 9;
					}
						break;
					case MARKS: {
						alt1 = 10;
					}
						break;
					case 21: {
						alt1 = 11;
					}
						break;
					case 22: {
						alt1 = 12;
					}
						break;
					case 23: {
						alt1 = 13;
					}
						break;
					case 25: {
						alt1 = 15;
					}
						break;
					case 26: {
						alt1 = 16;
					}
						break;
					case 27: {
						alt1 = 17;
					}
						break;
					case 28: {
						alt1 = 18;
					}
						break;
					case 29: {
						alt1 = 19;
					}
						break;
					case 30: {
						alt1 = 20;
					}
						break;
					case ML_COMMENT: {
						alt1 = 21;
					}
						break;
					case 31: {
						alt1 = 22;
					}
						break;
					case 32: {
						alt1 = 23;
					}
						break;

					}

					switch (alt1) {
					case 1:
						// Upper.g:32:3: ID
					{
						ID1 = (Token) input.LT(1);
						match(input, ID, FOLLOW_ID_in_divide54);
						ID1_tree = (Object) adaptor.create(ID1);
						adaptor.addChild(root_0, ID1_tree);

					}
						break;
					case 2:
						// Upper.g:33:4: STRING
					{
						STRING2 = (Token) input.LT(1);
						match(input, STRING, FOLLOW_STRING_in_divide59);
						STRING2_tree = (Object) adaptor.create(STRING2);
						adaptor.addChild(root_0, STRING2_tree);

					}
						break;
					case 3:
						// Upper.g:34:4: DECIMALLITERAL
					{
						DECIMALLITERAL3 = (Token) input.LT(1);
						match(input, DECIMALLITERAL,
								FOLLOW_DECIMALLITERAL_in_divide64);
						DECIMALLITERAL3_tree = (Object) adaptor.create(DECIMALLITERAL3);
						adaptor.addChild(root_0, DECIMALLITERAL3_tree);

					}
						break;
					case 4:
						// Upper.g:35:4: '<'
					{
						char_literal4 = (Token) input.LT(1);
						match(input, 15, FOLLOW_15_in_divide69);
						char_literal4_tree = (Object) adaptor.create(char_literal4);
						adaptor.addChild(root_0, char_literal4_tree);

					}
						break;
					case 5:
						// Upper.g:36:4: '>'
					{
						char_literal5 = (Token) input.LT(1);
						match(input, 16, FOLLOW_16_in_divide74);
						char_literal5_tree = (Object) adaptor.create(char_literal5);
						adaptor.addChild(root_0, char_literal5_tree);

					}
						break;
					case 6:
						// Upper.g:37:4: '+'
					{
						char_literal6 = (Token) input.LT(1);
						match(input, 17, FOLLOW_17_in_divide79);
						char_literal6_tree = (Object) adaptor.create(char_literal6);
						adaptor.addChild(root_0, char_literal6_tree);

					}
						break;
					case 7:
						// Upper.g:38:4: '|'
					{
						char_literal7 = (Token) input.LT(1);
						match(input, 18, FOLLOW_18_in_divide84);
						char_literal7_tree = (Object) adaptor.create(char_literal7);
						adaptor.addChild(root_0, char_literal7_tree);

					}
						break;
					case 8:
						// Upper.g:39:4: '-'
					{
						char_literal8 = (Token) input.LT(1);
						match(input, 19, FOLLOW_19_in_divide89);
						char_literal8_tree = (Object) adaptor.create(char_literal8);
						adaptor.addChild(root_0, char_literal8_tree);

					}
						break;
					case 9:
						// Upper.g:40:4: '='
					{
						char_literal9 = (Token) input.LT(1);
						match(input, 20, FOLLOW_20_in_divide94);
						char_literal9_tree = (Object) adaptor.create(char_literal9);
						adaptor.addChild(root_0, char_literal9_tree);

					}
						break;
					case 10:
						// Upper.g:41:5: MARKS
					{
						MARKS10 = (Token) input.LT(1);
						match(input, MARKS, FOLLOW_MARKS_in_divide100);
						MARKS10_tree = (Object) adaptor.create(MARKS10);
						adaptor.addChild(root_0, MARKS10_tree);

					}
						break;
					case 11:
						// Upper.g:42:4: '*'
					{
						char_literal11 = (Token) input.LT(1);
						match(input, 21, FOLLOW_21_in_divide105);
						char_literal11_tree = (Object) adaptor.create(char_literal11);
						adaptor.addChild(root_0, char_literal11_tree);

					}
						break;
					case 12:
						// Upper.g:43:4: ';'
					{
						char_literal12 = (Token) input.LT(1);
						match(input, 22, FOLLOW_22_in_divide110);
						char_literal12_tree = (Object) adaptor.create(char_literal12);
						adaptor.addChild(root_0, char_literal12_tree);

					}
						break;
					case 13:
						// Upper.g:44:4: ':'
					{
						char_literal13 = (Token) input.LT(1);
						match(input, 23, FOLLOW_23_in_divide115);
						char_literal13_tree = (Object) adaptor.create(char_literal13);
						adaptor.addChild(root_0, char_literal13_tree);

					}
						break;
					case 14:
						// Upper.g:45:4: ID '.' ID
					{
						ID14 = (Token) input.LT(1);
						match(input, ID, FOLLOW_ID_in_divide120);
						ID14_tree = (Object) adaptor.create(ID14);
						adaptor.addChild(root_0, ID14_tree);

						char_literal15 = (Token) input.LT(1);
						match(input, 24, FOLLOW_24_in_divide122);
						char_literal15_tree = (Object) adaptor.create(char_literal15);
						adaptor.addChild(root_0, char_literal15_tree);

						ID16 = (Token) input.LT(1);
						match(input, ID, FOLLOW_ID_in_divide124);
						ID16_tree = (Object) adaptor.create(ID16);
						adaptor.addChild(root_0, ID16_tree);

					}
						break;
					case 15:
						// Upper.g:46:4: ','
					{
						char_literal17 = (Token) input.LT(1);
						match(input, 25, FOLLOW_25_in_divide129);
						char_literal17_tree = (Object) adaptor.create(char_literal17);
						adaptor.addChild(root_0, char_literal17_tree);

					}
						break;
					case 16:
						// Upper.g:47:4: '\\n'
					{
						char_literal18 = (Token) input.LT(1);
						match(input, 26, FOLLOW_26_in_divide134);
						char_literal18_tree = (Object) adaptor.create(char_literal18);
						adaptor.addChild(root_0, char_literal18_tree);

					}
						break;
					case 17:
						// Upper.g:48:4: '\\r'
					{
						char_literal19 = (Token) input.LT(1);
						match(input, 27, FOLLOW_27_in_divide139);
						char_literal19_tree = (Object) adaptor.create(char_literal19);
						adaptor.addChild(root_0, char_literal19_tree);

					}
						break;
					case 18:
						// Upper.g:49:4: '\\t'
					{
						char_literal20 = (Token) input.LT(1);
						match(input, 28, FOLLOW_28_in_divide144);
						char_literal20_tree = (Object) adaptor.create(char_literal20);
						adaptor.addChild(root_0, char_literal20_tree);

					}
						break;
					case 19:
						// Upper.g:50:4: ' '
					{
						char_literal21 = (Token) input.LT(1);
						match(input, 29, FOLLOW_29_in_divide149);
						char_literal21_tree = (Object) adaptor.create(char_literal21);
						adaptor.addChild(root_0, char_literal21_tree);

					}
						break;
					case 20:
						// Upper.g:51:4: '$'
					{
						char_literal22 = (Token) input.LT(1);
						match(input, 30, FOLLOW_30_in_divide154);
						char_literal22_tree = (Object) adaptor.create(char_literal22);
						adaptor.addChild(root_0, char_literal22_tree);

					}
						break;
					case 21:
						// Upper.g:52:4: ML_COMMENT
					{
						ML_COMMENT23 = (Token) input.LT(1);
						match(input, ML_COMMENT, FOLLOW_ML_COMMENT_in_divide159);
						ML_COMMENT23_tree = (Object) adaptor.create(ML_COMMENT23);
						adaptor.addChild(root_0, ML_COMMENT23_tree);

					}
						break;
					case 22:
						// Upper.g:53:4: '?'
					{
						char_literal24 = (Token) input.LT(1);
						match(input, 31, FOLLOW_31_in_divide164);
						char_literal24_tree = (Object) adaptor.create(char_literal24);
						adaptor.addChild(root_0, char_literal24_tree);

					}
						break;
					case 23:
						// Upper.g:54:4: '/'
					{
						char_literal25 = (Token) input.LT(1);
						match(input, 32, FOLLOW_32_in_divide169);
						char_literal25_tree = (Object) adaptor.create(char_literal25);
						adaptor.addChild(root_0, char_literal25_tree);

					}
						break;

					default:
						break loop1;
					}
				} while (true);

			}

			retval.stop = input.LT(-1);

			retval.tree = (Object) adaptor.rulePostProcessing(root_0);
			adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

		} catch (RecognitionException re) {
			reportError(re);
			recover(input, re);
		} finally {
		}
		return retval;
	}

	// $ANTLR end divide

	public static class select_return extends
			ParserRuleReturnScope {
		Object tree;

		public Object getTree() {
			return tree;
		}
	};

	// $ANTLR start select
	// Upper.g:58:1: select : DECIMALLITERAL ;
	public final select_return select() throws RecognitionException {
		select_return retval = new select_return();
		retval.start = input.LT(1);

		Object root_0 = null;

		Token DECIMALLITERAL26 = null;

		Object DECIMALLITERAL26_tree = null;

		try {
			// Upper.g:58:8: ( DECIMALLITERAL )
			// Upper.g:59:2: DECIMALLITERAL
			{
				root_0 = (Object) adaptor.nil();

				DECIMALLITERAL26 = (Token) input.LT(1);
				match(input, DECIMALLITERAL, FOLLOW_DECIMALLITERAL_in_select185);
				DECIMALLITERAL26_tree = (Object) adaptor.create(DECIMALLITERAL26);
				adaptor.addChild(root_0, DECIMALLITERAL26_tree);

			}

			retval.stop = input.LT(-1);

			retval.tree = (Object) adaptor.rulePostProcessing(root_0);
			adaptor.setTokenBoundaries(retval.tree, retval.start, retval.stop);

		} catch (RecognitionException re) {
			reportError(re);
			recover(input, re);
		} finally {
		}
		return retval;
	}

	// $ANTLR end select

	public static final BitSet FOLLOW_ID_in_divide54 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_STRING_in_divide59 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_DECIMALLITERAL_in_divide64 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_15_in_divide69 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_16_in_divide74 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_17_in_divide79 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_18_in_divide84 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_19_in_divide89 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_20_in_divide94 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_MARKS_in_divide100 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_21_in_divide105 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_22_in_divide110 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_23_in_divide115 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_ID_in_divide120 = new BitSet(
			new long[] { 0x0000000001000000L });
	public static final BitSet FOLLOW_24_in_divide122 = new BitSet(
			new long[] { 0x0000000000000010L });
	public static final BitSet FOLLOW_ID_in_divide124 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_25_in_divide129 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_26_in_divide134 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_27_in_divide139 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_28_in_divide144 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_29_in_divide149 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_30_in_divide154 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_ML_COMMENT_in_divide159 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_31_in_divide164 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_32_in_divide169 = new BitSet(
			new long[] { 0x00000001FEFF81F2L });
	public static final BitSet FOLLOW_DECIMALLITERAL_in_select185 = new BitSet(
			new long[] { 0x0000000000000002L });

}