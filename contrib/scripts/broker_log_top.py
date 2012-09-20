#!/usr/bin/python

import sys, os, time
import re
from optparse import OptionParser




def sort_max(x, y):
	return int((y.max - x.max) * 1000000);

def sort_min(x, y):
	return int((y.min - x.min) * 1000000);
	
def sort_total(x, y):
	return int((y.total - x.total) * 1000000);
	
def sort_count(x, y):
	return int((y.count - x.count) * 1000000);
	
def sort_avg(x, y):
	return int((y.total/y.count - x.total/x.count) * 1000000);



class Query:
	def __init__(self, q, caslog):
		self.query = q;
		self.caslog = caslog;
		self.min = 1000000000;
		self.max = 0;
		self.total = 0;
		self.count = 0;
		self.filename = '';
		self.error = 0;
		self.log = {};

	def set(self, caslog, tm, filename, at):
		if tm < self.min:
			self.min = tm;

		if tm > self.max:
			self.max = tm;
			self.caslog = caslog;

		self.total = self.total + tm;
		self.count = self.count + 1;
		self.filename = filename;

		if caslog.find(' error:-') > 0:
			self.error = self.error + 1;

		if at is not None:
			at = at[:14];
			at = time.strptime('2000 ' + at, '%Y %m/%d %H:%M:%S');
			at = time.mktime(at);

			if self.log.has_key(at):
				if tm > self.log[at]:
					self.log[at] = tm;
			else:
				self.log[at] = tm;
	
	
class QueryInfo:
	def __init__(self, tran_mode):
		self.map = {};
		if tran_mode == True:
			self.tran_mode = 'T';
		else:
			self.tran_mode = 'Q';

		self.csv = False;

	def set(self, query, caslog, time, filename, at):
		if self.map.has_key(query) == False:
			self.map[query] = Query(query, caslog);
			
		ret = self.map[query];
		ret.set(caslog, time, filename, at);

		if at is not None:
			self.csv = True;

	def dump(self):
		keys = self.map.keys();
		for k in keys:
			print '%s' % self.map[k].caslog;
			print ' min: %f' % self.map[k].min;
			print ' max: %f' % self.map[k].max;
			print ' total: %f' % self.map[k].total;
			print ' count: %d' % self.map[k].count;
			print ' filename: %s' % self.map[k].filename;
			print '';

	def save(self, order):
		list = self.map.values();

		func = sort_max;
		if order == 'max':
			func = sort_max;
		elif order == 'min':
			func = sort_min;
		elif order == 'total':
			func = sort_total;
		elif order == 'count':
			func = sort_count;
		elif order == 'avg':
			func = sort_avg;

		ret = sorted(list, cmp = func);
		self.print_qlist(ret);

	def print_full(self, name, list):
		total_sum = 0;
		i=0;
		file = open('%s_top.full' % name, 'w');
		for r in list:
			i = i+1;
			file.write('[%c%d]-------------------------------------------\n' % (self.tran_mode, i));
			file.write('%s\n' % r.filename);
			file.write('%s\n\n' % r.caslog);
			total_sum = total_sum + r.total;
		file.close();

		return total_sum;
		

	def print_simple(self, name, list, total_sum):
		i=0;
		file = open('%s_top.simple' % name, 'w');
		file.write('\tmax\t\tmin\t\tavg\t\tcnt(err)\ttotal\t\tratio(%)\n');
		for r in list:
			i = i+1;
			file.write('---------------------------------------------------------------------------------------------------------------------------------------------------\n');
			file.write('[%c%d]\t%f\t%f\t%f\t%d(%d)\t\t%f\t%f\t(%s)\n\n' % (self.tran_mode, i, r.max, r.min, r.total/r.count, r.count, r.error, r.total, r.total/total_sum*100, r.filename));
			file.write('%s\n' % r.query);
		file.close();
		

	def print_report(self, name, list, total_sum):
		i=0;
		file = open('%s_top.report' % name, 'w');
		file.write('\tmax\t\tmin\t\tavg\t\tcnt(err)\ttotal\t\tratio(%)\n');
		file.write('---------------------------------------------------------------------------------------------------------\n');
		for r in list:
			i = i+1;
			file.write('[%c%d]\t%f\t%f\t%f\t%d(%d)\t\t%f\t%f\n' % (self.tran_mode, i, r.max, r.min, r.total/r.count, r.count, r.error, r.total, r.total/total_sum*100));
		file.close();

		
	def print_csv(self, name, list):
		csv = {};

		start_time = time.time();
		end_time = 0;

		idx = 1;
		for r in list:
			for k in r.log:
				if k < start_time:
					start_time = k;

				if k > end_time:
					end_time = k;

				if csv.has_key(k) == False:
					csv[k] = {}; 
				
				csv[k][idx] = r.log[k];

			idx = idx + 1;

		file = open('%s_top.csv' % name, 'w');
		file.write('time,');
		for i in range(1, idx):
			file.write('%c%d,' % (self.tran_mode, i));
		file.write('\n');

		t = start_time;
		while t < end_time:
			tm = time.localtime(t);
			file.write("'%02d/%02d %02d:%02d:%02d," % (tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec));

			if csv.has_key(t) == False:
				for i in range(1, idx):
					file.write('%f,' % 0);
				file.write('\n');
			else:
				for i in range(1, idx):
					ret = 0;
					if csv[t].has_key(i) == False:
						ret = 0;
					else:
						ret = csv[t][i];

					file.write('%f,' % ret);
				file.write('\n');

			t = t + 1;

		file.close();				


	def print_qlist(self, list):
		if self.tran_mode == 'T':
			name = 'tran';
		else:
			name = 'query';

		total_sum = self.print_full(name, list);
		self.print_simple(name, list, total_sum);
		self.print_report(name, list, total_sum);
		if self.csv:
			self.print_csv(name, list);

		





def process_query(name, options, query_info):
	print 'processing %s' % name;

	re_caslog = re.compile('([0-9][0-9][0-9 \/\:\.]+) [\(\)0-9]+ execute_?a?l?l? srv_h_id [0-9]+ ([^\n\r]+)[^\0]*? execute_?a?l?l? [0-9]+ tuple [0-9]+ time ([0-9\.]+)');
	file = open(name, 'r');

	lines = '';

	if options.sleep > 0:
		while True: 
			chunk  = file.read(4*1024*1024);
			if chunk == '':
				break;
			lines = lines + chunk;
			time.sleep(float(options.sleep)/1000);
	else:
		lines = file.read();


	start = 0;
	counter = 0;
	while True:
		m = re_caslog.search(lines, start);
		if m is None:
			#print 'match done';
			break;

		caslog = m.group(0);
		at = m.group(1);
		query = m.group(2);
		tm = float(m.group(3));
		start = m.end();

		#print caslog
		#print '--------------';
		
		date = caslog[:17];
		if date < options.From:
			#print 'continue';
			continue;

		if date > options.To:
			#print 'time out', date, options.To;
			break;

		if options.merge:
			query = re.sub(r'in[ ]*\([\?,]+,\?\)', 'in (?)', query);
			query = re.sub(r'limit [0-9]+,[0-9]+', 'limit ?,?', query);
			query = re.sub(r'limit [0-9]+', 'limit ?', query);

		if options.csv == False:
			at = None;

		query_info.set(query, caslog, tm, name, at);


def process_tran(name, options, tran_info):
	print 'processing %s' % name;

	re_caslog = re.compile('([0-9][0-9][0-9 \/\:\.]+) [\(\)0-9]+ [^\0]*? elapsed time ([0-9\.]+)');
	re_queries = re.compile('execute_?a?l?l? srv_h_id [0-9]+ ([^\n\r]+)');

	file = open(name, 'r');

	lines = '';

	if options.sleep > 0:
		while True: 
			chunk  = file.read(4*1024*1024);
			if chunk == '':
				break;
			lines = lines + chunk;
			time.sleep(float(options.sleep)/1000);
	else:
		lines = file.read();

		

	start = 0;
	counter = 0;
	while True:
		m = re_caslog.search(lines, start);
		if m is None:
			#print 'match done';
			break;

		query = m.group(0);
		caslog = m.group(0);
		at = m.group(1);
		tm = float(m.group(2));
		start = m.end();

		#print caslog
		#print '--------------';
		
		date = caslog[:17];
		if date < options.From:
			#print 'continue';
			continue;

		if date > options.To:
			#print 'time out', date, options.To;
			break;

		if options.merge:
			query = re_queries.findall(query);
			query = '\n\n'.join(query);
			query = re.sub(r'in[ ]*\([\?,]+,\?\)', 'in (?)', query);
			query = re.sub(r'limit [0-9]+,[0-9]+', 'limit ?,?', query);
			query = re.sub(r'limit [0-9]+', 'limit ?', query);

		if options.csv == False:
			at = None;

		tran_info.set(query, caslog, tm, name, at);
	
	

if __name__ == '__main__':

	usage = "usage: %prog [options] args"
	parser = OptionParser(usage=usage, version="%prog 1.0")
	parser.add_option("-F", "--from", dest="From", default='00/00', help="from datetime ex: '09/11 10:24:32.278', (default: 00/00)");
	parser.add_option("-T", "--to", dest="To", default='99/99', help="to datetime ex: '09/12 20', (default: 99/99)");
	parser.add_option("-s", "--sort", dest="sort", default='max', help="sort key: max, min, avg, count, total (default:max)");
	parser.add_option("-t", "--transaction", dest="transaction", default=False, help="transaction analysis", action="store_true");
	parser.add_option("-S", "--sleep", dest="sleep", default='50', help="sleep time to prevent io burst (default:50)");
	parser.add_option("-m", "--merge", dest="merge", default=False, help="merge same in-list queries (or transactions)", action="store_true");
	parser.add_option("-c", "--csv", dest="csv", default=False, help="dump max responses of each query (or transaction) to csv", action="store_true");

	(options, args) = parser.parse_args()

	if len(args) == 0:
		parser.print_usage();
		sys.exit();

	info = QueryInfo(options.transaction);

	if options.transaction:
		for name in args:
			process_tran(name, options, info);
	else:
		for name in args:
			process_query(name, options, info);


	#info.dump();
	info.save(options.sort);


	


