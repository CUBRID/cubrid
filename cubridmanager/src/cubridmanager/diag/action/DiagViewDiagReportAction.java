package cubridmanager.diag.action;

import org.eclipse.jface.action.Action;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.ISelectionListener;
import org.eclipse.ui.IWorkbenchPart;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.actions.ActionFactory;

import cubridmanager.diag.dialog.DiagDiagReportDialog;

public class DiagViewDiagReportAction extends Action implements
		ISelectionListener, ActionFactory.IWorkbenchAction {
	private final IWorkbenchWindow window;
	public final static String ID = "cubridmanager.DiagViewDiagReport";

	public DiagViewDiagReportAction(String text, IWorkbenchWindow window) {
		super(text);
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
		DiagDiagReportDialog dialog = new DiagDiagReportDialog(shell);
		dialog.doModal();
	}
}
