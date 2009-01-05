/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
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
package cubridmanager.query.view;

import java.util.ArrayList;
import java.util.List;
import java.util.Stack;

/**
 * parse sql,add rownum to paginate
 * 
 * @author pangqiren
 * 
 */
public class SqlParser {

	/**
	 * 
	 * @param sql
	 * @return
	 */
	public static String parse(String sql) {
		if (sql == null || sql.length() < 0) {
			return null;
		}
		sql = sql.replaceAll(";", "").trim();
		String lowerSql = sql.toLowerCase();
		if (lowerSql.length() < 7
				|| !lowerSql.substring(0, 7).matches("select\\s")) {
			return null;
		}
		List<SqlToken> tokenList = new ArrayList<SqlToken>();
		Stack<Integer> bracketStack = new Stack<Integer>();
		char[] charArray = lowerSql.toCharArray();
		int length = charArray.length;
		for (int i = 0; i < length; i++) {
			if (charArray[i] == '(') { // left bracket
				bracketStack.push(i);
			} else if (charArray[i] == ')') { // right bracket
				Integer start = (Integer) bracketStack.pop();
				tokenList.add(new SqlToken(SqlTokenType.BRACKET, start, i));
			} else if (charArray[i] == '/') { // comment
				if (i + 1 < length && charArray[i + 1] == '/') {
					// comment(//)
					int start = i;
					i = getCommentEndPos(charArray, i + 2);
					if (i < 0) {
						return null;
					}
					tokenList
							.add(new SqlToken(
									SqlTokenType.DOUBLE_VERTICAL_LINE_COMMENT,
									start, i));
				} else if (i + 1 < length && charArray[i + 1] == '*') {
					// block comment(/*...*/)
					int start = i;
					i = getBlockCommentEndPos(charArray, i + 2);
					if (i < 0) {
						return null;
					}
					tokenList.add(new SqlToken(SqlTokenType.BLOCK_COMMENT,
							start, i));
				}
			} else if (i + 1 < length && charArray[i] == '-'
					&& charArray[i + 1] == '-') {
				// comment(--)
				int start = i;
				i = getCommentEndPos(charArray, i + 2);
				if (i < 0) {
					return null;
				}
				tokenList.add(new SqlToken(
						SqlTokenType.DOUBLE_HORIZONTAL_LINE_COMMENT, start, i));
			} else if (i - 1 > 0 && charArray[i] == '\''
					&& charArray[i - 1] != '\\') {
				// single quotation('')
				int start = i;
				i = getSingleQuoteEndPos(charArray, i + 1);
				if (i < 0) {
					return null;
				}
				tokenList
						.add(new SqlToken(SqlTokenType.SINGLE_QUOTE, start, i));
			} else if (i - 1 > 0 && charArray[i] == '"'
					&& charArray[i - 1] != '\\') {
				// double quotation(")
				int start = i;
				i = getDoubleQuoteEndPos(charArray, i + 1);
				if (i < 0) {
					return null;
				}
				tokenList
						.add(new SqlToken(SqlTokenType.DOUBLE_QUOTE, start, i));
			}
		}
		if (bracketStack.size() > 0
				|| isHasOuterClause(lowerSql, tokenList, "where")
				|| isHasOuterByClause(lowerSql, tokenList, "group")
				|| isHasOuterByClause(lowerSql, tokenList, "order")
				|| isHasOuterClause(lowerSql, tokenList, "union")
				|| isHasOuterClause(lowerSql, tokenList, "difference")
				|| isHasOuterClause(lowerSql, tokenList, "intersect")
				|| isHasOuterJoinClause(lowerSql, tokenList, "left")
				|| isHasOuterJoinClause(lowerSql, tokenList, "right")) {
			return null;
		}
		int insertPos = lowerSql.length();
		int usingPos = getOuterUsingClausePos(lowerSql, tokenList);
		if (usingPos > 0 && usingPos < insertPos)
			insertPos = usingPos - 1;
		String prePartSql = sql.substring(0, insertPos);
		String afterPartSql = "";
		if (insertPos < sql.length() - 1) {
			afterPartSql = sql.substring(insertPos + 1);
		}
		String parsedSql = prePartSql
				+ "\r\nwhere rownum between ${start} and ${end}";
		if (afterPartSql.length() == 0) {
			parsedSql += ";";
		} else {
			String[] afterPartSqlArr = afterPartSql.split("\\n");
			String lastLine = afterPartSqlArr[afterPartSqlArr.length - 1];
			if (lastLine.indexOf("//") >= 0 || lastLine.indexOf("--") >= 0) {
				parsedSql += "\r\n" + afterPartSql + "\r\n" + ";";
			} else {
				parsedSql += "\r\n" + afterPartSql + ";";
			}
		}
		return parsedSql;
	}

	/**
	 * 
	 * @param sql
	 * @param tokenList
	 * @param clause
	 * @return
	 */
	private static boolean isHasOuterClause(String sql,
			List<SqlToken> tokenList, String clause) {
		int i = 0;
		int pos = 0;
		while (i < sql.length() && (pos = sql.indexOf(clause, i)) > 0) {
			i = pos + clause.length();
			String preStr = " ";
			if (pos > 1)
				preStr = String.valueOf(sql.charAt(pos - 1));
			String afterStr = " ";
			if (i < sql.length())
				afterStr = String.valueOf(sql.charAt(i));
			if (preStr.matches("\\s") && afterStr.matches("\\s")) {
				boolean isOuterWhereSql = true;
				for (int j = 0; j < tokenList.size(); j++) {
					SqlToken sqlToken = tokenList.get(j);
					if (sqlToken.start < pos && pos < sqlToken.end) {
						isOuterWhereSql = false;
					}
				}
				if (isOuterWhereSql) {
					return true;
				}
			}
		}
		return false;
	}

	/**
	 * 
	 * @param sql
	 * @param tokenList
	 * @param clause
	 * @return
	 */
	private static boolean isHasOuterByClause(String sql,
			List<SqlToken> tokenList, String clause) {
		int i = 0;
		int pos = 0;
		while (i < sql.length() && (pos = sql.indexOf(clause, i)) >= 0) {
			i = pos + clause.length();
			int byPos = -1;
			if (i < sql.length())
				byPos = sql.indexOf("by", i);
			if (byPos > 0 && sql.substring(i, byPos).matches("\\s+")) {
				i = byPos + 2;
				String preStr = " ";
				if (pos > 1)
					preStr = String.valueOf(sql.charAt(pos - 1));
				String afterStr = " ";
				if (i < sql.length())
					afterStr = String.valueOf(sql.charAt(i));
				if (preStr.matches("\\s") && afterStr.matches("\\s")) {
					boolean isHasOuterGroupClause = true;
					for (int j = 0; j < tokenList.size(); j++) {
						SqlToken sqlToken = tokenList.get(j);
						if (sqlToken.start < pos && pos < sqlToken.end) {
							isHasOuterGroupClause = false;
						}
					}
					if (isHasOuterGroupClause) {
						return true;
					}
				}
			}
		}
		return false;
	}

	/**
	 * 
	 * @param sql
	 * @param tokenList
	 * @param clause
	 * @return
	 */
	private static boolean isHasOuterJoinClause(String sql,
			List<SqlToken> tokenList, String clause) {
		int i = 0;
		int pos = 0;
		while (i < sql.length() && (pos = sql.indexOf(clause, i)) >= 0) {
			i = pos + clause.length();
			int outerPos = -1;
			if (i < sql.length())
				outerPos = sql.indexOf("outer", i);
			if (outerPos > 0 && sql.substring(i, outerPos).matches("\\s+")) {
				i = outerPos + 5;
				int joinPos = -1;
				if (i < sql.length())
					joinPos = sql.indexOf("join", i);
				if (joinPos > 0 && sql.substring(i, joinPos).matches("\\s+")) {
					i = joinPos + 4;
					String preStr = " ";
					if (pos > 1)
						preStr = String.valueOf(sql.charAt(pos - 1));
					String afterStr = " ";
					if (i < sql.length())
						afterStr = String.valueOf(sql.charAt(i));
					if (preStr.matches("\\s") && afterStr.matches("\\s")) {
						boolean isHasOuterLeftOuterJoinClause = true;
						for (int j = 0; j < tokenList.size(); j++) {
							SqlToken sqlToken = tokenList.get(j);
							if (sqlToken.start < pos && pos < sqlToken.end) {
								isHasOuterLeftOuterJoinClause = false;
							}
						}
						if (isHasOuterLeftOuterJoinClause) {
							return true;
						}
					}
				}
			}
		}
		return false;
	}

	/**
	 * 
	 * @param sql
	 * @param tokenList
	 * @return
	 */
	private static int getOuterUsingClausePos(String sql,
			List<SqlToken> tokenList) {
		int i = 0;
		int pos = 0;
		while (i < sql.length() && (pos = sql.indexOf("using", i)) >= 0) {
			i = pos + 5;
			int indexPos = -1;
			if (i < sql.length())
				indexPos = sql.indexOf("index", i);
			if (indexPos > 0 && sql.substring(i, indexPos).matches("\\s+")) {
				i = indexPos + 5;
				String preStr = " ";
				if (pos > 1)
					preStr = String.valueOf(sql.charAt(pos - 1));
				String afterStr = " ";
				if (i < sql.length())
					afterStr = String.valueOf(sql.charAt(i));
				if (preStr.matches("\\s") && afterStr.matches("\\s")) {
					boolean isOuterUsingClause = true;
					for (int j = 0; j < tokenList.size(); j++) {
						SqlToken sqlToken = tokenList.get(j);
						if (sqlToken.start < pos && pos < sqlToken.end) {
							isOuterUsingClause = false;
						}
					}
					if (isOuterUsingClause) {
						return pos;
					}
				}
			}
		}
		return -1;
	}

	/**
	 * 
	 * @param charArray
	 * @param start
	 * @return
	 */
	private static int getCommentEndPos(char[] charArray, int start) {
		for (int i = start; i < charArray.length; i++) {
			if (charArray[i] == '\n' || i == charArray.length - 1) {
				return i;
			}
		}
		return -1;
	}

	/**
	 * 
	 * @param charArray
	 * @param start
	 * @return
	 */
	private static int getBlockCommentEndPos(char[] charArray, int start) {
		for (int i = start; i < charArray.length - 1; i++) {
			if (charArray[i] == '*' && charArray[i + 1] == '/') {
				return i + 1;
			}
		}
		return -1;
	}

	/**
	 * 
	 * @param charArray
	 * @param start
	 * @return
	 */
	private static int getSingleQuoteEndPos(char[] charArray, int start) {
		for (int i = start; i < charArray.length; i++) {
			if (charArray[i] == '\'' && charArray[i - 1] != '\\') {
				return i;
			}
		}
		return -1;
	}

	/**
	 * 
	 * @param charArray
	 * @param start
	 * @return
	 */
	private static int getDoubleQuoteEndPos(char[] charArray, int start) {
		for (int i = start; i < charArray.length; i++) {
			if (charArray[i] == '"' && charArray[i - 1] != '\\') {
				return i;
			}
		}
		return -1;
	}

	/**
	 * record the token start postion and end positon
	 * 
	 * @author pangqiren
	 * 
	 */
	private static class SqlToken {
		@SuppressWarnings("unused")
		private SqlTokenType tokenType = null;
		private int start = 0;
		private int end = 0;

		private SqlToken(SqlTokenType tokenType, int start, int end) {
			this.tokenType = tokenType;
			this.start = start;
			this.end = end;
		}
	}

	/**
	 * token type
	 * 
	 * @author pangqiren
	 * 
	 */
	private enum SqlTokenType {
		DOUBLE_VERTICAL_LINE_COMMENT, BLOCK_COMMENT, DOUBLE_HORIZONTAL_LINE_COMMENT, SINGLE_QUOTE, DOUBLE_QUOTE, BRACKET
	}
}
