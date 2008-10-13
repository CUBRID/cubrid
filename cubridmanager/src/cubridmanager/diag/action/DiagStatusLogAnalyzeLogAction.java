package cubridmanager.diag.action;

import org.eclipse.jface.action.Action;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.ui.ISelectionListener;
import org.eclipse.ui.IWorkbenchPart;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.actions.ActionFactory;

import cubridmanager.diag.dialog.DiagAnalyzeLogDialog;

public class DiagStatusLogAnalyzeLogAction extends Action implements
		ISelectionListener, ActionFactory.IWorkbenchAction {
	private final IWorkbenchWindow window;
	public final static String ID = "cubridmanager.DiagStatusLogAnalyzeLog";
	public String logFileName = "";

	public DiagStatusLogAnalyzeLogAction(String text, String logName,
			IWorkbenchWindow window) {
		super(text);
		this.window = window;
		setId(ID);
		setActionDefinitionId(ID);

		logFileName = logName;
	}

	public void selectionChanged(IWorkbenchPart part, ISelection incomming) {
		setEnabled(false);
	}

	public void dispose() {
		window.getSelectionService().removeSelectionListener(this);
	}

	public void run() {
		DiagAnalyzeLogDialog dialog = new DiagAnalyzeLogDialog(window
				.getShell());
		dialog.doModal();
	}
}
