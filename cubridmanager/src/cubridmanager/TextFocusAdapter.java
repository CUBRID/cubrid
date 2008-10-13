package cubridmanager;

import org.eclipse.swt.events.FocusAdapter;
import org.eclipse.swt.widgets.Text;

public class TextFocusAdapter extends FocusAdapter {
	public void focusGained(org.eclipse.swt.events.FocusEvent e) {
		((Text) e.widget).selectAll();
	}
}
