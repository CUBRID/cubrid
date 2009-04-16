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

require 'active_record/connection_adapters/abstract_adapter'

begin
  require 'cubrid'

  module ActiveRecord
    class Base
      def self.cubrid_connection(config) # :nodoc:
        config = config.symbolize_keys
        host = config[:host] || 'localhost'
        port = config[:port] || 30000
        usr = config[:username] || 'public'
        pwd = config[:password] || ''

        if config.has_key?(:database)
          database = config[:database]
        else
          raise ArgumentError, 'No database specified. Missing argument: database.'
        end

        connection = Cubrid.connect(database, host, port, usr, pwd)
        ConnectionAdapters::CubridAdapter.new(connection, logger)
      end
    end

    module ConnectionAdapters
      class CubridAdapter < AbstractAdapter
        def initialize(connection, logger)
          super(connection, logger)
          @connection.auto_commit = true
        end

        def adapter_name()
          'CUBRID'
        end

        def supports_migrations? #:nodoc:
          true
        end

	def prefetch_primary_key?(table_name = nil)
	  true
	end

	def next_sequence_value(sequence_name)
	  select_value("select #{sequence_name}.next_value id from db_root")
	end

        def execute(sql, name = nil)
          rows_affected = 0
          @connection.prepare(sql) do |stmt|
            log(sql, name) do
              rows_affected = stmt.execute
            end
          end
          rows_affected
          rescue Exception
            raise ActiveRecord::StatementInvalid
        end

        def insert(sql, name = nil, pk = nil, id_value = nil, sequence_name = nil)
          execute(sql, name)
          id_value
        end
        
        def begin_db_transaction
          @connection.auto_commit = false
        end

        def commit_db_transaction
          @connection.commit
          @connection.auto_commit = true
        end
        
        def rollback_db_transaction
          @connection.rollback
          @connection.auto_commit = true
        end

        def quote_column_name(column_name)
          column_name
        end

        def quote_string(string)
          string.gsub(/'/, "''") # ' (for ruby-mode)
        end

        def quote(value, column = nil) #:nodoc:
          super  
        end

        def quoted_true
          '1'
        end

        def quoted_false
          '0'
        end

        def add_limit_offset!(sql, options)
          if limit = options[:limit]
            offset = options[:offset] || 0
            if offset > 0
              sql.replace "select * from (#{sql}) where rownum > #{limit} and rownum <= #{limit + offset}"
            else
              sql.replace "select * from (#{sql}) where rownum <= #{limit}"
            end  
          end
        end

        def default_sequence_name(table, column) #:nodoc:
          "#{table}_seq"
        end

	def empty_insert_statement(table_name)
	  "INSERT INTO #{table_name} DEFAULT VALUES"
	end

        def tables(name = nil)
          result = []
          sql = "select class_name from db_class where is_system_class = 'NO'"                   
          select(sql).each { |t| result << t["class_name"].downcase }
          result
        end

        def indexes(table_name, name = nil)
          sql = "select a.index_name, a.is_unique, b.key_attr_name from db_index a, db_index_key b where a.class_name = '#{table_name}' and a.class_name = b.class_name and a.index_name = b.index_name order by b.key_order" 
          indexes = []
          cur_index = nil
          
          select(sql).each do |r|
            if cur_index != r['index_name']
              indexes <<  IndexDefinition.new(table_name, r['index_name'], r['is_unique'] == 'YES', [])
              cur_index = r['index_name']
            end
            
            indexes.last.columns << r['key_attr_name']
          end
          
          indexes
        end

        def columns(table_name, name = nil)
          result = []
          sql = "select * from db_attribute where class_name = '#{table_name}'"                   
          select(sql).each do |c| 
            c_name = c['attr_name'].downcase
            c_default = c['default_value'] == 'NULL' ? nil : c['default_value']
            c_default.gsub!(/^'(.*)'$/, '\1') if !c_default.nil?
            c_type = c["data_type"].downcase
            c_type += "(#{c['prec']})" if !c['prec'].nil? && c['prec'] != 0
            result << Column.new(c_name, c_default, c_type, c['is_nullable'] == 'YES')
          end
          result
        end

        def native_database_types
          {
            :primary_key => 'int primary key',
            :string      => { :name => 'varchar(256)'},
            :text        => { :name => 'string'},
            :integer     => { :name => 'int' },
            :float       => { :name => 'float' },
            :datetime    => { :name => 'timestamp' },
            :timestamp   => { :name => 'timestamp' },
            :time        => { :name => 'time' },
            :date        => { :name => 'date' },
            :binary      => { :name => 'varbit', :limit => 32768 }, #TODO
            :boolean     => { :name => 'decimal', :limit => 1 }
          }
        end

        def active? #TODO
          select 'select 1 from db_root'
          true
        rescue Exception
          false
        end

        def reconnect!
          #TODO
          @connection.reconnect
        end

        def disconnect!
          @connection.close
        end

        def create_table(name, options = {}) #:nodoc:
          super(name, options)
	  seq_name = options[:sequence_name] || "#{name}_seq"
          execute "CREATE SERIAL #{name}_seq START WITH 1" unless options[:id] == false
        end

        def rename_table(name, new_name) #:nodoc:
          execute "RENAME TABLE #{name} AS #{new_name}"
          cur_serial = select_value("select #{sequence_name}.cur_value id from db_root")
          execute "CREATE SERIAL #{new_name}_seq START WITH #{cur_serial}"
          execute "DROP SERIAL #{name}_seq" rescue nil
        end  

        def drop_table(name) #:nodoc:
          super(name)
          execute "DROP SERIAL #{name}_seq" rescue nil
        end

        def remove_index(table_name, options = {}) #:nodoc:
            #TODO
          
        end

        def change_column_default(table_name, column_name, default) #:nodoc:
          execute "ALTER TABLE #{table_name} CHANGE #{column_name} DEFAULT #{quote(default)}"
        end

        def change_column(table_name, column_name, type, options = {}) #:nodoc:
          remove_column(table_name, column_name)
          execute "ALTER TABLE #{table_name} ADD COLUMN #{column_name}_tmp #{type}"
        end

        def rename_column(table_name, column_name, new_column_name) #:nodoc:
          execute "ALTER TABLE #{table_name} RENAME COLUMN #{column_name} AS #{new_column_name}"
        end

        def remove_column(table_name, column_name) #:nodoc:
          execute "ALTER TABLE #{table_name} DROP COLUMN #{column_name}"
        end

        def structure_dump #:nodoc:
            #TODO
          sql= "select b.* from db_class a, db_attribute b where a.is_system_class = 'NO' and a.class_name = b.class_name order by b.class_name, b=.def_order"
          structure = ""
          cur_class = nil
          
          select(sql).each do |r|
            if cur_class != r['class_name']
              structure <<  "create table #{r['class_name']};\n "
              structure <<  "create serial #{r['class_name']}_seq START WITH 1;\n "
              cur_class = r['class_name']
            end
            
            structure << "alter table #{cur_class} add attribute #{r['attr_name']} #{r['data_type']}"
            if r['data_type'] == 'NUMBER' 
              structure << "(#{r['prec']}, #{r['scale']})"
            end  
            if r['data_type'] == 'CHAR' or r['data_type'] == 'VARCHAR'or r['data_type'] == 'STRING'
              structure << "(#{r['prec']})"
            end  
            
            structure << " default #{['default_value']}" if row['default_value'] != 'NULL'
            structure << " NOT NULL"  if row['is_nullable'] == 'NO'
            structure << ";\n"
          end
          
          structure
        end

        def structure_drop #:nodoc:
            #TODO
        end

        def current_database
            @connection.db
        end    

        def recreate_database(name) #:nodoc:
            #TODO
        end
        
        def create_database(name) #:nodoc:
            #TODO
        end
        
        def drop_database(name) #:nodoc:
            #TODO
        end

        def select(sql, name = nil)
          rows = []

	  log(sql, name) do
            @connection.prepare(sql) do |stmt|
	      stmt.execute
              stmt.each_hash do |r|
                rows << r
              end
            end
          end

          rows
        end
      end
    end
  end
rescue LoadError
  # CUBRID driver is unavailable.
  module ActiveRecord # :nodoc:
    class Base
      def self.cubrid_connection(config) # :nodoc:
        # Set up a reasonable error message
        raise LoadError, "CUBRID Libraries could not be loaded."
      end
    end
  end
end
