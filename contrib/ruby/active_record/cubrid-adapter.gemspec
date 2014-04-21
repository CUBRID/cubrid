Gem::Specification.new do |s|
  s.name = "activerecord-cubrid-adapter"
  s.version = "0.6"
  s.authors = "NHN"
  s.email = "cubrid_ruby@nhncorp.com"
  s.homepage ="http://dev.naver.com/projects/cubrid-ruby"
  s.summary = "CUBRID Data Adapter for ActiveRecord"
  s.platform = Gem::Platform::RUBY
  s.files = ["lib/active_record", "lib/active_record/connection_adapters", "lib/active_record/connection_adapters/cubrid_adapter.rb", "lib/cubrid_adapter.rb"]
  s.require_path = "lib"
  s.autorequire = "cubrid_adapter"
  s.add_dependency("activerecord", [">= 2.0.2"])
end
