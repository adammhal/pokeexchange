#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "pokex/types.hpp"

namespace pokex::detail {

// Shared machinery for v2 and v3: an array-indexed price ladder whose levels are
// intrusive doubly-linked lists over a preallocated object pool.
//
// v2 and v3 differ ONLY in how they find an order by id, so that difference is
// the only thing left in their own files. Everything structural lives here.
//
// Nodes are addressed by index, never by pointer. That is what lets the pool
// grow with a plain vector reallocation without invalidating every level's
// links, and it also halves the size of a link on a 64-bit machine.
using NodeIndex = std::uint32_t;
inline constexpr NodeIndex kNil = 0xFFFFFFFFu;

struct Node {
  Order order{};
  NodeIndex next{kNil};
  NodeIndex prev{kNil};
};

// A price level is two indices. 8 bytes, versus a std::deque's ~48 plus a heap
// block, which is why 65,536 of them per side is affordable.
struct Level {
  NodeIndex head{kNil};
  NodeIndex tail{kNil};
};

class LadderPool {
 public:
  explicit LadderPool(std::size_t initial_capacity = 4096)
      : bids_(kPriceLevels), asks_(kPriceLevels) {
    bid_occ_.fill(0);
    ask_occ_.fill(0);
    pool_.reserve(initial_capacity);
  }

  // ------------------------------------------------------------------- pool
  // Takes a slot from the free list. Only ever touches the heap when the pool
  // has to grow, which after warm-up is never: the free list recycles slots as
  // orders leave. No allocation on the hot path is the whole point of v2.
  NodeIndex acquire(const Order& order) {
    NodeIndex idx;
    if (free_head_ != kNil) {
      idx = free_head_;
      free_head_ = pool_[idx].next;
    } else {
      idx = static_cast<NodeIndex>(pool_.size());
      pool_.emplace_back();
    }
    pool_[idx] = Node{order, kNil, kNil};
    ++live_;
    return idx;
  }

  void release(NodeIndex idx) {
    pool_[idx].next = free_head_;
    free_head_ = idx;
    --live_;
  }

  Node& node(NodeIndex i) { return pool_[i]; }
  const Node& node(NodeIndex i) const { return pool_[i]; }
  std::size_t live() const { return live_; }
  std::size_t pool_size() const { return pool_.size(); }

  // ------------------------------------------------------------------- lists
  void push_back(NodeIndex idx) {
    Node& n = pool_[idx];
    const Side side = n.order.side;
    Level& lv = level(side, n.order.price);
    n.prev = lv.tail;
    n.next = kNil;
    if (lv.tail != kNil) pool_[lv.tail].next = idx;
    else lv.head = idx;
    lv.tail = idx;

    set_bit(side, n.order.price);
    if (side == Side::Buy) {
      if (best_bid_ == kNoPrice || n.order.price > best_bid_) best_bid_ = n.order.price;
    } else {
      if (best_ask_ == kNoPrice || n.order.price < best_ask_) best_ask_ = n.order.price;
    }
  }

  // O(1) removal from the middle of a level. This is what an intrusive list
  // buys: no search, no shifting, just two pointer writes.
  void unlink(NodeIndex idx) {
    Node& n = pool_[idx];
    const Side side = n.order.side;
    const Price price = n.order.price;
    Level& lv = level(side, price);

    if (n.prev != kNil) pool_[n.prev].next = n.next;
    else lv.head = n.next;
    if (n.next != kNil) pool_[n.next].prev = n.prev;
    else lv.tail = n.prev;
    n.next = n.prev = kNil;

    if (lv.head == kNil) {
      clear_bit(side, price);
      const Price best = (side == Side::Buy) ? best_bid_ : best_ask_;
      if (price == best) refresh_best(side, price);
    }
  }

  // ------------------------------------------------------------------ lookup
  std::optional<Price> best(Side side) const {
    const Price p = (side == Side::Buy) ? best_bid_ : best_ask_;
    if (p == kNoPrice) return std::nullopt;
    return p;
  }

  NodeIndex front_index(Side side) const {
    const Price p = (side == Side::Buy) ? best_bid_ : best_ask_;
    return level(side, p).head;
  }

  Level& level(Side s, Price p) {
    return (s == Side::Buy) ? bids_[static_cast<std::size_t>(p)]
                            : asks_[static_cast<std::size_t>(p)];
  }
  const Level& level(Side s, Price p) const {
    return (s == Side::Buy) ? bids_[static_cast<std::size_t>(p)]
                            : asks_[static_cast<std::size_t>(p)];
  }

  template <typename F>
  void for_each(Side side, F&& fn) const {
    std::optional<Price> p = best(side);
    while (p) {
      for (NodeIndex i = level(side, *p).head; i != kNil; i = pool_[i].next)
        fn(pool_[i].order);
      p = next_level(side, *p);
    }
  }

 private:
  using Word = std::uint64_t;
  static constexpr Price kNoPrice = 0;  // kMinPrice is 1, so 0 is a safe sentinel
  static constexpr std::size_t kWords = (kPriceLevels + 63) / 64;
  using Bitmask = std::array<Word, kWords>;

  Bitmask& occ(Side s) { return (s == Side::Buy) ? bid_occ_ : ask_occ_; }
  const Bitmask& occ(Side s) const { return (s == Side::Buy) ? bid_occ_ : ask_occ_; }

  void set_bit(Side s, Price p) {
    occ(s)[static_cast<std::size_t>(p) >> 6] |= (Word{1} << (static_cast<unsigned>(p) & 63u));
  }
  void clear_bit(Side s, Price p) {
    occ(s)[static_cast<std::size_t>(p) >> 6] &= ~(Word{1} << (static_cast<unsigned>(p) & 63u));
  }

  static std::optional<Price> scan_up(const Bitmask& w, Price from) {
    if (from > kMaxPrice) return std::nullopt;
    std::size_t i = static_cast<std::size_t>(from) >> 6;
    Word bits = w[i] & (~Word{0} << (static_cast<unsigned>(from) & 63u));
    for (;;) {
      if (bits)
        return static_cast<Price>(i * 64 + static_cast<std::size_t>(std::countr_zero(bits)));
      if (++i >= kWords) return std::nullopt;
      bits = w[i];
    }
  }

  static std::optional<Price> scan_down(const Bitmask& w, Price from) {
    if (from < kMinPrice) return std::nullopt;
    std::size_t i = static_cast<std::size_t>(from) >> 6;
    const unsigned shift = 63u - (static_cast<unsigned>(from) & 63u);
    Word bits = (w[i] << shift) >> shift;
    for (;;) {
      if (bits)
        return static_cast<Price>(i * 64 + 63 - static_cast<std::size_t>(std::countl_zero(bits)));
      if (i == 0) return std::nullopt;
      bits = w[--i];
    }
  }

  std::optional<Price> next_level(Side side, Price from) const {
    if (side == Side::Buy) {
      if (from <= kMinPrice) return std::nullopt;
      return scan_down(bid_occ_, from - 1);
    }
    if (from >= kMaxPrice) return std::nullopt;
    return scan_up(ask_occ_, from + 1);
  }

  void refresh_best(Side side, Price emptied) {
    const Price value = next_level(side, emptied).value_or(kNoPrice);
    if (side == Side::Buy) best_bid_ = value;
    else best_ask_ = value;
  }

  std::vector<Node> pool_{};
  NodeIndex free_head_{kNil};
  std::size_t live_{0};

  std::vector<Level> bids_;
  std::vector<Level> asks_;
  Bitmask bid_occ_{};
  Bitmask ask_occ_{};
  Price best_bid_{kNoPrice};
  Price best_ask_{kNoPrice};
};

}  // namespace pokex::detail
