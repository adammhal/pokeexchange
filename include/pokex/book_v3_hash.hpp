#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "pokex/book_concept.hpp"
#include "pokex/detail/ladder_pool.hpp"
#include "pokex/types.hpp"

namespace pokex {
namespace detail {

// An open-addressing hash table from OrderId straight to a pool index.
//
// Not std::unordered_map, because that is specified as a bucket array of linked
// lists: every lookup is a pointer chase into a separately allocated node, which
// is exactly the access pattern v1 and v2 spent their effort eliminating. Linear
// probing keeps the whole search inside one or two cache lines.
//
// Deletion uses backward shifting rather than tombstones. Tombstones would be
// simpler, but this table sees a cancel for roughly every order, so a tombstoned
// table would fill with corpses and probe lengths would grow without bound over
// a long session.
class IdIndex {
 public:
  explicit IdIndex(std::size_t capacity_pow2 = 1024) { reset(capacity_pow2); }

  bool contains(OrderId id) const { return find_slot(id) != kAbsent; }

  const NodeIndex* find(OrderId id) const {
    const std::size_t s = find_slot(id);
    return (s == kAbsent) ? nullptr : &slots_[s].value;
  }

  void insert(OrderId id, NodeIndex node) {
    if ((count_ + 1) * 2 > slots_.size()) grow();
    std::size_t i = home(id);
    while (slots_[i].used) {
      if (slots_[i].key == id) {  // overwrite; should not happen with unique ids
        slots_[i].value = node;
        return;
      }
      i = (i + 1) & mask_;
    }
    slots_[i] = Slot{id, node, true};
    ++count_;
  }

  bool erase(OrderId id) {
    const std::size_t s = find_slot(id);
    if (s == kAbsent) return false;
    erase_at(s);
    --count_;
    return true;
  }

  std::size_t size() const { return count_; }

 private:
  struct Slot {
    OrderId key{0};
    NodeIndex value{kNil};
    bool used{false};
  };

  static constexpr std::size_t kAbsent = static_cast<std::size_t>(-1);

  // Order ids are monotonic, so the low bits alone would cluster badly. This is
  // the splitmix64 finalizer: cheap, and it spreads sequential keys.
  static std::uint64_t mix(OrderId id) {
    std::uint64_t z = id + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }

  std::size_t home(OrderId id) const { return static_cast<std::size_t>(mix(id)) & mask_; }

  std::size_t find_slot(OrderId id) const {
    std::size_t i = home(id);
    while (slots_[i].used) {
      if (slots_[i].key == id) return i;
      i = (i + 1) & mask_;
    }
    return kAbsent;
  }

  // Backward-shift deletion. Walk forward from the hole; any entry whose home
  // position is not cyclically inside (hole, j] can be moved back into it.
  void erase_at(std::size_t hole) {
    std::size_t j = hole;
    for (;;) {
      j = (j + 1) & mask_;
      if (!slots_[j].used) break;
      const std::size_t k = home(slots_[j].key);
      const bool k_between = (hole <= j) ? (hole < k && k <= j) : (hole < k || k <= j);
      if (!k_between) {
        slots_[hole] = slots_[j];
        hole = j;
      }
    }
    slots_[hole] = Slot{};
  }

  void reset(std::size_t capacity_pow2) {
    slots_.assign(capacity_pow2, Slot{});
    mask_ = capacity_pow2 - 1;
    count_ = 0;
  }

  void grow() {
    std::vector<Slot> old = slots_;
    reset(slots_.size() * 2);
    for (const Slot& s : old)
      if (s.used) insert(s.key, s.value);
  }

  std::vector<Slot> slots_{};
  std::size_t mask_{0};
  std::size_t count_{0};
};

}  // namespace detail

// v3: O(1) cancel.
//
// The single most valuable change in the ladder, because cancels are roughly 90%
// of real message traffic. v1 and v2 both had to walk a price level looking for
// an id; this version hashes the id straight to its pool slot, and because the
// list is intrusive, unlinking from there is two pointer writes.
//
// Note what did NOT change: matching.hpp is untouched, and so is every rule.
// The shared test suite runs against this exactly as it runs against v0.
class BookV3Hash {
 public:
  std::optional<Price> best(Side side) const { return core_.best(side); }

  Order& front(Side side) { return core_.node(core_.front_index(side)).order; }

  void pop_front(Side side) {
    const detail::NodeIndex idx = core_.front_index(side);
    index_.erase(core_.node(idx).order.id);
    core_.unlink(idx);
    core_.release(idx);
  }

  void insert(const Order& order) {
    const detail::NodeIndex idx = core_.acquire(order);
    core_.push_back(idx);
    index_.insert(order.id, idx);
  }

  bool contains(OrderId id) const { return index_.contains(id); }

  std::optional<Order> cancel(OrderId id) {
    const detail::NodeIndex* node = index_.find(id);
    if (node == nullptr) return std::nullopt;
    const detail::NodeIndex idx = *node;   // one probe, no search
    const Order copy = core_.node(idx).order;
    core_.unlink(idx);                     // two pointer writes
    core_.release(idx);
    index_.erase(id);
    return copy;
  }

  // One probe, no search. The same index that makes cancel O(1) makes an
  // in-place modify O(1).
  Order* find(OrderId id) {
    const detail::NodeIndex* node = index_.find(id);
    if (node == nullptr) return nullptr;
    return &core_.node(*node).order;
  }

  // ------------------------------------------------- not part of the contract
  std::size_t size() const { return index_.size(); }

  std::uint64_t total_remaining(Side side) const {
    std::uint64_t total = 0;
    core_.for_each(side, [&](const Order& o) { total += o.remaining; });
    return total;
  }

  template <typename F>
  void walk(Side side, F&& fn) const {
    core_.walk(side, static_cast<F&&>(fn));
  }

  template <typename F>
  void for_each(Side side, F&& fn) const {
    core_.for_each(side, static_cast<F&&>(fn));
  }

 private:
  detail::LadderPool core_{};
  detail::IdIndex index_{};
};

static_assert(Book<BookV3Hash>, "BookV3Hash must satisfy the Book contract");

}  // namespace pokex
