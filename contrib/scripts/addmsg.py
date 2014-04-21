#!/usr/bin/env python2

#
# addmsg - tool to append a message to a catalog set
#
# version: 1.0.0
#
# change log:
#
# 22-03-2012 (1.0.0) by vasi
#  initial script

import os
import sys
import re
import getopt
import shutil


#
# Show help screen
#
def print_help ():
	print ''
	print os.path.basename (sys.argv[0]), 'usage:'
	print '  %s [options] file set message' % (os.path.basename (sys.argv[0]))
	print ''
	print '   Options:'
	print '    -h , --help       : print usage'
	print ''
	print '   Arguments:'
	print '    file              : .msg file (without extension)'
	print '    set               : $set number to add message to'
	print '    message           : message string'
	print ''
	print '   Examples:'
	print '    %s cubrid 7 "Hello world!"' % (os.path.basename (sys.argv[0]))
	print '    %s csql 7 "Hello world!"' % (os.path.basename (sys.argv[0]))
	print ''


#
# Entry point
#
def main ():
	op_file = ''
	op_set = -1
	op_msg = ''

	# parse arguments
	try:
		opts, args = getopt.getopt (sys.argv[1:], "h::", ["help"])
	except getopt.GetoptError:
		print 'Encountered error while parsing arguments!'
		print_help ()
		sys.exit (2)

	for op, arg in opts:
		if op in ("-h", "--help"):
			print_help ()
			sys.exit ()

	
	# check parameters
	if len (args) != 3:
		print 'Incorrect number of parameters!'
		print_help ()
		sys.exit (2)
	
	# parse parameters
	op_file = args[0] + '.msg'
	op_msg = args[2]

	try:
		op_set = int(args[1])
	except ValueError:
		print 'Incorrect set number (must be integer)!'
		print_help ()
		sys.exit (2)

	# scan current folder for subdirs
	subdirs = [name for name in os.listdir('.') if os.path.isdir(os.path.join('.', name))]

	# work the (stupid) magic
	print ''
	for _dir in subdirs:

		# check file name
		_fname = _dir + '/' + op_file
		if not os.path.exists (_fname):
			print ' WARNING: File %(f)s does not exist!' % { 'f' : _fname }
			continue

		# open file and read all lines
		f = open (_fname, 'r')
		_lines = f.readlines ()
		f.close ()

		print ' File %(f)s has %(l)d lines:' % { 'f' : _fname, 'l' : len (_lines) }

		# find last message number
		_found = False
		_maxno = -1
		_maxline = -1
		_endofset = 0

		for _line in _lines:
			if _line.startswith ('$set') and _found:
				# next set found, nothing to do any more
				break

			if _line.startswith ('$set ' + str(op_set)):
				# found our set
				_found = True

			if _found:
				# get number
				_numstr = _line.split (' ', 1)
				_num = -1
				try:
					_num = int (_numstr[0])
				except ValueError:
					_nop = 0 # how the hell do you skip the except clause?

				# see if it's maximum
				if _num > _maxno:
					_maxno = _num
					_maxline = _endofset

			_endofset += 1

		# insert message
		if _maxno <= 0:
			_maxno = 1

		if _found:
			_lines.insert (_maxline + 1, str (_maxno + 1) + ' ' + op_msg + '\n');

		# show status
		if _found:
			print '  * added message #' + str (_maxno + 1)
		else:
			print '  * set not found!'

		# write file
		if _found:
			f = open (_fname, 'w')
			f.writelines (_lines)
			f.close ()

					


#
# Go to main
#
if __name__ == "__main__":
  main ()
