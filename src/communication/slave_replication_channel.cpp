#include "slave_replication_channel.hpp"

#include "connection_globals.h"
#include "connection_sr.h"
#include "thread_manager.hpp"
#include "thread_entry_task.hpp"
#include "thread_looper.hpp"

slave_replication_channel *slave_replication_channel::singleton = NULL;

class slave_dummy_send_msg : public cubthread::entry_task
{
  public:
    slave_dummy_send_msg (slave_replication_channel *ch) : channel (ch)
    {
    }

    void execute (cubthread::entry &context)
    {
      channel->send (channel->get_master_conn_entry()->fd, "hello", replication_channel::get_max_timeout());
    }

    void retire ()
    {

    }
  private:
    slave_replication_channel *channel;
};

slave_replication_channel::slave_replication_channel(const std::string& hostname, const std::string &master_server_name, int port) : master_hostname (hostname),
                                                                                                                                     master_server_name (master_server_name),
                                                                                                                                     master_port (port)
{
  cubthread::manager *session_manager = cubthread::get_manager ();
  
  master_conn_entry = css_make_conn (-1);
  request_id = -1;
  
  slave_dummy = session_manager->create_daemon (cubthread::looper (std::chrono::seconds (1)),
		       new slave_dummy_send_msg (this));
}

slave_replication_channel::~slave_replication_channel()
{
  css_free_conn (master_conn_entry);
}

int slave_replication_channel::connect_to_master()
{
  int length = 0;

  if (master_conn_entry == NULL)
    {
      return ER_FAILED;
    }

  length = master_server_name.length ();

  if (css_common_connect (master_conn_entry,&request_id, master_hostname.c_str (),
                          SERVER_REQUEST_CONNECT_NEW_SLAVE, master_server_name.c_str (), length, css_Service_id))
    {
      return REQUEST_REFUSED;
    }

  return NO_ERRORS;
}

CSS_CONN_ENTRY *slave_replication_channel::get_master_conn_entry ()
{
  return master_conn_entry;
}

void slave_replication_channel::init (const std::string &hostname, const std::string &server_name, int port)
{
  if (singleton == NULL)
    {
      std::lock_guard<std::mutex> guard (replication_channel::singleton_mutex);
      if (singleton == NULL)
        {
          singleton = new slave_replication_channel (hostname, server_name, port);
        }
    }
}

void slave_replication_channel::reset_singleton ()
{
  delete singleton;
  singleton = NULL;
}
 
slave_replication_channel *slave_replication_channel::get_channel ()
{
  return singleton;
}
