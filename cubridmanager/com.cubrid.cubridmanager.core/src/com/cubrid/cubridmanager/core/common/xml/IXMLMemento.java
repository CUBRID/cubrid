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
package com.cubrid.cubridmanager.core.common.xml;

import java.util.List;

/**
 * 
 * This interface is used for saving the state of an object that can be
 * persisted in the file system of xml format.
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public interface IXMLMemento {

	/**
	 * Return the created child with the given node name
	 * 
	 * @param name the node name
	 * @return the created child with the given node name
	 */
	public IXMLMemento createChild(String name);

	/**
	 * Return the first child with the given node name.
	 * 
	 * @param name the name
	 * @return the first child with the node name
	 */
	public IXMLMemento getChild(String name);

	/**
	 * Return all children with the given node name
	 * 
	 * @param name the node name
	 * @return the list of children with the node name
	 */
	public IXMLMemento[] getChildren(String name);

	/**
	 * Return the floating point value of the given key.
	 * 
	 * @param key the key
	 * @return the value, or <code>null</code> if the key was not found or was
	 *         found but was not a floating point number
	 */
	public Float getFloat(String key);

	/**
	 * Return the integer value of the given key.
	 * 
	 * @param key the key
	 * @return the value, or <code>null</code> if the key was not found or was
	 *         found but was not an integer
	 */
	public Integer getInteger(String key);

	/**
	 * Return the string value of the given key.
	 * 
	 * @param key the key
	 * @return the value, or <code>null</code> if the key was not found or was
	 *         found but was not an integer
	 */
	public String getString(String key);

	/**
	 * Return the boolean value of the given key.
	 * 
	 * @param key the key
	 * @return the value, or <code>null</code> if the key was not found or was
	 *         found but was not a boolean
	 */
	public Boolean getBoolean(String key);

	/**
	 * Return the text node data of this element
	 * 
	 * @return the node content
	 */
	public String getTextData();

	/**
	 * return all attribute name of this xml memento object
	 * 
	 * @return all attribute name of this xml memento object
	 */
	public List<String> getAttributeNames();

	/**
	 * Set the value of the given key to the given integer.
	 * 
	 * @param key the key
	 * @param value the value
	 */
	public void putInteger(String key, int value);

	/**
	 * Set the value of the given key to the given boolean value.
	 * 
	 * @param key the key
	 * @param value the value
	 */
	public void putBoolean(String key, boolean value);

	/**
	 * Set the value of the given key to the given string.
	 * 
	 * @param key the key
	 * @param value the value
	 */
	public void putString(String key, String value);

	/**
	 * Set the str to the text node of this element
	 * 
	 * @param str the node content
	 */
	public void putTextData(String str);
}