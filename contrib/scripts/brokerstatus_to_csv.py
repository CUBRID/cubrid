#!/usr/bin/python -u

import sys, os
import re
from optparse import OptionParser
from time import gmtime, strftime

usage = "usage: %prog [options] input_stats_filename[default: - (stdin)] output_csv_filename[default: - (stdout)]"
parser = OptionParser(usage=usage, version="%prog 1.0")
parser.add_option("-s", dest="short", default=False, help="short titles", action="store_true");

(options, args) = parser.parse_args()

if len(args) == 0:
	file = sys.stdin
	out = sys.stdout
elif len(args) == 1:
	file = None
	out = sys.stdout
else:
	file = None
	out = None

re_head = re.compile('^\s+NAME\s+.*')
re_stat = re.compile('^\*\s+(\w+)\s+(.*)');


if not file and not args[0] == '-':
	file = open(args[0], 'r');
else:
	file = sys.stdin

titles = [];

if not out and not args[1] == '-':
	out = open(args[1], 'w');
else:
	out = sys.stdout

while True:
	try:
		line = file.readline()
	except KeyboardInterrupt:
		print >> sys.stderr, 'Quit'
		break
	if not line:
		break;

	if len(titles) == 0:
		m = re_head.search(line, 0)
		if m:
			out.write ('Time,');
			titles = m.group(0).split()
			for k in titles:
				if options.short:
					k = k[0] + k[-1]
				out.write ('%s,' % k);
			out.write ('\n');
			continue
		  	
	m = re_stat.search(line, 0);
	if m is None:
		continue;

	time = strftime("%Y-%m-%d %H:%M:%S", gmtime())
	if m.group(2) == 'OFF':
	  continue

	bname = m.group(1)
	stats = m.group(2);

	items = stats.split()

	out.write ('%s,%s,' % (time, bname))
	for d in items:
		out.write ('%s,' % d);
	out.write ('\n');

