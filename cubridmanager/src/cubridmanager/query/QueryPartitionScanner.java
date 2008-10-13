package cubridmanager.query;

import org.eclipse.jface.text.rules.EndOfLineRule;
import org.eclipse.jface.text.rules.IPredicateRule;
import org.eclipse.jface.text.rules.IToken;
import org.eclipse.jface.text.rules.MultiLineRule;
import org.eclipse.jface.text.rules.RuleBasedPartitionScanner;
import org.eclipse.jface.text.rules.Token;

/**
 * This class scans a document and partitions it
 */
public class QueryPartitionScanner extends RuleBasedPartitionScanner {
	// Create a partition for comments, and leave the rest for code
	public static final String COMMENT = "comment";

	public static final String[] TYPES = { COMMENT };

	/**
	 * PerlPartitionScanner constructor
	 */
	public QueryPartitionScanner() {
		super();

		// Create the token for comment partitions
		IToken comment = new Token(COMMENT);

		// Set the rule--anything from # to the end of the line is a comment
		setPredicateRules(new IPredicateRule[] {
				new EndOfLineRule("//", comment),
				new EndOfLineRule("--", comment),
				new MultiLineRule("/*", "*/", comment) });
	}
}
