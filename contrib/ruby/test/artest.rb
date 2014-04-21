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

require 'rubygems'
require_gem 'activerecord'
#require 'activerecord'

ActiveRecord::Base.establish_connection(:adapter => "cubrid", :database => "demodb")
    
class Order < ActiveRecord::Base
end

order = Order.find(1, 2)
puts order[0].name

order = Order.new
order.name = 'world'
order.save
puts order.id

order.destroy

#require "rubygems"
#require_gem "activerecord"

#ActiveRecord::Base.establish_connection(:adapter => "cubrid", 
#    :host => "sun", :port => 31000, :database => "testdb", 
#    :username => "itrack", :password => "itrack")
    
#class Post < ActiveRecord::Base
#end
#post = Post.find_by_title("a")
#puts post.title
#puts post.body
#puts post.last_updated
#post.title = "updated title.."
#post.save
#post.destroy

#post = Post.new
#post.title = "Hello, Hello!!"
#post.body = "ajajajaja"
#post.save

#posts = Post.find(:all, :conditions => "title = 'a'", :limit => 1)
#p posts[0]
