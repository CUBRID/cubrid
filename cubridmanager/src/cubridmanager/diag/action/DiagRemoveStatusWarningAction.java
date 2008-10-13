package cubridmanager.diag.action;

import org.eclipse.jface.action.Action;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.ui.ISelectionListener;
import org.eclipse.ui.IWorkbenchPart;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.actions.ActionFactory;

import cubridmanager.Application;

public class DiagRemoveStatusWarningAction extends Action implements
		ISelectionListener, ActionFactory.IWorkbenchAction {
	private final IWorkbenchWindow window;
	public final static String ID = "cubridmanager.DiagRemoveStatusWarning";
	public String warningName = "";

	public DiagRemoveStatusWarningAction(String text, String wName,
			IWorkbenchWindow window) {
		super(text);
		this.window = window;
		setId(ID);
		setActionDefinitionId(ID);

		warningName = wName;
	}

	public void selectionChanged(IWorkbenchPart part, ISelection incoming) {
		setEnabled(false);
	}

	public void dispose() {
		window.getSelectionService().removeSelectionListener(this);
	}

	public void run() {
		MessageDialog.openInformation(Application.mainwindow.getShell(),
				"start status warning", warningName + "will be Removed.");
	}
}
