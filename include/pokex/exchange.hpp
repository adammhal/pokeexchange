#pragma once
#include <cstddef>
#include <variant>
#include <vector>

#include "pokex/book_concept.hpp"
#include "pokex/events.hpp"
#include "pokex/matching.hpp"
#include "pokex/types.hpp"

namespace pokex {

// One book per instrument, and a router in front of them.
//
// MatchingEngine stays exactly what it was: a single instrument and nothing
// else. That is deliberate. It is the thing the benchmark measures and the
// thing the fuzz tests hammer, and making it multi-instrument would have put an
// instrument lookup on the hot path of every message for the sake of a concern
// that belongs one layer up.
//
// Sequence numbers therefore run per instrument, which is correct rather than
// merely convenient: time priority is only ever compared between two orders in
// the same book, so a shared counter would be a contention point buying
// nothing.
//
// Emitted events are tagged with their instrument by this layer, so Event
// itself stays lean. Routing information belongs to the transport, not to the
// event.
template <Book B>
class Exchange {
 public:
  explicit Exchange(std::size_t instrument_count = 1) : books_(instrument_count) {}

  // `emit` is called as emit(InstrumentId, const Event&).
  template <typename Emit>
  void submit(const Command& command, Emit&& emit) {
    const InstrumentId instrument = instrument_of(command);
    if (static_cast<std::size_t>(instrument) >= books_.size()) {
      emit(instrument, Event{Rejected{order_id_of(command), RejectReason::UnknownInstrument}});
      return;
    }
    books_[instrument].submit(
        command, [&emit, instrument](const Event& e) { emit(instrument, e); });
  }

  std::size_t instruments() const { return books_.size(); }

  const MatchingEngine<B>& engine(InstrumentId i) const {
    return books_[static_cast<std::size_t>(i)];
  }
  MatchingEngine<B>& engine(InstrumentId i) {
    return books_[static_cast<std::size_t>(i)];
  }

 private:
  static InstrumentId instrument_of(const Command& c) {
    return std::visit([](const auto& v) { return v.instrument; }, c);
  }
  static OrderId order_id_of(const Command& c) {
    return std::visit([](const auto& v) { return v.id; }, c);
  }

  std::vector<MatchingEngine<B>> books_;
};

}  // namespace pokex
