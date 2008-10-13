package cubridmanager.query;

import org.eclipse.jface.text.TextAttribute;
import org.eclipse.jface.text.rules.IToken;
import org.eclipse.jface.text.rules.RuleBasedScanner;
import org.eclipse.jface.text.rules.Token;

import cubridmanager.query.view.QueryEditor;

/**
 * This class scans comment partitions
 */
public class CommentScanner extends RuleBasedScanner {
	/**
	 * CommentScanner constructor
	 */
	public CommentScanner() {
		// Get the color manager
		ColorManager colorManager = QueryEditor.getApp().getColorManager();

		// Create the tokens
		IToken other = new Token(new TextAttribute(colorManager
				.getColor(ColorManager.COMMENT)));

		// Use "other" for default
		setDefaultReturnToken(other);

		// This scanner has an easy job--we need no rules. Anything in a comment
		// partition should be scanned as a comment
	}
}
