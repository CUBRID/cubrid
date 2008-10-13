package cubridmanager.diag.action;

import java.util.ArrayList;

import org.eclipse.jface.action.Action;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.jface.window.Window;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.ISelectionListener;
import org.eclipse.ui.IWorkbenchPart;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.actions.ActionFactory;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.cas.CASItem;
import cubridmanager.cubrid.LogFileInfo;
import cubridmanager.diag.dialog.DiagActivityCASLogPathDialog;
import cubridmanager.diag.dialog.DiagCASLogTopConfigDialog;

public class DiagActivityAnalyzeCASLogAction extends Action implements
		ISelectionListener, ActionFactory.IWorkbenchAction {

	private final IWorkbenchWindow window;
	public static String logFile = new String("");
	public final static String ID = "cubridmanager.DiagActivityAnalyzeCASLog";

	public DiagActivityAnalyzeCASLogAction(String text, IWorkbenchWindow window) {
		super(text);
		this.window = window;
		setId(ID);
		setActionDefinitionId(ID);
	}

	public DiagActivityAnalyzeCASLogAction(String text,
			IWorkbenchWindow window, int style) {
		super(text, style);
		this.window = window;
		setId(ID);
		setActionDefinitionId(ID);
	}

	public void selectionChanged(IWorkbenchPart part, ISelection incoming) {
		setEnabled(false);
	}

	public void dispose() {
		window.getSelectionService().removeSelectionListener(this);
	}

	public void run() {
		Shell shell = new Shell();
		DiagCASLogTopConfigDialog configDialog = new DiagCASLogTopConfigDialog(
				shell);
		if (logFile.equals("")) {
			ArrayList casinfo = MainRegistry.CASinfo;
			for (int i = 0, n = casinfo.size(); i < n; i++) {
				ArrayList loginfo = ((CASItem) casinfo.get(i)).loginfo;
				for (int j = 0, m = loginfo.size(); j < m; j++) {
					if (((LogFileInfo) loginfo.get(j)).type.equals("script"))
						configDialog.targetStringList
								.add(((LogFileInfo) loginfo.get(j)).path);
				}
			}
		} else {
			configDialog.targetStringList.add(logFile);
		}

		if (configDialog.doModal() == Window.OK) {
			DiagActivityCASLogPathDialog dialog = new DiagActivityCASLogPathDialog(
					shell);

			String msg = new String();
			ClientSocket cs = new ClientSocket();

			msg = "open:logfilelist\n";
			for (int i = 0, n = configDialog.selectedStringList.size(); i < n; i++) {
				msg += "logfile:"
						+ (String) (configDialog.selectedStringList.get(i))
						+ "\n";
			}
			msg += "close:logfilelist\n";
			msg += "option_t:";
			if (configDialog.option_t)
				msg += "yes\n";
			else
				msg += "no\n";

			dialog.option_t = configDialog.option_t;

			cs.socketOwner = dialog;
			if (cs.SendBackGround(shell, msg, "analyzecaslog",
					"Analyzing cas log") == false) {
				CommonTool.ErrorBox(shell, cs.ErrorMsg);
				return;
			}

			dialog.filename = logFile;
			dialog.doModal();
		}
	}
}
