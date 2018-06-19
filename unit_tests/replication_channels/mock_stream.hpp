#ifndef __MOCK_STREAM_HPP
#define __MOCK_STREAM_HPP

#include "cubstream.hpp"

class mock_stream : public cubstream::stream
{
  public:

    mock_stream ()
    {
      write_buffer = (char *) malloc (5000 * sizeof (int));
      last_position = 0;
    }

    ~mock_stream()
    {
      free (write_buffer);
    }

    void produce (const size_t amount)
    {
      m_last_committed_pos += amount;
    }

    int write (const size_t byte_count, cubstream::stream::write_func_t &write_action) override
    {
      int err;

      err = write_action (last_position, write_buffer, byte_count);
      if (err == NO_ERRORS)
	{
	  last_position += byte_count;
	}
      return err;
    }

    int read (const cubstream::stream_position first_pos, const size_t byte_count,
	      cubstream::stream::read_func_t &read_action) override
    {
      char *ptr = (char *) malloc (byte_count);
      int err = NO_ERROR;

      for (std::size_t i = 0; i < byte_count; i += sizeof (int))
	{
	  * ((int *) (ptr + i)) = first_pos / sizeof (int) + i / sizeof (int);
	}

      err = read_action (ptr, byte_count);
      free (ptr);

      return err;
    }

    int read_partial (const cubstream::stream_position first_pos, const size_t byte_count, size_t &actual_read_bytes,
		      read_partial_func_t &read_partial_action) override
    {
      assert (false);
      return NO_ERROR;
    }

    char *write_buffer;
    cubstream::stream_position last_position;
};

#endif /* __MOCK_STREAM_HPP */
