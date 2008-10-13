package cubridmanager.query;

import java.util.Iterator;
import java.util.List;

import org.eclipse.jface.text.BadLocationException;
import org.eclipse.jface.text.IDocument;
import org.eclipse.jface.text.ITextViewer;
import org.eclipse.jface.text.contentassist.CompletionProposal;
import org.eclipse.jface.text.contentassist.ContextInformationValidator;
import org.eclipse.jface.text.contentassist.ICompletionProposal;
import org.eclipse.jface.text.contentassist.IContentAssistProcessor;
import org.eclipse.jface.text.contentassist.IContextInformation;
import org.eclipse.jface.text.contentassist.IContextInformationValidator;

import cubridmanager.CommonTool;

public class CubridKeyWordContentAssistProcessor implements
		IContentAssistProcessor {

	private String lastError = null;
	private IContextInformationValidator contextInfoValidator;
	private WordTracker wordTracker;

	public CubridKeyWordContentAssistProcessor(WordTracker tracker) {
		super();
		contextInfoValidator = new ContextInformationValidator(this);
		wordTracker = tracker;
	}

	public ICompletionProposal[] computeCompletionProposals(ITextViewer viewer,
			int offset) {
		IDocument document = viewer.getDocument();
		int currOffset = offset - 1;

		try {
			String currWord = "";
			char currChar;
			while (currOffset >= 0
					&& !Character.isWhitespace(currChar = document
							.getChar(currOffset))) {
				currWord = Character.toUpperCase(currChar) + currWord;
				currOffset--;
			}

			if (currWord.trim().equals(""))
				return null;

			List suggestions = wordTracker.suggest(currWord);
			ICompletionProposal[] proposals = null;
			if (suggestions.size() > 0) {
				proposals = buildProposals(suggestions, currWord, offset
						- currWord.length());
				lastError = null;
			}
			return proposals;
		} catch (BadLocationException e) {
			CommonTool.debugPrint(e);
			lastError = e.getMessage();
			return null;
		}
	}

	private ICompletionProposal[] buildProposals(List suggestions,
			String replacedWord, int offset) {
		ICompletionProposal[] proposals = new ICompletionProposal[suggestions
				.size()];
		int index = 0;
		for (Iterator i = suggestions.iterator(); i.hasNext();) {
			String currSuggestion = (String) i.next();
			proposals[index] = new CompletionProposal(currSuggestion, offset,
					replacedWord.length(), currSuggestion.length());
			index++;
		}
		return proposals;
	}

	public IContextInformation[] computeContextInformation(ITextViewer viewer,
			int offset) {
		lastError = "No Context Information available";
		return null;
	}

	public char[] getCompletionProposalAutoActivationCharacters() {
		// TODO Auto-generated method stub
		return null;
	}

	public char[] getContextInformationAutoActivationCharacters() {
		// TODO Auto-generated method stub
		return null;
	}

	public String getErrorMessage() {
		return lastError;
	}

	public IContextInformationValidator getContextInformationValidator() {
		return contextInfoValidator;
	}

}
