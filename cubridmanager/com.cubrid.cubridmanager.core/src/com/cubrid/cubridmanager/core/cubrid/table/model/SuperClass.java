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

package com.cubrid.cubridmanager.core.cubrid.table.model;

import java.util.ArrayList;
import java.util.List;

/**
 * This class is to store basic super class's information of one class,
 * including just all attribute(class and instance) names, and all method(class
 * and instance) names
 * 
 * @author moulinwang 2009-3-27
 */
public class SuperClass {
	String name;
	List<String> classAttributes = null; // String
	List<String> attributes = null;
	List<String> classMethods = null;
	List<String> methods = null;

	public SuperClass() {
	}

	/**
	 * add an attribute to list
	 * 
	 * @param attribute
	 */
	public void addAttribute(String attribute) {
		if (null == attributes) {
			attributes = new ArrayList<String>();
		}
		attributes.add(attribute);
	}

	/**
	 * add a class attribute to list
	 * 
	 * @param classAttribute
	 */
	public void addClassAttribute(String classAttribute) {
		if (null == classAttributes) {
			classAttributes = new ArrayList<String>();
		}
		classAttributes.add(classAttribute);
	}

	/**
	 * add a class method to list
	 * 
	 * @param classMethod
	 */
	public void addClassMethod(String classMethod) {
		if (null == classMethods) {
			classMethods = new ArrayList<String>();
		}
		classMethods.add(classMethod);
	}

	/**
	 * add a method to list
	 * 
	 * @param method
	 */
	public void addMethod(String method) {
		if (null == methods) {
			methods = new ArrayList<String>();
		}
		methods.add(method);
	}

	public String getName() {
		return name;
	}

	public void setName(String name) {
		this.name = name;
	}

	public List<String> getClassAttributes() {
		return classAttributes;
	}

	public List<String> getAttributes() {
		return attributes;
	}

	public List<String> getClassMethods() {
		return classMethods;
	}

	public List<String> getMethods() {
		return methods;
	}
}
