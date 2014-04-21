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

require 'cubrid'

module DBI
  module DBD
    module CUBRID

      VERSION          = "0.1"
      USED_DBD_VERSION = "0.2"

      class Driver < DBI::BaseDriver
        
        def initialize
          super(USED_DBD_VERSION)
        end
        
        def connect(dbname, user, auth, attr)
          Database.new(dbname, user, auth, attr)
        end

        def default_user
          ['PUBLIC', '']
        end
      end

      class Database < DBI::BaseDatabase
        include Utils

        def initialize(dbname, user, auth, attr)
          hash = Utils.parse_params(dbname)

          if hash['dbname'].nil? and hash['database'].nil?
            raise DBI::InterfaceError, "must specify database"
          end

          hash['port'] = hash['port'].to_i unless hash['port'].nil? 

          @conn = Cubrid.connect(hash['dbname'] || hash['database'], hash['host'], hash['port'], user, auth)

        rescue Error => err
          raise DBI::OperationalError.new(err.message)
        end

        def disconnect
          @conn.close
        end

        def ping
          stmt = @conn.query('select 1 from db_root')
          return stmt.affected_rows == 1
        end

        def prepare(sql)
          Statement.new(@conn, sql)
        end

      #  def execute(sql, *bindvars) #using super
      #  end

      #  def do(sql, *bindvars) #using super
      #  end

        def tables
          tbls = []
          sql = "select class_name from db_class where is_system_class = 'NO'"
          @conn.query(sql) { |r|                    
            tbls << r[0].downcase
          }
          tbls
        end

        def columns(table)
          cols = []
          sql = "select * from db_attribute where class_name = '#{table}'"
          
          @conn.prepare(sql) do |stmt|
            stmt.execute
            stmt.each_hash do |c|
              hash = {}
              hash['name']       = c['attr_name'].downcase
              hash['type_name']  = c["data_type"].downcase
              hash['nullable']   = c['is_nullable'] == 'YES'
              hash['precision']  = c['prec']
              hash['scale']      = c['scale']
              hash['default']    = c['default_value']
              #hash['sql_type']   = sql_type
              
              #sql2 = 'select a.is_unique, a.is_primary_key from db_index a, db_index_key b \
              #        where a.index_name = b.index_name and b.class_name = ? and b.key_attr_name = ?'
              #@conn.prepare sql2 do |stmt2|
              #  cnt = stm2t.execute(table, c['attr_name'])
              #  if (cnt > 0) 
              #    row = stmt2.fetch_hash
              #    hash['indexed'] = True
              #    hash['unique']  = row['is_unique'] == 'YES'
              #    hash['primary'] = row['is_primary_key'] == 'YES'
              #  else
              #    hash['indexed'] = False
              #    hash['unique']  = False
              #    hash['primary'] = False
              #  end
              #end

              cols << hash
            end
          end
          cols
        end

        def commit
          @conn.commit
        end

        def rollback
          @conn.rollback
        end
        
        def quote(value)
          case value
          when String
            "'#{ value.gsub(/\\/){ '\\\\' }.gsub(/'/){ '\\\'' } }'" #TODO: check if right
          else
            super
          end
        end
      end # class Database


      class Statement < DBI::BaseStatement
        include Utils

        def initialize(conn, sql)
          @stmt = conn.prepare sql
        end

        def bind_param(param, value, attribs)
          @stmt.bind(param, value)
        end

        def execute
          @stmt.execute
        end

        def finish
          @stmt.close
        end

        def fetch
          @stmt.fetch
        end

        def column_info
          col_info = @stmt.get_result_info
        end

        def rows
          @stmt.affected_rows
        end
      end # class Statement
    end #Cubrid
  end #DBD
end #DBI

