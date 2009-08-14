/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: -
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. - Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. - Neither the name of the <ORGANIZATION> nor the names
 * of its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 */
package com.cubrid.cubridmanager.ui.query.format;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.antlr.runtime.ANTLRStringStream;
import org.antlr.runtime.CommonToken;
import org.antlr.runtime.CommonTokenStream;
import org.antlr.runtime.RecognitionException;
import org.antlr.runtime.TokenRewriteStream;
import org.antlr.runtime.tree.CommonTree;
import org.antlr.runtime.tree.Tree;
import org.eclipse.jface.text.formatter.IFormattingStrategy;

import com.cubrid.cubridmanager.ui.query.editor.QuerySyntax;
import com.cubrid.cubridmanager.ui.query.grammar.CubridParser;
import com.cubrid.cubridmanager.ui.query.grammar.SharedParser;
import com.cubrid.cubridmanager.ui.query.grammar.UpperLexer;
import com.cubrid.cubridmanager.ui.query.grammar.UpperParser;
import com.cubrid.cubridmanager.ui.query.grammar.UpperParser.divide_return;

/**
 * a class for sql script format it realize the format operation
 * 
 * @author wangsl 2009-6-4
 */
public class SqlFormattingStrategy implements IFormattingStrategy {

	private static Map<String, String> keywordsMap = new HashMap<String, String>();
	static {
		for (String k : QuerySyntax.KEYWORDS) {
			keywordsMap.put(k, k);
		}

	}

	private List<CommonToken> comments = new ArrayList<CommonToken>();

	/**
	 * format the sql script
	 * 
	 * @param content
	 * @return formatted sql script
	 */
	public String format(String content) {
		return format(content, true, null, null);
	}

	@SuppressWarnings("unchecked")
	public String format(String content, boolean isLineStart, String indentation, int[] positions) {
		try {
			String con = toUppercase(content);
			TokenRewriteStream stream = SharedParser.getTokenStream(con);
			CubridParser parser = new CubridParser(stream);
			CubridParser.execute_return t = parser.execute();
			List allTokens = stream.getTokens();
			if (t.stop != allTokens.get(allTokens.size()-1)) {
				throw new RecognitionException();
			}
			Tree tree = (Tree) t.getTree();
			StringBuffer buf = new StringBuffer();
			int level = 0;
			for (int i = 0; i < tree.getChildCount(); i++) {
				CommonTree child = (CommonTree) tree.getChild(i);
				CommonToken brother = null;
				if (i > 0) {
					brother = (CommonToken) ((CommonTree) tree.getChild(i - 1)).getToken();
				}
				CommonToken comment = null;
				if ((comment = containComments(brother, (CommonToken) child.getToken())) != null) {
					buf.append(comment.getText());
				}
				int tokenType = child.getToken().getType();
				// remove all the space before ';'
				if (tokenType == CubridParser.END) {
					char end = buf.charAt(buf.length() - 1);
					while (end == '\n' || end == ' ' || end == '\r' || end == '\t') {
						buf.deleteCharAt(buf.length() - 1);
						end = buf.charAt(buf.length() - 1);
					}
				}
				if (tokenType == CubridParser.ENTER) {
					buf.append(System.getProperty("line.separator"));
					buf.append(addTab(level));
				} else if (tokenType == CubridParser.UNTAB) {
					buf.deleteCharAt(buf.length() - 1);
					level--;
				} else if (tokenType == CubridParser.TAB) {
					level++;
					buf.append(addTab(1));
				} else if (tokenType == CubridParser.CLEAR) {
					level = 0;
					buf.append(System.getProperty("line.separator"));
				} else {
					String text = child.getText();
					if (tokenType == CubridParser.DOT) {
						if (buf.toString().endsWith(" ")) {
							buf.deleteCharAt(buf.length() - 1);
						}
					}
					buf.append(text);
					if (tokenType != CubridParser.STRING
					        &&tokenType != CubridParser.DOT &&tokenType != CubridParser.DOLLAR) {
						buf.append(" ");
					}
				}
			}
			return buf.toString();
		} catch (RecognitionException e) {
			return content;
		}
	}

	/**
	 * validate sql script
	 * 
	 * @param sql
	 * @return
	 */
	public boolean validate(String sql) {
		try {
			String con = toUppercase(sql);
			TokenRewriteStream stream = SharedParser.getTokenStream(con);
			CubridParser parser = new CubridParser(stream);
			parser.execute();
		} catch (RecognitionException e) {
			return false;
		}
		return true;
	}

	@SuppressWarnings("unchecked")
	private String toUppercase(String content) throws RecognitionException {
		ANTLRStringStream input = new ANTLRStringStream(content);
		UpperLexer upperLexer = new UpperLexer(input);
		CommonTokenStream stream = new CommonTokenStream(upperLexer);
		UpperParser upperParser = new UpperParser(stream);
		@SuppressWarnings("unused")
		divide_return divide = upperParser.divide();
		List tk = stream.getTokens();
		String source = "";
		comments.clear();
		for (Object object : tk) {
			CommonToken t = (CommonToken) object;
			if (!(t.getType() == UpperParser.STRING)) {
				if (keywordsMap.containsKey(t.getText().toUpperCase())) {
					source += keywordsMap.get(t.getText().toUpperCase());
				} else {
					source += t.getText();
				}
			} else {
				source += t.getText();
			}
			if (t.getType() == UpperParser.ML_COMMENT) {
				comments.add(t);
			}
		}
		return source;
	}

	private CommonToken containComments(CommonToken previous, CommonToken current) {

		int i = 0;
		boolean find = false;
		for (i = 0; i < comments.size(); i++) {
			CommonToken t = comments.get(i);
			if (previous == null) {
				if (t.getStopIndex() < current.getStartIndex()) {
					find = true;
					break;
				}
			} else if (t.getStartIndex() > previous.getStopIndex() && t.getStopIndex() < current.getStartIndex()) {
				find = true;
				break;
			}
		}
		if (find) {
			return comments.remove(i);
		}
		return null;
	}

	private static String addTab(int times) {
		String s = "";
		if (times > 0) {
			for (int i = 0; i < times; i++) {
				s += "\t";
			}
		}
		return s;
	}

	public void formatterStarts(String initialIndentation) {

	}

	public void formatterStops() {

	}

}
