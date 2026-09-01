#pragma once

#include "core/economy/EconomySystem.hpp"

namespace core::economy_detail {

[[nodiscard]] inline EconomyAmount flow_for_workers(PopulationCount workers,
                                                     EconomyAmount per_1000) noexcept {
    return mul_div_nonnegative(static_cast<EconomyAmount>(workers), per_1000, 1000);
}

[[nodiscard]] inline EconomyAmount money_for_quantity(EconomyAmount quantity_milli,
                                                       EconomyPrice price_milli) noexcept {
    return mul_div_nonnegative(quantity_milli, price_milli, economy_scale);
}

} // namespace core::economy_detail
