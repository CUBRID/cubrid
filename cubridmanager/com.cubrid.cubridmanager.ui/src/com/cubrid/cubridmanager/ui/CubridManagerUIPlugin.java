/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: -
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. - Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. - Neither the name of the <ORGANIZATION> nor the names
 * of its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 */
package com.cubrid.cubridmanager.ui;

import org.eclipse.core.runtime.Platform;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.swt.graphics.Image;
import org.eclipse.ui.plugin.AbstractUIPlugin;
import org.osgi.framework.BundleContext;

import com.cubrid.cubridmanager.ui.common.navigator.NodeAdapterFactory;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

/**
 * The activator class controls the plug-in life cycle
 */
public class CubridManagerUIPlugin extends
		AbstractUIPlugin {

	// The plug-in ID
	public static final String PLUGIN_ID = "com.cubrid.cubridmanager.ui";

	// The shared instance
	private static CubridManagerUIPlugin plugin;

	/**
	 * The constructor
	 */
	public CubridManagerUIPlugin() {
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see org.eclipse.ui.plugin.AbstractUIPlugin#start(org.osgi.framework.BundleContext )
	 */
	public void start(BundleContext context) throws Exception {
		super.start(context);
		plugin = this;
		Platform.getAdapterManager().registerAdapters(new NodeAdapterFactory(),
				ICubridNode.class);
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see org.eclipse.ui.plugin.AbstractUIPlugin#stop(org.osgi.framework.BundleContext )
	 */
	public void stop(BundleContext context) throws Exception {
		plugin = null;
		super.stop(context);
	}

	/**
	 * Returns the shared instance
	 * 
	 * @return the shared instance
	 */
	public static CubridManagerUIPlugin getDefault() {
		return plugin;
	}

	/**
	 * Returns an image descriptor for the image file at the given plug-in
	 * relative path.
	 * 
	 * @param path the path
	 * @return the image descriptor
	 */
	public static ImageDescriptor getImageDescriptor(String path) {
		ImageDescriptor imageDesc = getDefault().getImageRegistry().getDescriptor(
				path);
		if (imageDesc == null) {
			imageDesc = AbstractUIPlugin.imageDescriptorFromPlugin(PLUGIN_ID,
					path);
			CubridManagerUIPlugin.getDefault().getImageRegistry().put(path,
					imageDesc);
		}
		return imageDesc;
	}

	/**
	 * Returns an image for the image file at the given plug-in relative path.
	 * 
	 * @param path the path
	 * @return the image
	 */
	public static Image getImage(String path) {
		Image image = getDefault().getImageRegistry().get(path);
		if (image == null || image.isDisposed()) {
			ImageDescriptor imageDesc = AbstractUIPlugin.imageDescriptorFromPlugin(
					PLUGIN_ID, path);
			CubridManagerUIPlugin.getDefault().getImageRegistry().put(path,
					imageDesc);
			return CubridManagerUIPlugin.getDefault().getImageRegistry().get(
					path);
		}
		return image;
	}

	/**
	 * Get value by id from plugin preference
	 * 
	 * @param id
	 * @return
	 */
	public static String getPreference(String id) {
		return getDefault().getPluginPreferences().getString(id);
	}

	/**
	 * Save value to plugin preference
	 * 
	 * @param id
	 * @param value
	 */
	public static void setPreference(String id, String value) {
		getDefault().getPluginPreferences().setValue(id, value);
	}
}
