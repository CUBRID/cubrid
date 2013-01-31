
from distutils.core import setup, Extension 

import os
import sys
import platform

if os.environ["CUBRID"]:
    lnk_dir = os.environ["CUBRID"] + "/lib"
    inc_dir = os.environ["CUBRID"] + "/include"
else:
    raise KeyError

if sys.version_info[0] == 2 and sys.version_info[1] >= 5:
    py_modules = [
        "CUBRIDdb.connections", "CUBRIDdb.cursors", "CUBRIDdb.FIELD_TYPE",
        "django_cubrid.base", "django_cubrid.client", "django_cubrid.compiler", 
        "django_cubrid.creation", "django_cubrid.introspection", "django_cubrid.validation",
        ]
else:
    py_modules = ["CUBRIDdb.connections", "CUBRIDdb.cursors", "CUBRIDdb.FIELD_TYPE"]

# Install CUBRID-Python driver.
setup(
    name = "CUBRID-Python", 
    version = "9.1.0.0001",
    description = "Python interface to CUBRID",
    long_description = \
            "Python interface to CUBRID conforming to the python DB API 2.0 "
            "specification.\n"
            "See http://www.python.org/topics/database/DatabaseAPI-2.0.html.",
    py_modules = py_modules,
    author = "Li Jinhu, Li Lin, Zhang hui",
    author_email = "beagem@nhn.com",
    license = "BSD",
    url = "http://svn.cubrid.org/cubridapis/python/",
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


