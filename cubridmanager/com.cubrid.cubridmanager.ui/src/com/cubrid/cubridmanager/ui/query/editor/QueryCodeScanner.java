/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *  - Neither the name of the <ORGANIZATION> nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
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

package com.cubrid.cubridmanager.ui.query.editor;

import java.util.ArrayList;
import java.util.List;

import org.eclipse.jface.text.TextAttribute;
import org.eclipse.jface.text.rules.IRule;
import org.eclipse.jface.text.rules.IToken;
import org.eclipse.jface.text.rules.IWhitespaceDetector;
import org.eclipse.jface.text.rules.MultiLineRule;
import org.eclipse.jface.text.rules.RuleBasedScanner;
import org.eclipse.jface.text.rules.Token;
import org.eclipse.jface.text.rules.WhitespaceRule;
import org.eclipse.swt.SWT;

/**
 * This class scans through a code partition and colors it.
 */
public class QueryCodeScanner extends
		RuleBasedScanner {
	/**
	 * PerlCodeScanner constructor
	 */
	public QueryCodeScanner() {
		// Get the color manager
		ColorManager cm = new ColorManager();

		// Create the tokens for keywords, strings, and other (everything else)
		IToken keyword = new Token(new TextAttribute(
				cm.getColor(ColorManager.KEYWORD),
				cm.getColor(ColorManager.BACKGROUND), SWT.BOLD));
		IToken other = new Token(new TextAttribute(
				cm.getColor(ColorManager.DEFAULT)));
		IToken string = new Token(new TextAttribute(
				cm.getColor(ColorManager.STRING)));
		IToken table = new Token(new TextAttribute(
				cm.getColor(ColorManager.STRING)));
		// token for column : doesn't embodiment
		IToken column = new Token(new TextAttribute(
				cm.getColor(ColorManager.STRING)));

		// Use "other" for default
		setDefaultReturnToken(other);

		// Create the rules
		List<IRule> rules = new ArrayList<IRule>();

		// Add rules for strings
		rules.add(new MultiLineRule("'", "'", string, '\n'));
		rules.add(new MultiLineRule("\"", "\"", string, '\n'));
		// rules.add(new NumberRule(number));

		// Add rule for whitespace
		rules.add(new WhitespaceRule(new IWhitespaceDetector() {
			public boolean isWhitespace(char c) {
				return Character.isWhitespace(c);
			}
		}));

		// Add word rule for keywords, types, and constants.

		UnsignedWordRule wordRule = new UnsignedWordRule(
				new QueryWordDetector(), other, // default
				// token
				table, // table token
				column // column token
		);

		for (int i = 0; i < QuerySyntax.KEYWORDS.length; i++)
			wordRule.addWord(QuerySyntax.KEYWORDS[i], keyword);
		rules.add(wordRule);

		IRule[] result = new IRule[rules.size()];
		rules.toArray(result);
		setRules(result);
	}
}
