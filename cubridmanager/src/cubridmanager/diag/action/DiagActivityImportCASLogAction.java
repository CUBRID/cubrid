package cubridmanager.diag.action;

import org.eclipse.jface.action.Action;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.ISelectionListener;
import org.eclipse.ui.IWorkbenchPart;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.actions.ActionFactory;

import cubridmanager.diag.dialog.DiagActivityImportCASLogDialog;

public class DiagActivityImportCASLogAction extends Action implements
		ISelectionListener, ActionFactory.IWorkbenchAction {

	private final IWorkbenchWindow window;
	public final static String ID = "cubridmanager.DiagActivityImportCASLog";

	public DiagActivityImportCASLogAction(String text, IWorkbenchWindow window) {
		super(text);
		this.window = window;
		setId(ID);
		setActionDefinitionId(ID);
	}

	public DiagActivityImportCASLogAction(String text, IWorkbenchWindow window,
			int style) {
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
		DiagActivityImportCASLogDialog dialog = new DiagActivityImportCASLogDialog(
				shell);
		dialog.doModal();
	}
}
