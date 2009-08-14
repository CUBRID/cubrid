/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search Solution. 
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
package com.cubrid.cubridmanager.core.query.plan;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.StringTokenizer;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.apache.log4j.Logger;

import com.cubrid.cubridmanager.core.common.StringUtil;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.query.plan.model.PlanCost;
import com.cubrid.cubridmanager.core.query.plan.model.PlanNode;
import com.cubrid.cubridmanager.core.query.plan.model.PlanRoot;
import com.cubrid.cubridmanager.core.query.plan.model.PlanTable;
import com.cubrid.cubridmanager.core.query.plan.model.PlanTerm;
import com.cubrid.cubridmanager.core.query.plan.model.PlanTermItem;
import com.cubrid.cubridmanager.core.query.plan.model.PlanTerm.PlanTermType;

/**
 * 
 * This class is responsible to parse a raw execution plan string.
 * 
 * PlanParser Description
 * 
 * @author pcraft
 * @version 1.0 - 2009. 06. 06 created by pcraft
 */
public class PlanParser {
	private static final Logger logger = LogUtil.getLogger(PlanParser.class);

	private List<PlanRoot> planTreeList = null;

	public PlanRoot getPlanTree(int index) {
		if (planTreeList == null) {
			return null;
		}

		if (planTreeList.size() <= index) {
			return null;
		}

		return planTreeList.get(index);
	}

	public int countPlanTree() {
		if (planTreeList == null) {
			return 0;
		}

		return planTreeList.size();
	}

	public boolean doParse(String string) {

		if (string == null || string.length() == 0) {
			return false;
		}

		string = StringUtil.replace(string, "\r\n", "\n"); // for Win OS
		string = StringUtil.replace(string, "\r", "\n"); // for Mac OS

		List<Integer> eachPlanStartPointList = new ArrayList<Integer>();

		// count subplans
		for (int sp = 0;;) {

			sp = string.indexOf("Join graph segments (f indicates final):", sp);
			if (sp == -1) {
				break;
			}

			eachPlanStartPointList.add(sp);

			sp += 1;
		}

		if (eachPlanStartPointList.size() == 0) {
			return false;
		}

		if (logger.isDebugEnabled())
			logger.debug("<subplanCount>" + eachPlanStartPointList.size()
					+ "</subplanCount>");

		eachPlanStartPointList.add(string.length());

		for (int i = 0, len = eachPlanStartPointList.size() - 1; i < len; i++) {

			int sp = eachPlanStartPointList.get(i);
			int ep = eachPlanStartPointList.get(i + 1);

			String partOfPlanString = string.substring(sp, ep).trim();
			if (!doParseEachPlan(partOfPlanString)) {
				return false;
			}

		}

		return true;
	}

	private boolean doParseEachPlan(String string) {
		Map<String, String> dic = getReplacementMap(string);

		int sp = string.indexOf("Query stmt:");
		if (sp == -1) {
			return false;
		}

		sp = string.indexOf('\n', sp);
		if (sp == -1) {
			return false;
		}

		String sql = string.substring(sp).trim();

		sp = string.indexOf("Query plan:");
		if (sp == -1) {
			return false;
		}

		sp = string.indexOf("\n\n", sp);
		if (sp == -1) {
			return false;
		}

		sp += 2;

		int ep = string.indexOf("\n\n", sp);
		if (ep == -1) {
			return false;
		}

		StringBuilder planString = new StringBuilder(string.substring(sp, ep));
		if (logger.isDebugEnabled())
			logger.debug("<query plan string>" + planString.toString()
					+ "</query plan string>");

		if (dic != null) {
			Set<String> keys = dic.keySet();
			for (String key : keys) {
				String val = dic.get(key);
				StringUtil.replace(planString, key, val);
			}

			if (logger.isDebugEnabled())
				logger.debug("<query plan binding>" + planString.toString()
						+ "</query plan binding>");
		}

		String refinedRawPlan = refineRawPlanString(planString.toString());
		if (logger.isDebugEnabled())
			logger.debug("<refinedRawPlan>" + refinedRawPlan + "</refinedRawPlan>");

		PlanNode planNode = new PlanNode();
		parseTree(planNode, refinedRawPlan, 0, refinedRawPlan.length());

		if (planTreeList == null) {
			planTreeList = new ArrayList<PlanRoot>();
		}

		PlanRoot planRoot = new PlanRoot();
		planRoot.setPlanNode(planNode);
		planRoot.setSql(sql);
		planRoot.setRaw(string);

		planTreeList.add(planRoot);

		return true;

	}

	private String refineRawPlanString(String planString) {

		int lastSpc = 0;
		int depth = 0;

		StringBuilder result = new StringBuilder();
		StringTokenizer token = new StringTokenizer(planString, "\n");
		while (token.hasMoreTokens()) {
			String row = token.nextToken();
			int spc = StringUtil.countSpace(row);

			if (lastSpc < spc) {
				depth++;
			} else if (lastSpc > spc) {
				depth--;
			}

			for (int i = 0; i < depth; i++) {
				result.append("    "); // add for depth spaces
			}

			result.append(row.trim()).append('\n');
			lastSpc = spc;
		}

		return result.toString().replaceAll("\\[[0-9]+\\]", ""); // column + [1] => column

	}

	private Map<String, String> getReplacementMap(String string) {

		Map<String, String> dic = new HashMap<String, String>();

		int type = 0;

		String[] rows = string.trim().split("\\\n");

		for (int i = 0, len = rows.length; i < len; i++) {

			String row = rows[i].trim();
			if (row.length() == 0) {
				continue;
			}

			String[] arr = row.split(":");
			if (arr.length == 1) {
				String idx = arr[0];

				if (idx.indexOf("Join graph segments (f indicates final)") != -1) {
					type = 1;
				} else if (idx.indexOf("Join graph nodes") != -1) {
					type = 2;
				} else if (idx.indexOf("Join graph equivalence classes") != -1) {
					type = 3;
				} else if (idx.indexOf("Join graph edges") != -1) {
					type = 4;
				} else if (idx.indexOf("Join graph terms") != -1) {
					type = 5;
				} else if (idx.indexOf("Query plan") != -1) {
					type = 6;
				} else if (idx.indexOf("Query stmt") != -1) {
					type = 7;
				}

				continue;
			}

			switch (type) {
			case 1:
			case 3:
				continue;
			case 6:
			case 7:
				break;
			case 2:
			case 4:
			case 5:
				if (arr.length == 2) {
					dic.put(arr[0].trim(), arr[1].trim());
				}
				break;
			}

		}

		return dic;
		
	}

	private void parseTree(PlanNode parent, String string, int sp, int ep) {

		int newSp = string.indexOf('\n', sp);
		if (newSp == -1) {
			return;
		}

		parent.setMethod(string.substring(sp, newSp).trim());

		sp = newSp + 1;

		for (;;) {
			int eol = string.indexOf("\n", sp);
			if (eol == -1) {
				eol = ep;
			}

			String row = string.substring(sp, eol);
			int nvSplitPos = row.indexOf(':');
			if (nvSplitPos == -1) {
				break;
			}

			String name = row.substring(0, nvSplitPos).trim();
			if ("outer".equals(name) || "inner".equals(name)
					|| "subplan".equals(name)) {

				PlanNode child = parent.newChild();
				child.setPosition(name);

				int childSp = string.indexOf(':', sp) + 1;
				int childEp = getEndPositionOfChildNodeString(string, childSp,
						ep);
				if (childEp == -1) {
					break;
				}

				String area = string.substring(childSp, childEp).trim();
				if (logger.isDebugEnabled())
					logger.debug("<area>" + area + "</area>");

				parseTree(child, string, childSp, childEp);
				eol = childEp;
			} else if ("cost".equals(name)) {
				if (logger.isDebugEnabled())
					logger.debug("cost");
				String partString = row.substring(nvSplitPos + 1).trim();
				parent.setCost(parseCost(partString));
			} else if ("class".equals(name)) {
				if (logger.isDebugEnabled())
					logger.debug("class");
				String partString = row.substring(nvSplitPos + 1).trim();
				parent.setTable(parseClass(partString));
			} else if ("index".equals(name)) {
				if (logger.isDebugEnabled())
					logger.debug("index");
				String partString = row.substring(nvSplitPos + 1).trim();
				PlanTerm planTerm = parseIndex(partString);
				planTerm.setType(PlanTermType.INDEX);
				parent.setIndex(planTerm);
			} else if ("edge".equals(name)) {
				if (logger.isDebugEnabled())
					logger.debug("edge");
				String partString = row.substring(nvSplitPos + 1).trim();
				PlanTerm planTerm = parseTerm(partString);
				planTerm.setType(PlanTermType.EDGE);
				parent.setEdge(planTerm);
			} else if ("sargs".equals(name)) {
				if (logger.isDebugEnabled())
					logger.debug("sargs");
				String partString = row.substring(nvSplitPos + 1).trim();
				PlanTerm planTerm = parseTerm(partString);
				planTerm.setType(PlanTermType.SARGS);
				parent.setSargs(planTerm);
			} else if ("filtr".equals(name)) {
				if (logger.isDebugEnabled())
					logger.debug("filtr");
				String partString = row.substring(nvSplitPos + 1).trim();
				PlanTerm planTerm = parseTerm(partString);
				planTerm.setType(PlanTermType.FILTER);
				parent.setFilter(planTerm);
				// filtr: x.c<>'a' (sel 0.999)(sargterm)(not-joineligible)(loc0)
			} else if ("order".equals(name)) {
				if (logger.isDebugEnabled())
					logger.debug("order");
				String partString = row.substring(nvSplitPos + 1).trim();
				parent.setOrder(partString);
			} else if ("others".equals(name)) {
				if (logger.isDebugEnabled())
					logger.debug("others");
			} else if ("sort".equals(name)) {
				if (logger.isDebugEnabled())
					logger.debug("sort");
				String partString = row.substring(nvSplitPos + 1).trim();
				parent.setSort(partString);
			}

			// subplan: m-join(inner join) 

			sp = eol + 1;

			if (logger.isDebugEnabled())
				logger.debug("sp=" + sp);

			if (sp >= ep) {
				break;
			}
		}
		
	}

	private int getEndPositionOfChildNodeString(String string, int sp, int ep) {

		sp = string.indexOf('\n', sp);
		if (sp == -1) {
			return -1;
		}

		int spc = 0;
		for (int i = sp + 1, len = string.length(); i < len; i++) {
			if (string.charAt(i) != ' ')
				break;
			spc++;
		}

		String spcString = "\n" + StringUtil.repeat(" ", spc);
		if (logger.isDebugEnabled())
			logger.debug("<spc>" + spc + "</spc>");

		for (;;) {
			int lsp = string.indexOf('\n', sp);
			if (lsp == -1) {
				break;
			}

			int lep = string.indexOf('\n', lsp + 1);
			if (lep == -1) {
				lep = ep;
			}

			String s = string.substring(lsp, lep);
			if (logger.isDebugEnabled())
				logger.debug("<s>" + s + "</s>");

			if (s.indexOf(spcString) == -1) {
				break;
			}

			sp = lep;
		}

		return sp;

	}

	/**
	 * Returns the cost while a query execution.
	 *  
	 * @param	raw
	 * 			A raw text with a query execution cost informations.
	 * 
	 * @return	PlanCost object
	 */
	private PlanCost parseCost(String raw) {

		// fixed 0(0.0/0.0) var 281(16.7/264.0) card 6677

		PlanCost planCost = null;

		String pattenString = "fixed[ ]+([0-9]+)\\(([0-9\\.]+)/([0-9\\.]+)\\)[ ]+var[ ]+([0-9]+)\\(([0-9\\.]+)/([0-9\\.]+)\\)[ ]+card[ ]+([0-9]+)";
		Matcher matcher = Pattern.compile(pattenString).matcher(raw.trim());
		if (logger.isDebugEnabled()) {
			logger.debug("<groupCount>" + matcher.groupCount()
					+ "</groupCount>");
			logger.debug("<parseCost-str>" + raw + "</parseCost-str>");
		}

		if (matcher.matches() && matcher.groupCount() == 7) {
			planCost = new PlanCost();

			planCost.setFixedTotal(StringUtil.intValue(matcher.group(1)));
			if (logger.isDebugEnabled()) {
				logger.debug("<parseCost-match-group1>" + matcher.group(1)
						+ "</parseCost-match-group1>");
			}
			planCost.setFixedCpu(StringUtil.floatValue(matcher.group(2)));
			if (logger.isDebugEnabled()) {
				logger.debug("<parseCost-match-group2>" + matcher.group(2)
						+ "</parseCost-match-group2>");
			}
			planCost.setFixedDisk(StringUtil.floatValue(matcher.group(3)));
			if (logger.isDebugEnabled()) {
				logger.debug("<parseCost-match-group3>" + matcher.group(3)
						+ "</parseCost-match-group3>");
			}

			planCost.setVarTotal(StringUtil.intValue(matcher.group(4)));
			planCost.setVarCpu(StringUtil.floatValue(matcher.group(5)));
			planCost.setVarDisk(StringUtil.floatValue(matcher.group(6)));

			planCost.setCard(StringUtil.intValue(matcher.group(7)));
		}

		return planCost;

	}

	/**
	 * split with a partition info and a table name
	 * 
	 * <p>index == 0 - table name</p>
	 * <p>index > 0 - partition name</p>
	 * 
	 * @param raw
	 * @return
	 */
	private String[] splitPartitionedTable(String raw) {
		
		if (raw == null || raw.length() <= 2) {
			return null;
		}
		
		int sp = raw.indexOf("(");
		if (sp == -1)
			return null;
		
		int ep = raw.indexOf(")");
		if (ep == -1)
			return null;
		
		raw = raw.substring(sp+1, ep);
		
		String[] arr = raw.split(",");
		if (arr == null || arr.length == 0) {
			return null;
		}
		
		String[] res = new String[arr.length];
		for (int i = 0, len = arr.length; i < len; i++) {
			res[i] = arr[i].trim();
		}
		
		return res;
		
	}
	
	private PlanTable parseClass(String raw) {

		// C nation C(215/6)

		int sp = raw.indexOf(' ');
		if (sp == -1) {
			return null;
		}

		raw = raw.substring(sp + 1);
		if (logger.isDebugEnabled())
			logger.debug("<raw>" + raw + "</raw>");

		// for partitioned table : (game, game__p__medal1) as game(2833/196) (sargs 0)
		// eg. general table : athlete A(6677/264)
		sp = 0;
		boolean partitioned = false;
		if (raw.charAt(0) == '(') {
			sp = 1;
			partitioned = true;
		}
		
		int ep = raw.indexOf('(', sp);
		if (ep == -1) {
			return null;
		}
		
		String className = raw.substring(0, ep);
		if (logger.isDebugEnabled())
			logger.debug("<className>" + className + "</className>");

		// for partitioned table
		String[] partitions = null;
		if (partitioned) {
			String[] tmpArr = splitPartitionedTable(className);
			if (tmpArr != null && tmpArr.length > 1) {
				className = tmpArr[0];
				partitions = new String[tmpArr.length - 1];
				for (int i = 1, len = tmpArr.length; i < len; i++) {
					partitions[i - 1] = tmpArr[i].trim();
					if (logger.isDebugEnabled())
						logger.debug("<partition>" + partitions[i - 1] + "</partition>");
				}
			}
			
			if (logger.isDebugEnabled())
				logger.debug("<className-new>" + className + "</className-new>");
		}
		
		sp = ep;
		ep = raw.indexOf(')', sp);
		
		System.err.println(sp+" "+ep);
		if (ep == -1) {
			return null;
		}
		raw = raw.substring(sp, ep + 1);

		String pattenString = "\\(([0-9]+)/([0-9]+)\\)";
		Matcher matcher = Pattern.compile(pattenString).matcher(raw);
		if (logger.isDebugEnabled())
			logger.debug("<PlanClass:matches>" + matcher.matches()
					+ "</PlanClass:matches>");
		if (logger.isDebugEnabled())
			logger.debug("<PlanClass:groupCount>" + matcher.groupCount()
					+ "</PlanClass:groupCount>");

		if (!matcher.matches() || matcher.groupCount() != 2) {
			return null;
		}

		PlanTable planClass = new PlanTable();
		planClass.setName(className);
		planClass.setCard(StringUtil.intValue(matcher.group(1)));
		planClass.setPage(StringUtil.intValue(matcher.group(2)));
		planClass.setPartitions(partitions);

		return planClass;

	}

	private PlanTerm parseIndex(String indexRaw) {

		if (indexRaw == null || indexRaw.length() == 0) {
			return null;
		}

		// pk_nation_code C.code=A.nation_code (sel 0.00465116) (join term) (mergeable) (inner-join) (indexable code[2]) (loc 0) AND C.code=A.nation_code (sel 0.00465116) (join term) (mergeable) (inner-join) (indexable code[2]) (loc 0)
		int sp = indexRaw.indexOf(' ');
		if (sp == -1) {
			return null;
		}

		String indexName = indexRaw.substring(0, sp++);

		PlanTerm planIndex = parseTerm(indexRaw.substring(sp).trim());
		if (planIndex == null) {
			planIndex = new PlanTerm();
		}
		planIndex.setName(indexName);

		return planIndex;

	}

	private PlanTerm parseTerm(String raw) {

		// sargs: rownum range (min inf_lt 10) (sel 0.1) (rank 3) (instnum term) (not-join eligible) (loc 0)
		// sargs: A.gender=B.s_name (sel 0.001) (join term) (mergeable) (inner-join) (loc 0)
		// sargs: y.j range (min inf_lt10) (sel 1) (rank 2) (sargterm) (not-join eligible) (loc 0)
		// sargs: x.i range ((select max(z.i) from z zwhere z.c=x.c) gt_inf max) (sel 0.1) (rank 10) (sarg term) (not-join eligible) (loc 0)
		// sargs: x.vc range ('b' gt_inf max) (sel 0.1) (rank 2) (sarg term) (not-join eligible) (loc 0)
		// edge:  A.gender=B.s_name (sel 0.001) (join term) (mergeable) (inner-join) (loc 0)

		PlanTerm term = new PlanTerm();
		term.setName("");

		int sp = 0;
		String[] arr = raw.split(" AND ");
		for (int i = 0, len = arr.length; i < len; i++) {
			String eachTerm = arr[i].trim();
			sp = eachTerm.indexOf("(sel");
			if (sp == -1) {
				sp = eachTerm.indexOf("(");
				if (sp == -1) {
					continue;
				}
			}

			PlanTermItem item = new PlanTermItem();
			item.setCondition(eachTerm.substring(0, sp).trim());
			item.setAttribute(eachTerm.substring(sp).trim());
			term.addTermItem(item);
		}

		return term;

	}

}
