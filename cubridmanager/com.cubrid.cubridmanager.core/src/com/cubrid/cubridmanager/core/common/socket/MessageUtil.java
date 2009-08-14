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
package com.cubrid.cubridmanager.core.common.socket;

/**
 * 
 * This class is responsible to parse response message
 * 
 * @author moulinwang
 * @version 1.0 - 2009-6-4 created by moulinwang
 */
public class MessageUtil {

	public static TreeNode parseResponse(String response) {
		return parseResponse(response, false);
	}

	/**
	 * Parse response message into tree structure
	 * 
	 * @param response
	 * @return
	 */
	public static TreeNode parseResponse(String response,
			boolean bUsingSpecialDelimiter) {
		String[] toks = response.split("\n");

		TreeNode root = new TreeNode();
		TreeNode node = root;
		int maxindex = toks.length;
		if (bUsingSpecialDelimiter) {
			if (toks[maxindex - 1].equals("END__DIAGDATA")) {// remove the
				// last unneeded
				// line
				maxindex--;
			}
		}
		for (int i = 0; i < maxindex; i++) {
			String messageitem = toks[i];
			// append next line if it does not contain char ':'
			while (i + 1 < maxindex && -1 == toks[i + 1].indexOf(":")) {// failure's
				// note
				// message
				// and
				// others
				messageitem += toks[i + 1];
				i++;
			}
			if (messageitem.startsWith("cas_mon:DIAG_DEL:start")
					|| messageitem.startsWith("open:")
					|| messageitem.startsWith("start:")) {
				TreeNode newnode = new TreeNode();
				node.addChild(newnode);
				node = newnode;
				addMsgItem(node, messageitem, bUsingSpecialDelimiter);
			} else if (messageitem.startsWith("cas_mon:DIAG_DEL:end")
					|| messageitem.startsWith("close:")
					|| messageitem.startsWith("end:")) {
				addMsgItem(node, messageitem, bUsingSpecialDelimiter);
				node = node.getParent();
			} else {
				addMsgItem(node, messageitem, bUsingSpecialDelimiter);
			}
		}
		return root;
	}

	/**
	 * Add a message item into TreeNode node assert each message item contains
	 * char ':'
	 * 
	 * @param node
	 * @param msgitem
	 */
	private static void addMsgItem(TreeNode node, String msgitem,
			boolean bUsingSpecialDelimiter) {
		assert (msgitem.indexOf(":") != -1);
		if (bUsingSpecialDelimiter) {
			int index = msgitem.indexOf(":DIAG_DEL:");
			if (index >= 0) {
				String key = msgitem.substring(0, index);
				String value = msgitem.substring(index + 10);
				node.add(key, value);
			} else {
				node.add(msgitem);
			}
		} else {
			node.add(msgitem);
		}
	}

}
