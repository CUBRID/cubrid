package cubridmanager.query;

import java.util.Iterator;
import java.util.LinkedList;
import java.util.List;

public class WordTracker {
	private List wordBuffer;

	public WordTracker(String[] keyWords) {
		wordBuffer = new LinkedList();
		for (int i = 0; i < keyWords.length; i++) {
			wordBuffer.add(keyWords[i]);
		}
	}

	public int getWordCount() {
		return wordBuffer.size();
	}

	public List suggest(String word) {
		List suggestions = new LinkedList();
		for (Iterator i = wordBuffer.iterator(); i.hasNext();) {
			String currWord = (String) i.next();
			if (currWord.startsWith(word)) {
				suggestions.add(currWord);
			}
		}
		return suggestions;
	}
}
