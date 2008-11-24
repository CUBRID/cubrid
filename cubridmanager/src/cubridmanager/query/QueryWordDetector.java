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

package cubridmanager.query;

import org.eclipse.jface.text.rules.IWordDetector;

/**
 * This class detects words in a perl file
 */
public class QueryWordDetector implements IWordDetector {
	/**
	 * Gets whether the specified character is the start of a word
	 * 
	 * @return boolean
	 */
	// public boolean isWordStart(char c) {
	// for (int i = 0, n = QuerySyntax.KEYWORDS.length; i < n; i++)
	// if (c == ((String) QuerySyntax.KEYWORDS[i]).charAt(0)) return true;
	// return false;
	// }
	public boolean isWordStart(char character) {
		return Character.isJavaIdentifierStart(character);
	}

	/**
	 * Gets whether the specified character is part of a word
	 * 
	 * @return boolean
	 */
	// public boolean isWordPart(char c) {
	// for (int i = 0, n = QuerySyntax.KEYWORDS.length; i < n; i++)
	// if (((String) QuerySyntax.KEYWORDS[i]).indexOf(c) != -1) return true;
	// return false;
	// }
	public boolean isWordPart(char character) {
		return Character.isJavaIdentifierPart(character);
	}

}
