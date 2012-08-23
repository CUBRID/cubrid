#
# Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met: 
#
# - Redistributions of source code must retain the above copyright notice, 
#   this list of conditions and the following disclaimer. 
#
# - Redistributions in binary form must reproduce the above copyright notice, 
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution. 
#
# - Neither the name of the <ORGANIZATION> nor the names of its contributors 
#   may be used to endorse or promote products derived from this software without 
#   specific prior written permission. 
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
# IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
# OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
# OF SUCH DAMAGE. 
#
#

#CUBRID-Python Distutils Setup

from distutils.core import setup, Extension 
import os
import platform 
import sys

if sys.argv[1] == 'install':
  cci_lib_dir = ''
  cci_inc_dir = ''
elif os.environ.has_key("CUBRID"):
    if '64bit' in platform.architecture():
        cci_lib_dir = os.environ["CUBRID"] + "/lib64"
    else:
        cci_lib_dir = os.environ["CUBRID"] + "/lib"

    cci_inc_dir = os.environ["CUBRID"] + "/include"
else:
    print "WARNING:",
    print "it seems that you did not install CUBRID."
    print "You must install CUBRID."
    sys.exit(1)

setup(
    name = "CUBRID-Python", 
    version = "0.5", 
    description="CUBRID API Module for Python", 
    author = "Kang, Dong-Wan", 
    author_email="cubrid_python@nhncorp.com",
    license="BSD",
    url="http://dev.naver.com/projects/cubrid-python",
    py_modules=["cubriddb"],
    ext_modules=[
        Extension(
            "cubrid", 
            ["cubrid.c"],
            include_dirs = [cci_inc_dir],
            library_dirs = [cci_lib_dir],
            libraries = ["cascci"],
        )
    ] 
)

