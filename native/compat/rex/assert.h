#pragma once

#include <cassert>

#define assert_true(x, ...) assert(x)
#define assert_false(x, ...) assert(!(x))
#define assert_zero(x, ...) assert((x) == 0)
#define assert_not_zero(x, ...) assert((x) != 0)
#define assert_null(x, ...) assert((x) == nullptr)
#define assert_not_null(x, ...) assert((x) != nullptr)
#define assert_always(...) assert(false)
#define assert_unhandled_case(v) assert(false)
#define assert_mask(x, m) assert(((x) & ~(m)) == 0)
#define static_assert_size(type, size) static_assert(sizeof(type) == (size))
