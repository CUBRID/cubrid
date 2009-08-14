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

import java.util.HashMap;
import java.util.Map;

import org.eclipse.jface.text.rules.ICharacterScanner;
import org.eclipse.jface.text.rules.IRule;
import org.eclipse.jface.text.rules.IToken;
import org.eclipse.jface.text.rules.IWordDetector;
import org.eclipse.jface.text.rules.Token;

public class UnsignedWordRule implements
		IRule {

	protected static final int UNDEFINED = -1;
	/** The word detector used by this rule */
	protected IWordDetector fDetector;
	/**
	 * The default token to be returned on success and if nothing else has been
	 * specified.
	 */
	protected IToken fDefaultToken;
	/** The column constraint */
	protected int fColumn = UNDEFINED;
	/** The table of predefined words and token for this rule */
	public Map<String, IToken> fWords = new HashMap<String, IToken>();
	private StringBuffer fBuffer = new StringBuffer();

	public UnsignedWordRule(IWordDetector detector) {
		this(detector, Token.UNDEFINED, null, null);

	}

	// public UnsignedWordRule(IWordDetector detector, IToken defaultToken) {
	public UnsignedWordRule(IWordDetector detector, IToken defaultToken,
			IToken tableToken, IToken columnToken) {
		fDetector = detector;
		fDefaultToken = defaultToken;
	}

	public void addWord(String word, IToken token) {

		if (word != null)
			word = word.toLowerCase();
		fWords.put(word, token);
	}

	public void setColumnConstraint(int column) {
		if (column < 0)
			column = UNDEFINED;
		fColumn = column;
	}

	public IToken evaluate(ICharacterScanner scanner) {
		int c = scanner.read();
		if (fDetector.isWordStart((char) c)) {
			if (fColumn == UNDEFINED || (fColumn == scanner.getColumn() - 1)) {

				fBuffer.setLength(0);
				do {
					fBuffer.append((char) c);
					c = scanner.read();
				} while (c != ICharacterScanner.EOF
						&& fDetector.isWordPart((char) c));
				scanner.unread();

				String tokenName = fBuffer.substring(0, fBuffer.length()).toLowerCase();

				IToken token = (IToken) fWords.get(tokenName);
				if (token != null) {

					return token;
				}

				if (fDefaultToken.isUndefined())
					unreadBuffer(scanner);

				return fDefaultToken;
			}
		}

		scanner.unread();
		return Token.UNDEFINED;
	}

	public void unreadBuffer(ICharacterScanner scanner) {
		for (int i = fBuffer.length() - 1; i >= 0; i--)
			scanner.unread();
	}

	public int getMapSize() {
		return this.fWords.size();
	}

}