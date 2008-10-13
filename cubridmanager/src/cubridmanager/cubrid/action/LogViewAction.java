package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.CommonTool;
import cubridmanager.cubrid.LogFileInfo;
import cubridmanager.cubrid.dialog.LogViewDialog;
import java.util.ArrayList;

public class LogViewAction extends Action {
	public static ArrayList viewlist = null;
	public static String viewitem = null;

	public LogViewAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("LogViewAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (viewlist == null || viewitem == null)
			return;
		LogFileInfo firec = null;
		int i, n;
		for (i = 0, n = viewlist.size(); i < n; i++) {
			firec = (LogFileInfo) viewlist.get(i);
			if (firec.filename.equals(viewitem))
				break;
		}
		if (i >= n)
			return;
		Shell shell = new Shell();
		LogViewDialog dlg = new LogViewDialog(shell);
		if (!dlg.doModal(firec))
			CommonTool.ErrorBox("Cannot view log.");
	}
}
