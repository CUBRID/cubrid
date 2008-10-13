package cubridmanager.diag.action;

import org.eclipse.jface.action.Action;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.ui.ISelectionListener;
import org.eclipse.ui.IWorkbenchPart;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.actions.ActionFactory;

public class DiagRemoveDiagReportAction extends Action implements
		ISelectionListener, ActionFactory.IWorkbenchAction {
	private final IWorkbenchWindow window;
	public final static String ID = "cubridmanager.DiagRemoveDiagReport";
	public String logFileName = "";

	public DiagRemoveDiagReportAction(String text, String logName,
			IWorkbenchWindow window) {
		super(text);
		this.window = window;
		setId(ID);
		setActionDefinitionId(ID);
	}

	public void selectionChanged(IWorkbenchPart part, ISelection incomming) {
		setEnabled(false);
	}

	public void dispose() {
		window.getSelectionService().removeSelectionListener(this);
	}

	public void run() {

	}
}
