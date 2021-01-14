/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include "resource_tracker.hpp"
#include "critical_section_tracker.hpp"

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
static void test_csect (void);

int
main (int, char **)
{
  cubbase::restrack_set_suppress_assert (true);

  test_push_pop ();
  test_leaks ();
  test_amount ();
  test_abort ();
  test_csect ();
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
	rt.increment (ARG_FILE_LINE, (int) idx);
      }
    rt.pop_track ();
  }
  check_no_error ();
}

void
test_csect (void)
{
  // enabled
  cubsync::critical_section_tracker cst = { true };

  // test no conflict => no error
  {
    cst.start ();

    // enter as reader and exit
    cst.on_enter_as_reader (0);
    cst.on_exit (0);

    // enter as writer and exit
    cst.on_enter_as_writer (0);
    cst.on_exit (0);

    // enter as reader, promote, demote, exit
    cst.on_enter_as_reader (0);
    cst.on_promote (0);
    cst.on_demote (0);
    cst.on_exit (0);

    cst.stop ();
  }
  check_no_error ();

  // leak error
  {
    cst.start ();

    cst.on_enter_as_reader (0);

    cst.stop ();
  }
  check_has_error ();

  // re-enter as reader error
  {
    cst.start ();

    cst.on_enter_as_reader (0);
    cst.on_enter_as_reader (0);
    cst.on_exit (0);
    cst.on_exit (0);

    cst.stop ();
  }
  check_has_error ();

  // re-enter as writer over reader error
  {
    cst.start ();

    cst.on_enter_as_reader (0);
    cst.on_enter_as_writer (0);
    cst.on_exit (0);
    cst.on_exit (0);

    cst.stop ();
  }
  check_has_error ();

  // re-enter as writer over writer is ok
  {
    cst.start ();

    cst.on_enter_as_writer (0);
    cst.on_enter_as_writer (0);
    cst.on_exit (0);
    cst.on_exit (0);

    cst.stop ();
  }
  check_no_error ();

  // re-enter as reader over writer is ok
  {
    cst.start ();

    cst.on_enter_as_writer (0);
    cst.on_enter_as_reader (0);
    cst.on_exit (0);
    cst.on_exit (0);

    cst.stop ();
  }
  check_no_error ();

  // promoting write is not ok
  {
    cst.start ();

    cst.on_enter_as_writer (0);
    cst.on_promote (0);
    cst.on_exit (0);

    cst.stop ();
  }
  check_has_error ();

  // promoting no lock is not ok
  {
    cst.start ();

    cst.on_promote (0);
    cst.on_exit (0);

    cst.stop ();
  }
  check_has_error ();

  // demoting reader is not ok
  {
    cst.start ();

    cst.on_enter_as_reader (0);
    cst.on_demote (0);
    cst.on_exit (0);

    cst.stop ();
  }
  check_has_error ();

  // entering as reader after demotion is ok
  {
    cst.start ();

    cst.on_enter_as_writer (0);
    cst.on_demote (0);
    cst.on_enter_as_reader (0);
    cst.on_exit (0);
    cst.on_exit (0);

    cst.stop ();
  }
  check_no_error ();

  // interdependencies

  // CSECT_LOCATOR_SR_CLASSNAME_TABLE after CSECT_CT_OID_TABLE is not allowed
  {
    cst.start ();

    cst.on_enter_as_reader (CSECT_CT_OID_TABLE);
    cst.on_enter_as_reader (CSECT_LOCATOR_SR_CLASSNAME_TABLE);
    cst.on_exit (CSECT_LOCATOR_SR_CLASSNAME_TABLE);
    cst.on_exit (CSECT_CT_OID_TABLE);

    cst.stop ();
  }
  check_has_error ();
}
