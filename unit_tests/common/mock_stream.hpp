#ifndef __MOCK_STREAM_HPP
#define __MOCK_STREAM_HPP

#include "cubstream.hpp"

class mock_stream : public cubstream::stream
{
  public:

    mock_stream ();
    ~mock_stream();

    void produce (const size_t amount);
    int write (const size_t byte_count, cubstream::stream::write_func_t &write_action) override;
    int read (const cubstream::stream_position first_pos, const size_t byte_count,
	      cubstream::stream::read_func_t &read_action) override;
    int read_partial (const cubstream::stream_position first_pos, const size_t byte_count, size_t &actual_read_bytes,
		      read_partial_func_t &read_partial_action) override;

    char *write_buffer;
    cubstream::stream_position last_position;
};

#endif /* __MOCK_STREAM_HPP */
