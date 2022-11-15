#!/bin/bash
#
#
#  Copyright 2016 CUBRID Corporation
# 
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
# 
#       http://www.apache.org/licenses/LICENSE-2.0
# 
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
# 

f=$1  

ext=$(expr $f : ".*\(\..*\)")

case $ext in
.c|.h|.i)
  indent -l120 -lc120 ${f}
;;
.cpp|.hpp|.ipp)
  astyle --style=gnu --mode=c --indent-namespaces --indent=spaces=2 -xT8 -xt4 --add-brackets --max-code-length=120 --align-pointer=name --indent-classes --pad-header --pad-first-paren-out ${f}
;;
.java)
  java -jar google-java-format-1.7-all-deps.jar -a -r ${f}
;;
esac
