package cubridmanager.query;

/*
 * Copyright (C) 2002-2004 Andrea Mazzolini
 * andreamazzolini@users.sourceforge.net
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

import java.util.HashMap;
import java.util.Map;

import org.eclipse.jface.text.rules.ICharacterScanner;
import org.eclipse.jface.text.rules.IRule;
import org.eclipse.jface.text.rules.IToken;
import org.eclipse.jface.text.rules.IWordDetector;
import org.eclipse.jface.text.rules.Token;
import org.eclipse.jface.util.Assert;

public class UnsignedWordRule implements IRule {

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
	public Map fWords = new HashMap();
	private StringBuffer fBuffer = new StringBuffer();
	private IToken fTableToken;
	private IToken fColumnToken;

	public UnsignedWordRule(IWordDetector detector) {
		this(detector, Token.UNDEFINED, null, null);

	}

	// public UnsignedWordRule(IWordDetector detector, IToken defaultToken) {
	public UnsignedWordRule(IWordDetector detector, IToken defaultToken,
			IToken tableToken, IToken columnToken) {
		Assert.isNotNull(detector);
		Assert.isNotNull(defaultToken);

		fDetector = detector;
		fDefaultToken = defaultToken;
		fTableToken = tableToken;
		fColumnToken = columnToken;
	}

	public void addWord(String word, IToken token) {
		Assert.isNotNull(word);
		Assert.isNotNull(token);
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

				String tokenName = fBuffer.substring(0, fBuffer.length())
						.toLowerCase();

				IToken token = (IToken) fWords.get(tokenName);
				if (token != null) {

					/*
					 * if ((token == fTableToken) && (dictionary != null)) {
					 * 
					 * ArrayList list = (ArrayList)
					 * dictionary.getByTableName(tokenName); if (list != null) {
					 * for (int j = 0; j < list.size(); j++) {
					 * 
					 * ArrayList ls = null; try { ls = (ArrayList)
					 * nd.getColumnNames(); } catch (Throwable e) {
					 * SQLExplorerPlugin.error("Error getting columns names",
					 * e); } if (ls != null) { TreeSet colTree = (TreeSet)
					 * dictionary.getColumnListByTableName(tokenName); if
					 * (colTree == null && j == 0) { colTree = new TreeSet();
					 * dictionary.putColumnsByTableName(tokenName, colTree); for
					 * (int i = 0; i < ls.size(); i++) { String lo = ((String)
					 * ls.get(i)); addWord(lo, fColumnToken); colTree.add(lo); } }
					 * else if (colTree != null && j > 0) { }
					 *  } } } }
					 */
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