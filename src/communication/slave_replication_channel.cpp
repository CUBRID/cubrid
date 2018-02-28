#include "slave_replication_channel.hpp"

#include "connection_globals.h"
#include "connection_sr.h"
#include "thread_manager.hpp"
#include "thread_entry_task.hpp"
#include "thread_looper.hpp"
#include "system_parameter.h"

#if defined (WINDOWS)
#include "wintcp.h"
#endif

slave_replication_channel *slave_replication_channel::singleton = NULL;
std::mutex slave_replication_channel::singleton_mutex;

slave_replication_channel::slave_replication_channel (const std::string &hostname,
    const std::string &master_server_name, int port) :
                                                      slave_daemon (NULL),
                                                      cub_server_master_channel (hostname.c_str (),
                                                                                 port,
                                                                                 SERVER_REQUEST_CONNECT_NEW_SLAVE,
                                                                                 master_server_name.c_str ())
{
  _er_log_debug (ARG_FILE_LINE, "init slave_replication_channel hostname=%s\n", hostname.c_str ());
}

slave_replication_channel::~slave_replication_channel()
{
  cubthread::get_manager()->destroy_daemon (slave_daemon);

  _er_log_debug (ARG_FILE_LINE, "destroy slave_replication_channel \n");
}

int slave_replication_channel::connect_to_cub_server_master ()
{
  int rc = NO_ERRORS;

  rc = cub_server_master_channel.connect ();
  if (rc != NO_ERRORS)
    {
      assert (false);
      return rc;
    }

  return NO_ERRORS;
}

void slave_replication_channel::init (const std::string &hostname, const std::string &server_name, int port)
{
  if (singleton == NULL)
    {
      std::lock_guard<std::mutex> guard (singleton_mutex);
      if (singleton == NULL)
	{
	  singleton = new slave_replication_channel (hostname, server_name, port);
	}
    }
}

void slave_replication_channel::reset_singleton ()
{
  if (singleton == NULL)
    {
      return;
    }

  delete singleton;
  singleton = NULL;
}

slave_replication_channel &slave_replication_channel::get_channel ()
{
  return *singleton;
}

int slave_replication_channel::start_daemon (const cubthread::looper &loop, cubthread::entry_task *task)
{
  cubthread::manager *session_manager = cubthread::get_manager ();

  if (slave_daemon != NULL)
    {
      session_manager->destroy_daemon (slave_daemon);
      slave_daemon = NULL;
    }

  slave_daemon = session_manager->create_daemon (loop, task);

  if (slave_daemon == NULL)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

communication_channel &slave_replication_channel::get_cub_server_master_channel ()
{
  return cub_server_master_channel;
}

slave_dummy_send_msg::slave_dummy_send_msg (slave_replication_channel *ch) : channel (ch)
{
  if (GETHOSTNAME (this_hostname, MAXHOSTNAMELEN) != 0)
    {
      _er_log_debug (ARG_FILE_LINE, "unable to find this computer's hostname\n");
      strcpy (this_hostname, "unknown");
    }
  this_hostname[MAXHOSTNAMELEN-1] = '\0';
}

void slave_dummy_send_msg::execute (cubthread::entry &context)
{
  if (channel->get_cub_server_master_channel ().is_connection_alive ())
    {
      int rc = channel->get_cub_server_master_channel ().send (std::string ("hello from ") + this_hostname);
      if (rc == ERROR_ON_WRITE || rc == ERROR_WHEN_READING_SIZE)
	{
	  /* this probably means that the connection was closed */
	  slave_replication_channel::reset_singleton();
	}
      else if (rc != NO_ERRORS)
	{
	  assert (false);
	}
      _er_log_debug (ARG_FILE_LINE, "slave::execute:" "sent:hello\n");
    }
}
