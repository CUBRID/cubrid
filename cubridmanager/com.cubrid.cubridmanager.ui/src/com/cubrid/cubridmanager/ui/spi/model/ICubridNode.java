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

import java.util.List;

import org.eclipse.core.runtime.IAdaptable;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.ui.IEditorInput;

/**
 * 
 * This interface is designed in use of composite pattern.it can construct a
 * navigator tree,all tree node must implement this interface,it may be a
 * container node,also may be a leaf node
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public interface ICubridNode extends
		IAdaptable,
		IEditorInput,
		Comparable<ICubridNode> {

	/**
	 * Get whether it is container node
	 * 
	 * @return boolean
	 */
	public boolean isContainer();

	/**
	 * 
	 * Set this node for container node
	 * 
	 * @param isContainer
	 */
	public void setContainer(boolean isContainer);

	/**
	 * 
	 * Get child CUBRID Node by id
	 * 
	 * @param id
	 * @return
	 */
	public ICubridNode getChild(String id);

	/**
	 * Search all children nodes and Get the child node by id
	 * 
	 * @param id
	 * @return
	 */
	public ICubridNode getChildInAll(String id);

	/**
	 * 
	 * Get all children of this node
	 * 
	 * @return
	 */
	public List<ICubridNode> getChildren();

	/**
	 * 
	 * Get all children of this node
	 * 
	 * @param monitor
	 * @return Cubrid Node Array
	 */
	public ICubridNode[] getChildren(IProgressMonitor monitor);

	/**
	 * Add child object to this node
	 * 
	 * @param obj
	 */
	public void addChild(ICubridNode obj);

	/**
	 * Remove child object from this node
	 * 
	 * @param obj
	 */
	public void removeChild(ICubridNode obj);

	/**
	 * Remove all child objects from this node
	 */
	public void removeAllChild();

	/**
	 * Get parent object of this node
	 * 
	 * @return ITree
	 */
	public ICubridNode getParent();

	/**
	 * Set this node's parent node object
	 * 
	 * @param obj
	 */
	public void setParent(ICubridNode obj);

	/**
	 * Get whether contain this child node in this node,only traverse the first
	 * level
	 * 
	 * @param obj
	 * @return boolean
	 */
	public boolean isContained(ICubridNode obj);

	/**
	 * Get whether contain this child node in this node,traverse all children
	 * 
	 * @param obj
	 * @return boolean
	 */
	public boolean isContainedInAll(ICubridNode obj);

	/**
	 * Retrun whether it is the top level node
	 * 
	 * @return boolean
	 */
	public boolean isRoot();

	/**
	 * Set this node for root node
	 * 
	 * @param isRoot
	 */
	public void setRoot(boolean isRoot);

	/**
	 * Get the path of this node object's icon path
	 * 
	 * @return String
	 */
	public String getIconPath();

	/**
	 * Set the path of this node object's icon path
	 * 
	 * @param iconPath
	 */
	public void setIconPath(String iconPath);

	/**
	 * Get displayed label of this node
	 * 
	 * @return String
	 */
	public String getLabel();

	/**
	 * Set displayed label of this node
	 * 
	 * @param label
	 */
	public void setLabel(String label);

	/**
	 * Get the UUID of this node
	 * 
	 * @return
	 */
	public String getId();

	/**
	 * Set the UUID of this node
	 * 
	 * @param id
	 */
	public void setId(String id);

	/**
	 * Get editor id of this node
	 * 
	 * @return
	 */
	public String getEditorId();

	/**
	 * 
	 * Set editor id of this node
	 * 
	 * @param editorId
	 */
	public void setEditorId(String editorId);

	/**
	 * 
	 * Get view id of this node
	 * 
	 * @return
	 */
	public String getViewId();

	/**
	 * 
	 * Set view id of this node
	 * 
	 */
	public void setViewId(String viewId);

	/**
	 * 
	 * Get this node object's loader for loading all children
	 * 
	 * @return the CUBRID node loader
	 */
	public ICubridNodeLoader getLoader();

	/**
	 * Set this node object's loader for loading all children
	 * 
	 * @param loader
	 */
	public void setLoader(ICubridNodeLoader loader);

	/**
	 * Set this node object's type
	 * 
	 * @param type
	 */
	public void setType(CubridNodeType type);

	/**
	 * Get this node object's type
	 * 
	 * @return
	 */
	public CubridNodeType getType();

	/**
	 * Get the server that this node belong to
	 * 
	 * @return
	 */
	public CubridServer getServer();

	/**
	 * Set the server that this node belong to
	 * 
	 * @param obj
	 */
	public void setServer(CubridServer obj);

	/**
	 * Set the corresponding CUBRID model object of this node
	 * 
	 * @param obj
	 */
	public void setModelObj(Object obj);

}
