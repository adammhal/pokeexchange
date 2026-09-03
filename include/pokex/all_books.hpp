#pragma once
// Every book version in one include, so tests and tools can be generic over the
// whole set. The matching logic in matching.hpp never changes; only these do.
#include "pokex/book_v0_map.hpp"
#include "pokex/book_v1_ladder.hpp"
#include "pokex/book_v2_pool.hpp"
#include "pokex/book_v3_hash.hpp"
