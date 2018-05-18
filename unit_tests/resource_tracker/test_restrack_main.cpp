/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#include "resource_tracker.hpp"

using test_restrack = cubbase::resource_tracker<unsigned>;
const std::size_t DEFAULT_MAX_ITEMS = 8;
const char *DEFAULT_TRACK_NAME = "test_restrack";
const char *DEFAULT_RES_NAME = "res";

#define DEFAULT_CT_ARGS DEFAULT_TRACK_NAME, true, DEFAULT_MAX_ITEMS, DEFAULT_RES_NAME, 1
#define DEFAULT_DISABLED_CT_ARGS DEFAULT_TRACK_NAME, false, DEFAULT_MAX_ITEMS, DEFAULT_RES_NAME, 1
#define ARG_FILE_LINE __FILE__, __LINE__

static void check_has_error (void)
{
  assert (cubbase::restrack_pop_error ());
}

static void check_no_error (void)
{
  assert (!cubbase::restrack_pop_error ());
}

static void test_push_pop (void);
static void test_leaks (void);
static void test_amount (void);
static void test_abort (void);

int
main (int, char **)
{
  cubbase::restrack_set_suppress_assert (true);

  test_push_pop ();
  test_leaks ();
  test_amount ();
  test_abort ();
}

//////////////////////////////////////////////////////////////////////////

void
test_push_pop (void)
{
  // normal push/pop
  {
    test_restrack rt (DEFAULT_CT_ARGS);
    rt.push_track ();
    rt.pop_track ();
  }
  check_no_error ();
  // forget pop
  {
    test_restrack rt (DEFAULT_CT_ARGS);
    rt.push_track ();
  }
  check_has_error ();
  // forget push
  {
    test_restrack rt (DEFAULT_CT_ARGS);
    rt.pop_track ();
  }
  check_has_error ();
  // double push
  {
    test_restrack rt (DEFAULT_CT_ARGS);
    rt.push_track ();
    rt.push_track ();
    rt.pop_track ();
    rt.pop_track ();
  }
  check_no_error ();
  // push/pop error not detected when disabled
  {
    test_restrack rt (DEFAULT_DISABLED_CT_ARGS);
    rt.push_track ();
  }
  check_no_error ();
}

void
test_leaks (void)
{
  // no leak
  {
    test_restrack rt (DEFAULT_CT_ARGS);
    rt.push_track ();
    rt.increment (ARG_FILE_LINE, 0);
    rt.decrement (0);
    rt.pop_track ();
  }
  check_no_error ();
  // leak
  {
    test_restrack rt (DEFAULT_CT_ARGS);
    rt.push_track ();
    rt.increment (ARG_FILE_LINE, 0);
    rt.pop_track ();
  }
  check_has_error ();
  // leak, but not detected when disabled
  {
    test_restrack rt (DEFAULT_DISABLED_CT_ARGS);
    rt.push_track ();
    rt.increment (ARG_FILE_LINE, 0);
    rt.pop_track ();
  }
  check_no_error ();
  // leak
  {
    test_restrack rt (DEFAULT_CT_ARGS);
    rt.push_track ();
    rt.increment (ARG_FILE_LINE, 0);
    rt.push_track ();
    rt.increment (ARG_FILE_LINE, 1);
    rt.decrement (1);
    rt.pop_track ();
    rt.pop_track ();
  }
  check_has_error ();
  // free on different level
  {
    test_restrack rt (DEFAULT_CT_ARGS);
    rt.push_track ();
    rt.increment (ARG_FILE_LINE, 0);
    rt.push_track ();
    rt.increment (ARG_FILE_LINE, 1);
    rt.decrement (1);
    rt.decrement (0);
    rt.pop_track ();
    rt.pop_track ();
  }
  check_has_error ();
  // add on each level, and remove correctly
  {
    test_restrack rt (DEFAULT_CT_ARGS);
    rt.push_track ();
    rt.increment (ARG_FILE_LINE, 0);
    rt.push_track ();
    rt.increment (ARG_FILE_LINE, 1);
    rt.decrement (1);
    rt.pop_track ();
    rt.decrement (0);
    rt.pop_track ();
  }
  check_no_error ();
}

void
test_amount (void)
{
  // test exceeding max_amount (default = 1)
  {
    test_restrack rt (DEFAULT_CT_ARGS);
    rt.push_track ();
    rt.increment (ARG_FILE_LINE, 0, 2);
    rt.decrement (0, 2);
    rt.pop_track ();
  }
  check_has_error ();
  // test accumulation exceeding max_amount (default = 1)
  {
    test_restrack rt (DEFAULT_CT_ARGS);
    rt.push_track ();
    rt.increment (ARG_FILE_LINE, 0, 1);
    rt.increment (ARG_FILE_LINE, 0, 1);
    rt.decrement (0, 2);
    rt.pop_track ();
  }
  check_has_error ();
  // test disabled ignores errors
  {
    test_restrack rt (DEFAULT_DISABLED_CT_ARGS);
    rt.push_track ();
    rt.increment (ARG_FILE_LINE, 0, 2);
    rt.decrement (0, 2);
    rt.pop_track ();
  }
  check_no_error ();
  // test accumulation works
  {
    test_restrack rt (DEFAULT_TRACK_NAME, true, DEFAULT_MAX_ITEMS, DEFAULT_RES_NAME, 4);
    rt.push_track ();
    rt.increment (ARG_FILE_LINE, 0, 2);
    rt.increment (ARG_FILE_LINE, 0, 2);
    rt.decrement (0, 2);
    rt.decrement (0, 2);
    rt.pop_track ();
  }
  check_no_error ();
}

void
test_abort (void)
{
  // test not aborted; leak one resource
  {
    test_restrack rt (DEFAULT_CT_ARGS);
    rt.push_track ();
    for (unsigned idx = 0; idx < DEFAULT_MAX_ITEMS; idx++)
      {
	rt.increment (ARG_FILE_LINE, idx);
      }
    for (unsigned idx = 0; idx < DEFAULT_MAX_ITEMS - 1; idx++)
      {
	rt.decrement (idx);
      }
    rt.pop_track ();
  }
  check_has_error ();
  // test aborted; leak ignored
  {
    test_restrack rt (DEFAULT_CT_ARGS);
    rt.push_track ();
    for (std::size_t idx = 0; idx <= DEFAULT_MAX_ITEMS; idx++)
      {
	rt.increment (ARG_FILE_LINE, idx);
      }
    rt.pop_track ();
  }
  check_no_error ();
}
