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

/*
 * test_scope_exit.cpp - what a scope_exit guard promises to the code that uses it
 *
 * - the callback runs once when the guard leaves its scope, and not at all after release ()
 * - a moved guard hands the callback over; only the destination runs it
 * - guards run in the reverse order of their declaration, like any local object
 * - the type is move-only, and the noexcept of the constructors and the destructor follows the callable
 */

#include "catch2/catch.hpp"

#include "scope_exit.hpp"
#include "scope_exit.hpp"	/* included twice on purpose: the header guard must make the second one empty */

#include <string>
#include <type_traits>
#include <utility>

namespace
{
  struct may_throw_callable
  {
    void operator() () {}
  };

  struct nothrow_callable
  {
    void operator() () noexcept {}
  };

  struct throwing_move_callable
  {
    throwing_move_callable () = default;
    throwing_move_callable (throwing_move_callable &&) noexcept (false) {}
    void operator() () noexcept {}
  };

  auto plain_lambda = [] () {};
  using lambda_t = decltype (plain_lambda);
}

TEST_CASE ("the callback runs once when the guard leaves its scope", "[scope_exit]")
{
  int calls = 0;

  SECTION ("class template argument deduction from a lambda")
  {
    {
      scope_exit guard ([&calls] ()
      {
	++calls;
      });
      REQUIRE (calls == 0);
      REQUIRE (guard.engaged ());
    }
    REQUIRE (calls == 1);
  }

  SECTION ("make_scope_exit")
  {
    {
      auto guard = make_scope_exit ([&calls] ()
      {
	++calls;
      });
      REQUIRE (calls == 0);
    }
    REQUIRE (calls == 1);
  }

  SECTION ("brace initialization, as log_recovery.c writes it")
  {
    {
      scope_exit guard {[&calls] { ++calls; }};
    }
    REQUIRE (calls == 1);
  }

  SECTION ("a function pointer")
  {
    static int fp_calls;
    fp_calls = 0;
    void (*fp) () = [] ()
    {
      ++fp_calls;
    };
    {
      scope_exit guard (std::move (fp));
    }
    REQUIRE (fp_calls == 1);
  }
}

TEST_CASE ("the callback sees the state at scope exit, not at construction", "[scope_exit]")
{
  int seen = -1;
  int value = 1;
  {
    scope_exit guard ([&seen, &value] ()
    {
      seen = value;
    });
    value = 2;
  }
  REQUIRE (seen == 2);
}

TEST_CASE ("release () keeps the callback from running", "[scope_exit]")
{
  int calls = 0;
  {
    scope_exit guard ([&calls] ()
    {
      ++calls;
    });
    guard.release ();
    REQUIRE_FALSE (guard.engaged ());
    guard.release ();		/* a second release is harmless */
  }
  REQUIRE (calls == 0);
}

TEST_CASE ("a moved guard runs the callback once, from the destination", "[scope_exit]")
{
  int calls = 0;
  {
    auto source = make_scope_exit ([&calls] ()
    {
      ++calls;
    });
    {
      auto destination = std::move (source);
      REQUIRE_FALSE (source.engaged ());
      REQUIRE (destination.engaged ());
    }
    REQUIRE (calls == 1);
  }
  REQUIRE (calls == 1);

  SECTION ("moving a released guard moves the released state")
  {
    calls = 0;
    {
      auto source = make_scope_exit ([&calls] ()
      {
	++calls;
      });
      source.release ();
      auto destination = std::move (source);
      REQUIRE_FALSE (destination.engaged ());
    }
    REQUIRE (calls == 0);
  }
}

TEST_CASE ("the callable is moved into the guard, not copied", "[scope_exit]")
{
  std::string payload (64, 'x');	/* longer than the small string buffer, so a copy would allocate */
  const char *buffer = payload.data ();
  const char *seen = nullptr;
  {
    scope_exit guard ([p = std::move (payload), &seen] ()
    {
      seen = p.data ();
    });
    auto moved = std::move (guard);
  }
  REQUIRE (seen == buffer);
}

TEST_CASE ("guards run in the reverse order of their declaration", "[scope_exit]")
{
  std::string order;
  {
    scope_exit first ([&order] ()
    {
      order += '1';
    });
    scope_exit second ([&order] ()
    {
      order += '2';
    });
    scope_exit third ([&order] ()
    {
      order += '3';
    });
  }
  REQUIRE (order == "321");
}

TEST_CASE ("a callback that unlocks on every return path", "[scope_exit]")
{
  /* the shape of pgbuf_latch_bcb_upon_fix (): the bcb mutex is released on every return path */
  int locked = 0;
  auto work = [&locked] (bool early) -> int
  {
    ++locked;
    scope_exit unlock ([&locked] ()
    {
      --locked;
    });
    if (early)
      {
	return 1;
      }
    return 2;
  };

  REQUIRE (work (true) == 1);
  REQUIRE (locked == 0);
  REQUIRE (work (false) == 2);
  REQUIRE (locked == 0);
}

TEST_CASE ("the guard is move-only and its noexcept follows the callable", "[scope_exit]")
{
  using guard_t = scope_exit<lambda_t>;

  /* nothrow_callable can be copied and assigned, so only scope_exit itself can forbid it */
  using assignable_guard_t = scope_exit<nothrow_callable>;
  STATIC_REQUIRE_FALSE (std::is_copy_constructible_v<assignable_guard_t>);
  STATIC_REQUIRE_FALSE (std::is_copy_assignable_v<assignable_guard_t>);
  STATIC_REQUIRE_FALSE (std::is_move_assignable_v<assignable_guard_t>);
  STATIC_REQUIRE (std::is_move_constructible_v<assignable_guard_t>);
  STATIC_REQUIRE_FALSE (std::is_default_constructible_v<assignable_guard_t>);
  STATIC_REQUIRE_FALSE (std::is_copy_constructible_v<guard_t>);

  STATIC_REQUIRE (std::is_same_v<decltype (make_scope_exit (std::declval<lambda_t> ())), guard_t>);

  /* the destructor is noexcept only when the callable is; a lambda without noexcept, like every user's, is not */
  STATIC_REQUIRE (std::is_nothrow_destructible_v<scope_exit<nothrow_callable>>);
  STATIC_REQUIRE_FALSE (std::is_nothrow_destructible_v<scope_exit<may_throw_callable>>);
  STATIC_REQUIRE_FALSE (std::is_nothrow_destructible_v<guard_t>);

  /* the type traits count the destructor too, so the constructors are probed with callables whose call is noexcept */
  STATIC_REQUIRE (std::is_nothrow_constructible_v<scope_exit<nothrow_callable>, nothrow_callable &&>);
  STATIC_REQUIRE_FALSE (std::is_nothrow_constructible_v<scope_exit<throwing_move_callable>, throwing_move_callable &&>);
  STATIC_REQUIRE (std::is_nothrow_move_constructible_v<scope_exit<nothrow_callable>>);
  STATIC_REQUIRE_FALSE (std::is_nothrow_move_constructible_v<scope_exit<throwing_move_callable>>);
  STATIC_REQUIRE_FALSE (std::is_nothrow_move_constructible_v<guard_t>);
}
