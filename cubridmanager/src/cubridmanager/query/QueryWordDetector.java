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
