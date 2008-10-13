package cubridmanager;

import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.Listener;

import cubridmanager.cubrid.ParameterItem;

public class VerifyDigitListener implements Listener {
	int type = ParameterItem.tPositiveNumber;

	public VerifyDigitListener() {
		type = ParameterItem.tPositiveNumber;
	}

	public VerifyDigitListener(int type) {
		this.type = type;
	}

	public void handleEvent(Event e) {
		String string = e.text;
		char[] chars = new char[string.length()];
		string.getChars(0, chars.length, chars, 0);
		switch (type) {
		case ParameterItem.tBoolean:
			for (int i = 0; i < chars.length; i++) {
				if (!(chars[i] == '0' || chars[i] == '1')) {
					e.doit = false;
					return;
				}
			}
		case ParameterItem.tPositiveNumber:
			for (int i = 0; i < chars.length; i++) {
				if (!('0' <= chars[i] && chars[i] <= '9')) {
					e.doit = false;
					return;
				}
			}
		case ParameterItem.tInteger:
			for (int i = 0; i < chars.length; i++) {
				if (!(('0' <= chars[i] && chars[i] <= '9') || chars[i] == '-')) {
					e.doit = false;
					return;
				}
			}
		case ParameterItem.tFloat:
			for (int i = 0; i < chars.length; i++) {
				if (!(('0' <= chars[i] && chars[i] <= '9') || chars[i] == '.' || chars[i] == '-')) {
					e.doit = false;
					return;
				}
			}

		// Normal text
		case ParameterItem.tIsolation:
		case ParameterItem.tString:
		case ParameterItem.tUnknown:
		default:
			;
		}
	}
}
