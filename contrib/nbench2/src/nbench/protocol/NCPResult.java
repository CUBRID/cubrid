package nbench.protocol;

import java.util.Iterator;
import java.util.LinkedList;
import java.util.Stack;

public class NCPResult {
	private Stack<String> actor_stack;
	private LinkedList<String> errors;
	private LinkedList<String> warnings;

	
	public NCPResult() {
		actor_stack = new Stack<String>();
		errors = new LinkedList<String>();
		warnings = new LinkedList<String>();
	}

	private String getSummaryString(LinkedList<String> list) {
		StringBuffer sb = new StringBuffer();
		Iterator<String> iter = list.iterator();
		while(iter.hasNext()) {
			sb.append(iter.next());
			sb.append("\n");
		}
		return sb.toString();
	}

	public String toString() {
		return getSummaryString(errors) + getSummaryString(warnings);
	}
	
	public String getErrorString() {
		return getSummaryString(errors);
	}
	
	public Iterator<String> getErrorIterator() {
		return errors.iterator();
	}

	public String getWarningString() {
		return getSummaryString(warnings);
	}
	
	public Iterator<String> getWarningIterator() {
		return warnings.iterator();
	}

	public NCPResult enter(String actor) {
		actor_stack.push(actor);
		return this;
	}

	public void leave() {
		actor_stack.pop();
	}

	private String getPrefix() {
		StringBuffer sb = new StringBuffer();
		for(int i = 0; i < actor_stack.size(); i++) {
			sb.append(actor_stack.get(i));
			sb.append(">");
		}
		return sb.toString();
	}
	
	public void setError(String message) {
		errors.add(getPrefix() + message.trim());
	}

	public void setError(Exception e) {
		setError(e.toString());
	}

	public void setWarning(String message) {
		warnings.add(getPrefix() + message.trim());
	}

	public void clearAll() {
		clearStack();
		clearMessage();
	}
	
	private void clearStack() {
		actor_stack.clear();
	}
	
	private void clearMessage() {
		errors.clear();
		warnings.clear();
	}

	public boolean hasError() {
		return errors.size() > 0;
	}

	public boolean hasInfo() {
		return warnings.size() > 0;
	}
}
