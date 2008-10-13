package cubridmanager.cas.view;

import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.custom.ScrolledComposite;
import org.eclipse.swt.events.PaintEvent;
import org.eclipse.swt.events.PaintListener;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.layout.GridLayout;

import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.TreeObject;
import cubridmanager.WorkView;
import cubridmanager.cas.CASItem;

public class BrokerList extends ViewPart {
	ScrolledComposite sc = null;

	public static final String ID = "workview.BrokerList";	
	// TODO Needs to be whatever is mentioned in plugin.xml

	private static TreeObject lastsel = null;
	private GridLayout gridLayout;
	private Composite top = null;
	private int oldcol = 4;

	public BrokerList() {
		super();
		// TODO Auto-generated constructor stub
	}

	public void createPartControl(Composite parent) {
		sc = new ScrolledComposite(parent, SWT.H_SCROLL | SWT.V_SCROLL);
		FillLayout flayout = new FillLayout();
		sc.setLayout(flayout);
		gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		top = new Composite(sc, SWT.NONE);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		top.setLayout(gridLayout);
		sc.setContent(top);
		sc.setExpandHorizontal(true);
		sc.setExpandVertical(true);
		setcaslist();
	}

	public void setcaslist() {
		top.addPaintListener(new PaintListener() {
			public void paintControl(PaintEvent e) {
				Rectangle toprect = top.getBounds();
				int newcol = (toprect.width / 200) + 1;
				if (newcol != oldcol) {
					oldcol = newcol;
					gridLayout.numColumns = newcol;
					Control[] ctl = top.getChildren();
					int x = 0, y = 0, x2 = 0, y2 = 0;
					for (int i = 0, n = ctl.length; i < n; i++) {
						toprect = ctl[i].getBounds();
						if (i == 0) {
							x = toprect.x;
							y = toprect.y;
							x2 = toprect.width;
							y2 = toprect.height;
						} else {
							if (toprect.x < x)
								x = toprect.x;
							if (toprect.y < y)
								y = toprect.y;
							if ((toprect.x + toprect.width) > x2)
								x2 = (toprect.x + toprect.width);
							if ((toprect.y + toprect.height) > y2)
								y2 = (toprect.y + toprect.height);
						}
					}
					sc.setMinWidth(x2 + x + 50);
					sc.setMinHeight(y2 + y + 50);
				}
			}
		});

		CLabel cLabel = null;
		CASItem casrec;
		for (int i = 0, n = MainRegistry.CASinfo.size(); i < n; i++) {
			casrec = (CASItem) MainRegistry.CASinfo.get(i);
			if (casrec.status == MainConstants.STATUS_NONE)
				continue; // not exist DB
			cLabel = new CLabel(top, SWT.NONE);
			cLabel.setText(casrec.broker_name);
			cLabel.setBackground(Display.getCurrent().getSystemColor(
					SWT.COLOR_WHITE));
			cLabel.setImage(new Image(Display.getCurrent(),
					getClass().getResourceAsStream(
							"/cubridmanager/image/caslisticon.png")));
			cLabel.addMouseListener(new org.eclipse.swt.events.MouseAdapter() {
				public void mouseDown(org.eclipse.swt.events.MouseEvent e) {
					String lblname = ((CLabel) e.getSource()).getText();
					Control[] lbls = top.getChildren();
					for (int i = 0, n = lbls.length; i < n; i++) {
						String lblsname = ((CLabel) lbls[i]).getText();
						if (lblsname.equals(lblname))
							lbls[i].setBackground(Display.getCurrent()
									.getSystemColor(SWT.COLOR_GRAY));
						else
							lbls[i].setBackground(Display.getCurrent()
									.getSystemColor(SWT.COLOR_WHITE));
					}
					lastsel = CASView.SelectBroker(((CLabel) e.getSource())
							.getText());
				}

				public void mouseDoubleClick(org.eclipse.swt.events.MouseEvent e) {
					if (lastsel != null) {
						WorkView.SetView(lastsel.getViewID(),
								lastsel.getName(), null);
					}
				}
			});
			CASView.hookContextMenu(cLabel);
		}
	}

	public void setFocus() {
		// TODO Auto-generated method stub

	}

}
