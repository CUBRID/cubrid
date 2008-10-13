package cubridmanager.cubrid.action;

import java.util.ArrayList;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.cubrid.dialog.FILEDOWN_PROGRESSDialog;

public class BackupDownThread extends Thread {
	public ArrayList sfiles = new ArrayList();
	public ArrayList dfiles = new ArrayList();
	public boolean compress = false;

	public BackupDownThread() {
		sfiles.clear();
		dfiles.clear();
		compress = false;
	}

	public void run() {
		Shell sh = new Shell();
		FILEDOWN_PROGRESSDialog dlg = new FILEDOWN_PROGRESSDialog(sh);
		dlg.sfiles = this.sfiles;
		dlg.dfiles = this.dfiles;
		dlg.compress = this.compress;
		dlg.doModal();
	}
}
