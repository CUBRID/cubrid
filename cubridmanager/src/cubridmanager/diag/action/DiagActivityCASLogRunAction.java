package cubridmanager.diag.action;

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
import cubridmanager.diag.dialog.DiagCasRunnerConfigDialog;
import cubridmanager.diag.dialog.DiagCasRunnerResultDialog;

public class DiagActivityCASLogRunAction extends Action implements
		ISelectionListener, ActionFactory.IWorkbenchAction {
	public static String logFile = new String();
	private final IWorkbenchWindow window;
	public final static String ID = "cubridmanager.DiagActivityCASLogRun";

	public DiagActivityCASLogRunAction(String text, IWorkbenchWindow window) {
		super(text);
		this.window = window;
		setId(ID);
		setActionDefinitionId(ID);
	}

	public DiagActivityCASLogRunAction(String text, IWorkbenchWindow window,
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
		if (logFile.equals(""))
			return;
		DiagCasRunnerConfigDialog casrunnerconfigdialog = new DiagCasRunnerConfigDialog(
				shell);
		casrunnerconfigdialog.logfile = logFile;
		casrunnerconfigdialog.execwithFile = true;

		if (casrunnerconfigdialog.doModal() == Window.CANCEL)
			return;
		StringBuffer msg = new StringBuffer();

		msg.append("dbname:").append(casrunnerconfigdialog.dbname).append("\n");
		msg.append("brokername:").append(casrunnerconfigdialog.brokerName)
				.append("\n");
		msg.append("username:").append(casrunnerconfigdialog.userName).append(
				"\n");
		msg.append("passwd:").append(casrunnerconfigdialog.password).append(
				"\n");
		msg.append("num_thread:").append(casrunnerconfigdialog.numThread)
				.append("\n");
		msg.append("repeat_count:")
				.append(casrunnerconfigdialog.numRepeatCount).append("\n");
		msg.append("executelogfile:yes\n");
		msg.append("logfile:").append(logFile).append("\n");
		msg.append("show_queryresult:no\n");

		ClientSocket cs = new ClientSocket();
		if (cs.SendBackGround(shell, msg.toString(), "executecasrunner",
				"Executing cas runner") == false) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}

		/* Display result */
		DiagCasRunnerResultDialog resultDialog = new DiagCasRunnerResultDialog(
				shell);

		resultDialog
				.doModal(MainRegistry.tmpDiagExecuteCasRunnerResult.resultString);
	}
}
