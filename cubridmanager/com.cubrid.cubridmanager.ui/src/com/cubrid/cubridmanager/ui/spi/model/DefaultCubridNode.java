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
package com.cubrid.cubridmanager.ui.spi.model;

import java.text.DateFormat;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;

import org.apache.log4j.Logger;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.Platform;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.ui.IEditorInput;
import org.eclipse.ui.IPersistableElement;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;

/**
 * 
 * This class implment the ICubridNode interface defaultly.it can construct a
 * simple tree structrue.
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class DefaultCubridNode implements
		ICubridNode {
	private static final Logger logger = LogUtil.getLogger(DefaultCubridNode.class);
	protected List<ICubridNode> childList = null;
	private String id = "";
	private String editorId = null;
	private String viewId = null;
	private String label = "";
	private ICubridNode parent = null;
	private boolean isRoot = false;
	private String iconPath = "";
	private ICubridNodeLoader loader = null;
	private CubridNodeType type = null;
	private CubridServer server = null;
	private boolean isContainer = false;
	private Object modelObj = null;

	/**
	 * The constructor
	 * 
	 * @param id
	 * @param label
	 * @param iconPath
	 */
	public DefaultCubridNode(String id, String label, String iconPath) {
		this.id = id;
		this.label = label;
		this.iconPath = iconPath;
		isRoot = false;
		childList = new ArrayList<ICubridNode>();
	}

	/**
	 * @see ICubridNode#isContainer()
	 */
	public boolean isContainer() {
		return isContainer;
	}

	/**
	 * @see ICubridNode#setContainer(boolean)
	 */
	public void setContainer(boolean isContainer) {
		this.isContainer = isContainer;
	}

	/**
	 * @see ICubridNode#getChild(String)
	 */
	public ICubridNode getChild(String id) {
		if (childList != null) {
			for (ICubridNode node : childList) {
				if (node != null && node.getId().equals(id)) {
					return node;
				}
			}
		}
		return null;
	}

	/**
	 * @see ICubridNode#getChildInAll(String)
	 */
	public ICubridNode getChildInAll(String id) {
		ICubridNode childNode = getChild(id);
		if (childNode != null) {
			return childNode;
		} else {
			for (ICubridNode node : childList) {
				childNode = node.getChild(id);
				if (childNode != null) {
					return childNode;
				}
			}
		}
		return null;
	}

	/**
	 * @see ICubridNode#getChildren()
	 */
	public List<ICubridNode> getChildren() {
		return childList;
	}

	/**
	 * @see ICubridNode#getChildren(IProgressMonitor)
	 */
	public ICubridNode[] getChildren(IProgressMonitor monitor) {
		if (loader != null && !loader.isLoaded())
			loader.load(this, monitor);
		if (childList.size() > 0) {
			ICubridNode[] nodeArr = new ICubridNode[childList.size()];
			return childList.toArray(nodeArr);
		}
		return new ICubridNode[] {};
	}

	/**
	 * @see ICubridNode#addChild(ICubridNode)
	 */
	public void addChild(ICubridNode obj) {
		if (obj != null && !isContained(obj)) {
			obj.setParent(this);
			obj.setServer(this.getServer());
			childList.add(obj);
		}
	}

	/**
	 * @see ICubridNode#removeChild(ICubridNode)
	 */
	public void removeChild(ICubridNode obj) {
		if (obj != null) {
			childList.remove(obj);
		}
	}

	/**
	 * @see ICubridNode#removeAllChild()
	 */
	public void removeAllChild() {
		childList.clear();
	}

	/**
	 * @see ICubridNode#getParent()
	 */
	public ICubridNode getParent() {
		return parent;
	}

	/**
	 * @see ICubridNode#setParent(ICubridNode)
	 */
	public void setParent(ICubridNode obj) {
		parent = obj;
	}

	/**
	 * @see ICubridNode#isContained(ICubridNode)
	 */
	public boolean isContained(ICubridNode obj) {
		if (obj == null) {
			return false;
		}
		for (ICubridNode node : childList) {
			if (node != null && node.getId().equals(obj.getId())) {
				return true;
			}
		}
		return false;
	}

	/**
	 * @see ICubridNode#isContainedInAll(ICubridNode)
	 */
	public boolean isContainedInAll(ICubridNode obj) {
		if (childList.contains(obj)) {
			return true;
		} else {
			for (ICubridNode node : childList) {
				if (node.isContainedInAll(obj)) {
					return true;
				}
			}
		}
		return false;
	}

	/**
	 * @see ICubridNode#isRoot()
	 */
	public boolean isRoot() {
		return isRoot;
	}

	/**
	 * @see ICubridNode#setRoot(boolean)
	 */
	public void setRoot(boolean isRoot) {
		this.isRoot = isRoot;
	}

	/**
	 * @see ICubridNode#getIconPath()
	 */
	public String getIconPath() {
		return iconPath;
	}

	/**
	 * @see ICubridNode#setIconPath(String)
	 */
	public void setIconPath(String iconPath) {
		this.iconPath = iconPath;
	}

	/**
	 * @see ICubridNode#getLabel()
	 */
	public String getLabel() {
		return label;
	}

	/**
	 * @see ICubridNode#setLabel(String)
	 */
	public void setLabel(String label) {
		this.label = label;
	}

	/**
	 * @see ICubridNode#getId()
	 */
	public String getId() {
		return id;
	}

	/**
	 * @see ICubridNode#setId(String)
	 */
	public void setId(String id) {
		this.id = id;
	}

	/**
	 * @see ICubridNode#getLoader()
	 */
	public ICubridNodeLoader getLoader() {
		return this.loader;
	}

	/**
	 * @see ICubridNode#setLoader(ICubridNodeLoader)
	 */
	public void setLoader(ICubridNodeLoader loader) {
		this.loader = loader;
	}

	/**
	 * @see ICubridNode#getEditorId()
	 */
	public String getEditorId() {
		return editorId;
	}

	/**
	 * @see ICubridNode#setEditorId(String)
	 */
	public void setEditorId(String editorId) {
		this.editorId = editorId;

	}

	/**
	 * @see ICubridNode#getViewId()
	 */
	public String getViewId() {
		return viewId;
	}

	/**
	 * @see ICubridNode#setViewId(String)
	 */
	public void setViewId(String viewId) {
		this.viewId = viewId;

	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see org.eclipse.core.runtime.IAdaptable#getAdapter(java.lang.Class)
	 */
	@SuppressWarnings("unchecked")
	public Object getAdapter(Class adapter) {
		if (modelObj != null && modelObj.getClass() == adapter) {
			return modelObj;
		}
		return Platform.getAdapterManager().getAdapter(this, adapter);
	}

	/**
	 * @see ICubridNode#setType(CubridNodeType)
	 */
	public void setType(CubridNodeType type) {
		this.type = type;
	}

	/**
	 * @see ICubridNode#getType()
	 */
	public CubridNodeType getType() {
		return this.type;
	}

	/**
	 * @see ICubridNode#getServer()
	 */
	public CubridServer getServer() {
		return server;
	}

	/**
	 * @see ICubridNode#setServer(CubridServer)
	 */
	public void setServer(CubridServer obj) {
		server = obj;
	}

	/**
	 * @see IEditorInput#exists()
	 */
	public boolean exists() {
		return false;
	}

	/**
	 * @see IEditorInput#getPersistable()
	 */
	public IPersistableElement getPersistable() {
		return null;
	}

	/**
	 * @see IEditorInput#getName()
	 */
	public String getName() {
		return getLabel();
	}

	/**
	 * @see IEditorInput#getToolTipText()
	 */
	public String getToolTipText() {
		String tipText = getLabel();
		ICubridNode parent = getParent();
		while (parent != null) {
			tipText = parent.getLabel() + "/" + tipText;
			parent = parent.getParent();
		}
		return tipText;
	}

	/**
	 * @see IEditorInput#getImageDescriptor()
	 */
	public ImageDescriptor getImageDescriptor() {
		if (getIconPath() != null && getIconPath().trim().length() > 0) {
			return CubridManagerUIPlugin.getImageDescriptor(getIconPath());
		}
		return null;
	}

	/**
	 * @see ICubridNode#setModelObj(Object)
	 */
	public void setModelObj(Object obj) {
		modelObj = obj;
	}

	public int compareTo(ICubridNode o) {
		if (getType() == CubridNodeType.LOGS_SERVER_DATABASE_LOG) {
			String[] dateArr1 = getLabel().split("\\.")[0].split("_");
			String[] dateArr2 = o.getLabel().split("\\.")[0].split("_");
			DateFormat dateFormat = new SimpleDateFormat("yyyyMMdd hhmm");
			if (dateArr1.length > 2 && dateArr2.length > 2) {
				String str1 = dateArr1[1] + " " + dateArr1[2];
				String str2 = dateArr2[1] + " " + dateArr2[2];
				try {
					Date date1 = dateFormat.parse(str1);
					Date date2 = dateFormat.parse(str2);
					return date1.compareTo(date2);
				} catch (ParseException e) {
					logger.error(e);
				}
				return 0;
			}
		}
		String str = o.getLabel();
		return label.compareTo(str);
	}
}
