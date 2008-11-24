/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

package cubridmanager.dialog;

/*******************************************************************************
 * Copyright (c) 2000, 2005 IBM Corporation and others.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *     IBM Corporation - initial API and implementation
 *******************************************************************************/

import java.util.ArrayList;
import java.util.LinkedList;

import org.eclipse.core.runtime.IBundleGroup;
import org.eclipse.core.runtime.IBundleGroupProvider;
import org.eclipse.core.runtime.IProduct;
import org.eclipse.core.runtime.Platform;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.resource.JFaceColors;
import org.eclipse.osgi.util.NLS;
import org.eclipse.swt.SWT;
import org.eclipse.swt.accessibility.AccessibleAdapter;
import org.eclipse.swt.accessibility.AccessibleEvent;
import org.eclipse.swt.custom.BusyIndicator;
import org.eclipse.swt.custom.StyledText;
import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.graphics.Cursor;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.RowLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.internal.IWorkbenchHelpContextIds;
import org.eclipse.ui.internal.ProductProperties;
import org.eclipse.ui.internal.WorkbenchMessages;
import org.eclipse.ui.internal.about.AboutBundleGroupData;
import org.eclipse.ui.internal.about.AboutFeaturesButtonManager;
import org.eclipse.ui.internal.dialogs.AboutFeaturesDialog;
import org.eclipse.ui.internal.dialogs.AboutPluginsDialog;
import org.eclipse.ui.internal.dialogs.AboutSystemDialog;
import org.eclipse.ui.internal.dialogs.ProductInfoDialog;

import cubridmanager.Messages;
import cubridmanager.Version;

/**
 * Displays information about the product.
 */
public class AboutDialog extends ProductInfoDialog {
	private final static int MAX_IMAGE_WIDTH_FOR_TEXT = 250;
	private final static int FEATURES_ID = IDialogConstants.CLIENT_ID + 1;
	private final static int PLUGINS_ID = IDialogConstants.CLIENT_ID + 2;
	private final static int INFO_ID = IDialogConstants.CLIENT_ID + 3;
	private String productName;
	private IProduct product;
	private AboutBundleGroupData[] bundleGroupInfos;
	private ArrayList images = new ArrayList();
	private AboutFeaturesButtonManager buttonManager = new AboutFeaturesButtonManager();

	// TODO should the styled text be disposed? if not then it likely
	// doesn't need to be a member
	private StyledText text;

	/**
	 * Create an instance of the AboutDialog for the given window.
	 * 
	 * @param parentShell
	 *            The parent of the dialog.
	 */
	public AboutDialog(Shell parentShell) {
		super(parentShell);

		product = Platform.getProduct();
		if (product != null)
			productName = product.getName();
		if (productName == null)
			productName = WorkbenchMessages.AboutDialog_defaultProductName;

		// create a descriptive object for each BundleGroup
		IBundleGroupProvider[] providers = Platform.getBundleGroupProviders();
		LinkedList groups = new LinkedList();
		if (providers != null)
			for (int i = 0; i < providers.length; ++i) {
				IBundleGroup[] bundleGroups = providers[i].getBundleGroups();
				for (int j = 0; j < bundleGroups.length; ++j)
					groups.add(new AboutBundleGroupData(bundleGroups[j]));
			}
		bundleGroupInfos = (AboutBundleGroupData[]) groups
				.toArray(new AboutBundleGroupData[0]);
	}

	/*
	 * (non-Javadoc) Method declared on Dialog.
	 */
	protected void buttonPressed(int buttonId) {
		switch (buttonId) {
		case FEATURES_ID:
			new AboutFeaturesDialog(getShell(), productName, bundleGroupInfos)
					.open();
			break;
		case PLUGINS_ID:
			new AboutPluginsDialog(getShell(), productName).open();
			break;
		case INFO_ID:
			BusyIndicator.showWhile(null, new Runnable() {
				public void run() {
					new AboutSystemDialog(getShell()).open();
				}
			});
			break;
		default:
			super.buttonPressed(buttonId);
			break;
		}
	}

	public boolean close() {
		// dispose all images
		for (int i = 0; i < images.size(); ++i) {
			Image image = (Image) images.get(i);
			image.dispose();
		}

		return super.close();
	}

	/*
	 * (non-Javadoc) Method declared on Window.
	 */
	protected void configureShell(Shell newShell) {
		super.configureShell(newShell);
		newShell.setText(Messages.getString("TOOL.ABOUT"));
		PlatformUI.getWorkbench().getHelpSystem().setHelp(newShell,
				IWorkbenchHelpContextIds.ABOUT_DIALOG);
	}

	/**
	 * Add buttons to the dialog's button bar.
	 * 
	 * Subclasses should override.
	 * 
	 * @param parent
	 *            the button bar composite
	 */
	protected void createButtonsForButtonBar(Composite parent) {
		parent.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));

		// bug 64232: the feature details button should only be created if there
		// are features to show
		/*
		 * except buttons
		 * if (bundleGroupInfos != null &&
		 * bundleGroupInfos.length > 0) createButton(parent, FEATURES_ID,
		 * WorkbenchMessages.AboutDialog_featureInfo, false);
		 * 
		 * createButton(parent, PLUGINS_ID,
		 * WorkbenchMessages.AboutDialog_pluginInfo, false);
		 * createButton(parent, INFO_ID,
		 * WorkbenchMessages.AboutDialog_systemInfo, false);
		 */

		Label l = new Label(parent, SWT.NONE);
		l.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
		GridLayout layout = (GridLayout) parent.getLayout();
		layout.numColumns++;
		layout.makeColumnsEqualWidth = false;

		Button b = createButton(parent, IDialogConstants.OK_ID,
				IDialogConstants.OK_LABEL, true);
		b.setFocus();
	}

	/**
	 * Creates and returns the contents of the upper part of the dialog (above
	 * the button bar).
	 * 
	 * Subclasses should overide.
	 * 
	 * @param parent
	 *            the parent composite to contain the dialog area
	 * @return the dialog area control
	 */
	protected Control createDialogArea(Composite parent) {
		final Cursor hand = new Cursor(parent.getDisplay(), SWT.CURSOR_HAND);
		final Cursor busy = new Cursor(parent.getDisplay(), SWT.CURSOR_WAIT);
		setHandCursor(hand);
		setBusyCursor(busy);
		getShell().addDisposeListener(new DisposeListener() {
			public void widgetDisposed(DisposeEvent e) {
				setHandCursor(null);
				hand.dispose();
				setBusyCursor(null);
				busy.dispose();
			}
		});

		// brand the about box if there is product info
		Image aboutImage = null;
		if (product != null) {
			ImageDescriptor imageDescriptor = ProductProperties
					.getAboutImage(product);
			if (imageDescriptor != null)
				aboutImage = imageDescriptor.createImage();

			// if the about image is small enough, then show the text
			if (aboutImage == null
					|| aboutImage.getBounds().width <= MAX_IMAGE_WIDTH_FOR_TEXT) {
				String aboutText = ProductProperties.getAboutText(product);
				if (aboutText != null)
					setItem(scan(aboutText));
			}

			if (aboutImage != null)
				images.add(aboutImage);
		}

		// create a composite which is the parent of the top area and the bottom
		// button bar, this allows there to be a second child of this composite
		// with
		// a banner background on top but not have on the bottom
		Composite workArea = new Composite(parent, SWT.NONE);
		GridLayout workLayout = new GridLayout();
		workLayout.marginHeight = 0;
		workLayout.marginWidth = 0;
		workLayout.verticalSpacing = 0;
		workLayout.horizontalSpacing = 0;
		workArea.setLayout(workLayout);
		workArea.setLayoutData(new GridData(GridData.FILL_BOTH));

		// page group
		// Color background =
		// JFaceColors.getBannerBackground(parent.getDisplay());

		Color foreground = JFaceColors.getBannerForeground(parent.getDisplay());
		Composite top = (Composite) super.createDialogArea(workArea);
		Color background = top.getBackground();
		// override any layout inherited from createDialogArea
		GridLayout layout = new GridLayout();
		layout.marginHeight = 0;
		layout.marginWidth = 0;
		layout.verticalSpacing = 0;
		layout.horizontalSpacing = 0;
		top.setLayout(layout);
		top.setLayoutData(new GridData(GridData.FILL_BOTH));
		top.setBackground(background);
		top.setForeground(foreground);

		// the image & text
		Composite topContainer = new Composite(top, SWT.NONE);
		topContainer.setBackground(background);
		topContainer.setForeground(foreground);

		layout = new GridLayout();
		layout.numColumns = (aboutImage == null || getItem() == null ? 1 : 2);
		layout.marginWidth = 0;
		layout.marginHeight = 0;
		layout.verticalSpacing = 0;
		layout.horizontalSpacing = 0;
		topContainer.setLayout(layout);
		GridData data = new GridData();
		data.horizontalAlignment = GridData.FILL;
		data.grabExcessHorizontalSpace = true;
		topContainer.setLayoutData(data);

		// image on left side of dialog
		if (aboutImage != null) {
			Label imageLabel = new Label(topContainer, SWT.NONE);
			imageLabel.setBackground(background);
			imageLabel.setForeground(foreground);

			data = new GridData();
			data.horizontalAlignment = GridData.FILL;
			data.verticalAlignment = GridData.BEGINNING;
			data.grabExcessHorizontalSpace = false;
			imageLabel.setLayoutData(data);
			/* imageLabel.setImage(aboutImage); */
		}

		if (getItem() != null) {
			// there is no margins around the image, so insert an extra
			// composite
			// here to provide some margins for the text.
			Composite textContainer = new Composite(topContainer, SWT.NONE);
			textContainer.setBackground(background);
			textContainer.setForeground(foreground);

			layout = new GridLayout();
			layout.numColumns = 1;
			textContainer.setLayout(layout);
			data = new GridData();
			data.horizontalAlignment = GridData.FILL;
			data.verticalAlignment = GridData.BEGINNING;
			data.grabExcessHorizontalSpace = true;
			textContainer.setLayoutData(data);

			// text on the right
			text = new StyledText(textContainer, SWT.MULTI | SWT.READ_ONLY);
			text.setCaret(null);
			text.setFont(parent.getFont());
			data = new GridData();
			data.horizontalAlignment = GridData.FILL;
			data.verticalAlignment = GridData.BEGINNING;
			data.grabExcessHorizontalSpace = true;
			text.setText(getItem().getText());
			text.setText(getAboutText());
			text.setLayoutData(data);
			text.setCursor(null);
			text.setBackground(background);
			text.setForeground(foreground);

			setLinkRanges(text, getItem().getLinkRanges());
			addListeners(text);
		}

		// horizontal bar
		Label bar = new Label(workArea, SWT.HORIZONTAL | SWT.SEPARATOR);
		data = new GridData();
		data.horizontalAlignment = GridData.FILL;
		bar.setLayoutData(data);

		/*
		 * add image buttons for bundle groups that have them
		 * Composite bottom = (Composite) super.createDialogArea(workArea);
		 * override any layout inherited from createDialogArea layout = new
		 * GridLayout(); bottom.setLayout(layout); bottom.setLayoutData(new
		 * GridData(GridData.FILL_BOTH));
		 * 
		 * createFeatureImageButtonRow(bottom);
		 * spacer bar = new Label(bottom, SWT.NONE); data = new GridData();
		 * data.horizontalAlignment = GridData.FILL; bar.setLayoutData(data);
		 */

		return workArea;
	}

	private void createFeatureImageButtonRow(Composite parent) {
		Composite featureContainer = new Composite(parent, SWT.NONE);
		RowLayout rowLayout = new RowLayout();
		rowLayout.wrap = true;
		featureContainer.setLayout(rowLayout);
		GridData data = new GridData();
		data.horizontalAlignment = GridData.FILL;
		featureContainer.setLayoutData(data);

		for (int i = 0; i < bundleGroupInfos.length; i++)
			createFeatureButton(featureContainer, bundleGroupInfos[i]);
	}

	private Button createFeatureButton(Composite parent,
			final AboutBundleGroupData info) {
		if (!buttonManager.add(info))
			return null;

		ImageDescriptor desc = info.getFeatureImage();
		Image featureImage = null;

		Button button = new Button(parent, SWT.FLAT | SWT.PUSH);
		button.setData(info);
		featureImage = desc.createImage();
		images.add(featureImage);
		button.setImage(featureImage);
		button.setToolTipText(info.getProviderName());

		button.getAccessible().addAccessibleListener(new AccessibleAdapter() {
			/*
			 * (non-Javadoc)
			 * 
			 * @see org.eclipse.swt.accessibility.AccessibleAdapter#getName(org.eclipse.swt.accessibility.AccessibleEvent)
			 */
			public void getName(AccessibleEvent e) {
				e.result = info.getProviderName();
			}
		});
		button.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent event) {
				AboutBundleGroupData[] groupInfos = buttonManager
						.getRelatedInfos(info);
				AboutBundleGroupData selection = (AboutBundleGroupData) event.widget
						.getData();

				AboutFeaturesDialog d = new AboutFeaturesDialog(getShell(),
						productName, groupInfos);
				d.setInitialSelection(selection);
				d.open();
			}
		});

		return button;
	}

	private String getAboutText() {
		return Messages.getString("MSG.ABOUT") + " " + Version.getString("RELEASE.STRING")
		+ "\n\n" + Messages.getString("MSG.COPYRIGHT");
	}
}
