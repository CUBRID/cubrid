#include "mock_stream.hpp"

#include <assert.h>
#include <error_code.h>

#include "communication_channel.hpp"

mock_stream::mock_stream ()
{
  write_buffer = (char *) malloc (cubcomm::MTU * cubtest::MAX_CYCLES);
  last_position = 0;
}

mock_stream::~mock_stream()
{
  free (write_buffer);
}

void mock_stream::produce (const size_t amount)
{
  m_last_committed_pos += amount;
}

int mock_stream::write (const size_t byte_count, cubstream::stream::write_func_t &write_action)
{
  int err;

  err = write_action (last_position, write_buffer + last_position, byte_count);
  if (err == NO_ERROR)
    {
      last_position += byte_count;
    }
  return err;
}

int mock_stream::read (const cubstream::stream_position first_pos, const size_t byte_count,
		       cubstream::stream::read_func_t &read_action)
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

int mock_stream::read_partial (const cubstream::stream_position first_pos, const size_t byte_count,
			       size_t &actual_read_bytes,
			       read_partial_func_t &read_partial_action)
{
  assert (false);
  return NO_ERROR;
}
