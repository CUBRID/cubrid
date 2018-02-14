#ifndef _MASTER_REPLICATION_CHANNEL_MOCK_HPP
#define _MASTER_REPLICATION_CHANNEL_MOCK_HPP

#define SERVER_MODE
#include "master_replication_channel_manager.hpp"

#define NUM_OF_MSG_SENT 5

namespace master
{

	void init ();
	void finish ();

	class receive_from_slave_daemon_mock : public cubthread::entry_task
	{
		public:
		  receive_from_slave_daemon_mock (master_replication_channel *ch);
			void execute (cubthread::entry &context);

		private:
		  master_replication_channel *channel;
			int num_of_received_messages;
	};

}

#endif /* _MASTER_REPLICATION_CHANNEL_MOCK_HPP */
