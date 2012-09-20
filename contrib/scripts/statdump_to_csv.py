#!/usr/bin/python

import sys, os
import re
from optparse import OptionParser

usage = "usage: %prog [options] input_statdump_filename output_csv_filename"
parser = OptionParser(usage=usage, version="%prog 1.0")
parser.add_option("-F", "--from", dest="From", default='0', help="from time ex: '17 10:24:32'");
parser.add_option("-T", "--to", dest="To", default='9', help="to time ex: '18 20:27:32'");
parser.add_option("-s", dest="short", default=False, help="short titles", action="store_true");

(options, args) = parser.parse_args()

if len(args) == 0:
	parser.print_usage();
	sys.exit();

re_stat = re.compile('([0-9]+ [0-9][0-9]:[0-9][0-9]:[0-9][0-9])[^\0]*?OTHER STATISTICS[^\0]*?\n\n');
re_items = re.compile('[a-zA-Z0-9_-]+[ \t]*=[ \t]*[0-9\.]+');


file = open(args[0], 'r');
lines = file.read();
titles = [];
datas = [];

start = 0;
while True:
	m = re_stat.search(lines, start);
	if m is None:
		#print 'match done';
		break;

	statdump = m.group(0);
	time = m.group(1);
	start = m.end();

	#print statdump
	#print '--------------';

	if time < options.From:
		#print 'continue';
		continue;

	if time > options.To:
		#print 'time out', date, options.To;
		break;

	items = re_items.findall(statdump);

	if len(titles) == 0:
		titles.append('Time');
		for i in items:
			k = i.split('=')[0].strip();

			if options.short:
				k = k.replace('Num_', '');
				k = k.replace('query', 'Q');
				k = k.replace('btree', 'B');
				k = k.replace('file', 'F');
				k = k.replace('page', 'P');
				k = k.replace('data', 'D');
				k = k.replace('tran', 'T');
				k = k.replace('prior_lsa', 'LSA');
				k = k.replace('prior_lsa', 'LSA');
				k = k.replace('adaptive_flush', 'AF');
				k = k.replace('object', 'OBJ');
				k = k.replace('lock', 'LK');

			titles.append(k);

	data = [];
	data.append(time);
	for i in items:
		d = i.split('=')[1].strip();
		data.append(d);
		
	datas.append(data);

#print titles;
#print datas;

out = open(args[1], 'w');

for k in titles:
	out.write('%s,' % k);
out.write('\n');

for data in datas:
	for d in data:
		out.write('%s,' % d);
	out.write('\n');
 
	


	



	


