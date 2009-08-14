/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *  - Neither the name of the <ORGANIZATION> nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
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
package com.cubrid.cubridmanager.ui.logs;

/**
 * 
 * This class is responsible to control page action.
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-4-3 created by wuyingshi
 */
public class PageUtil {
	private int totalRs = 0; // total row
	private int pageSize = 100; // size per page
	private int pages = 0; // total page num
	private int currentPage = 1; // current page

	/**
	 * The constructor
	 */
	public PageUtil() {

	}

	/**
	 * The constructor
	 * 
	 * @param totalRs
	 * @param pageSize
	 */
	public PageUtil(int totalRs,int pageSize) {
		this.totalRs = totalRs;
		if (pageSize > 0)
			this.pageSize = pageSize;
		if (totalRs % pageSize == 0)
			pages = totalRs / pageSize;
		else
			pages = totalRs / pageSize + 1;
	}

	/**
	 * get the total row count.
	 * 
	 * @return
	 */
	public int getTotalRs() {
		return totalRs;
	}

	/**
	 * set the total row count.
	 * 
	 * @param totalRs
	 */
	public void setTotalRs(int totalRs) {
		this.totalRs = totalRs;
		if (pageSize > 0) {
			if (totalRs % pageSize == 0)
				pages = totalRs / pageSize;
			else
				pages = totalRs / pageSize + 1;
		}
	}

	/**
	 * get the per page record count.
	 * 
	 * @return
	 */
	public int getPageSize() {
		return pageSize;
	}

	/**
	 * set the per page record count.
	 * 
	 * @param pageSize
	 */
	public void setPageSize(int pageSize) {
		this.pageSize = pageSize;
		if (totalRs % pageSize == 0)
			pages = totalRs / pageSize;
		else
			pages = totalRs / pageSize + 1;
	}

	/**
	 * get total page count.
	 * 
	 * @return
	 */
	public int getPages() {
		return pages;
	}

	/**
	 * set total page count.
	 * 
	 * @param pages
	 */
	public void setPages(int pages) {
		this.pages = pages;
	}

	/**
	 * get current page.
	 * 
	 * @return
	 */
	public int getCurrentPage() {
		return currentPage;
	}

	/**
	 * set current page.
	 * 
	 * @param currentPage
	 */
	public void setCurrentPage(int currentPage) {
		if (currentPage > 0 && currentPage <= pages)
			this.currentPage = currentPage;
	}
}
