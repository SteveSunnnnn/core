#include "core/simulation/CommandQueue.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace core {

std::uint64_t CommandQueue::enqueue(CommandType type, CountryId country, double value) {
    if (next_sequence_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("command sequence exhausted");
    if (!std::isfinite(value)) throw std::invalid_argument("command value must be finite");
    const auto seq = next_sequence_++;
    queue_.push_back(Command{seq, type, country, value});
    return seq;
}

void CommandQueue::apply_all(World& world) {
    // Validate the entire pending batch first. A malformed network/mod command
    // must not partially mutate authoritative state before failing.
    for (std::size_t i = read_; i < queue_.size(); ++i) {
        const auto& cmd = queue_[i];
        if (!cmd.country.valid() || static_cast<std::size_t>(cmd.country.value()) >= world.countries.size())
            throw std::out_of_range("command references invalid CountryId");
        if (!std::isfinite(cmd.value)) throw std::invalid_argument("command value must be finite");
        switch (cmd.type) {
            case CommandType::SetTaxRate:
            case CommandType::AddTreasury:
                break;
            default:
                throw std::invalid_argument("unknown command type");
        }
    }

    for (; read_ < queue_.size(); ++read_) {
        const auto& cmd = queue_[read_];
        switch (cmd.type) {
            case CommandType::SetTaxRate: world.countries.set_tax_rate(cmd.country, cmd.value); break;
            case CommandType::AddTreasury: world.countries.add_treasury(cmd.country, cmd.value); break;
            default: break; // validated above
        }
    }
    queue_.clear();
    read_ = 0;
}

} // namespace core
