
from distutils.core import setup, Extension 
import os
import platform

if os.environ["CUBRID"]:
    lnk_dir = os.environ["CUBRID"] + "/lib"
    inc_dir = os.environ["CUBRID"] + "/include"
else:
    raise KeyError

setup(
    name = "CUBRID-Python", 
    version = "9.1.0.0001",
    description = "Python interface to CUBRID",
    long_description = \
            "Python interface to CUBRID conforming to the python DB API 2.0 "
            "specification.\n"
            "See http://www.python.org/topics/database/DatabaseAPI-2.0.html.",
    py_modules=["_cubrid_exceptions", "CUBRIDdb.connections", "CUBRIDdb.cursors", "CUBRIDdb.FIELD_TYPE"],
    author = "zhanghui",
    author_email = "zhanghui@nhn.com",
    license = "BSD",
    url = "https://cubridinterface.svn.sourceforge.net/svnroot/cubridinterface/python",
    ext_modules=[
        Extension(
            name = "_cubrid", 
            library_dirs = [lnk_dir],
            libraries = ["cascci"],
            include_dirs = [inc_dir],
            sources = ['python_cubrid.c'],
        )
    ] 
)

