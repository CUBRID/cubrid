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
 * This class is to store information about a method on some class(table). There
 * are two kinds of type: instance method and class method
 * 
 * @author moulinwang 2009-3-27
 */
public class DBMethod {
	// method name, which could be used in sQL/X
	String name;
	// the class(table) on which the method is defined
	String inherit;
	// the argument
	List<String> arguments = null;
	// function name, which is defined in a object file
	String function;

	public DBMethod() {
	}

	/**
	 * add an argument to list
	 * 
	 * @param argument
	 */
	public void addArgument(String argument) {
		if (null == arguments) {
			arguments = new ArrayList<String>();
		}
		arguments.add(argument);
	}

	/**
	 * get the method name
	 * 
	 * @return
	 */
	public String getName() {
		return name;
	}

	/**
	 * set the method name
	 * 
	 * @param name
	 */
	public void setName(String name) {
		this.name = name;
	}

	/**
	 * get the class name which the method operator
	 * 
	 * @return
	 */
	public String getInherit() {
		return inherit;
	}

	/**
	 * set the class name which the method operator
	 * 
	 * @param inherit
	 */
	public void setInherit(String inherit) {
		this.inherit = inherit;
	}

	public String getFunction() {
		return function;
	}

	public void setFunction(String function) {
		this.function = function;
	}

	public List<String> getArguments() {
		return arguments;
	}
}
