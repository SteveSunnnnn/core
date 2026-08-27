#include "core/economy/ConstructionStore.hpp"
#include "core/simulation/World.hpp"

#include <algorithm>

namespace core {

ConstructionProjectId ConstructionStore::enqueue(ConstructionProjectInit init) {
    ConstructionProjectRecord rec;
    rec.id = ConstructionProjectId{next_id_++};
    rec.country = init.country;
    rec.province = init.province;
    rec.target_building = init.target_building;
    rec.kind = init.kind;
    rec.target_pm = init.target_pm;
    rec.monument_key_hash = init.monument_key_hash;
    rec.total_points_required = std::max(1u, init.total_points_required);
    rec.total_cost_milli = init.total_cost_milli;
    rec.progress_points = 0;
    rec.paid_cost_milli = 0;
    rec.weekly_progress_ppm = 0;
    rec.paused = false;

    // Assign priority at the tail of the country's queue
    std::uint32_t max_pri = 0;
    for (const auto& p : projects_) {
        if (p.country == init.country) {
            max_pri = std::max(max_pri, p.priority + 1);
        }
    }
    rec.priority = max_pri;
    projects_.push_back(rec);
    return rec.id;
}

ConstructionProjectId ConstructionStore::enqueue_expansion(CountryId country, BuildingId building,
                                                           std::uint32_t points,
                                                           EconomyAmount cost_milli) {
    ConstructionProjectInit init;
    init.country = country;
    init.target_building = building;
    init.kind = ConstructionKind::ExpandBuilding;
    init.total_points_required = points == 0 ? 100u : points;
    init.total_cost_milli = cost_milli;
    return enqueue(init);
}

ConstructionProjectId ConstructionStore::enqueue_pm_upgrade(CountryId country, BuildingId building,
                                                            ProductionMethodId target_pm,
                                                            std::uint32_t points,
                                                            EconomyAmount cost_milli) {
    ConstructionProjectInit init;
    init.country = country;
    init.target_building = building;
    init.kind = ConstructionKind::UpgradeProductionMethod;
    init.target_pm = target_pm;
    init.total_points_required = points == 0 ? 50u : points;
    init.total_cost_milli = cost_milli;
    return enqueue(init);
}

ConstructionProjectId ConstructionStore::enqueue_monument(CountryId country, ProvinceId province,
                                                          std::string_view monument_key,
                                                          std::uint32_t points,
                                                          EconomyAmount cost_milli) {
    ConstructionProjectInit init;
    init.country = country;
    init.province = province;
    init.kind = ConstructionKind::ConstructMonument;
    init.monument_key_hash = economy_stable_key(monument_key);
    init.total_points_required = points == 0 ? 500u : points;
    init.total_cost_milli = cost_milli;
    return enqueue(init);
}

bool ConstructionStore::cancel(ConstructionProjectId id) {
    const auto it = std::find_if(projects_.begin(), projects_.end(),
                                 [id](const auto& p) { return p.id == id; });
    if (it != projects_.end()) {
        projects_.erase(it);
        return true;
    }
    return false;
}

bool ConstructionStore::set_paused(ConstructionProjectId id, bool paused) {
    for (auto& p : projects_) {
        if (p.id == id) {
            p.paused = paused;
            return true;
        }
    }
    return false;
}

bool ConstructionStore::move_up(ConstructionProjectId id) {
    for (std::size_t i = 0; i < projects_.size(); ++i) {
        if (projects_[i].id == id) {
            // Find preceding project in the same country
            for (std::size_t j = i; j > 0; --j) {
                if (projects_[j - 1].country == projects_[i].country) {
                    std::swap(projects_[i].priority, projects_[j - 1].priority);
                    std::swap(projects_[i], projects_[j - 1]);
                    return true;
                }
            }
            break;
        }
    }
    return false;
}

bool ConstructionStore::move_down(ConstructionProjectId id) {
    for (std::size_t i = 0; i < projects_.size(); ++i) {
        if (projects_[i].id == id) {
            // Find succeeding project in the same country
            for (std::size_t j = i + 1; j < projects_.size(); ++j) {
                if (projects_[j].country == projects_[i].country) {
                    std::swap(projects_[i].priority, projects_[j].priority);
                    std::swap(projects_[i], projects_[j]);
                    return true;
                }
            }
            break;
        }
    }
    return false;
}

const ConstructionProjectRecord* ConstructionStore::find(ConstructionProjectId id) const noexcept {
    for (const auto& p : projects_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

std::vector<ConstructionProjectId> ConstructionStore::country_queue(CountryId country) const {
    std::vector<const ConstructionProjectRecord*> subset;
    for (const auto& p : projects_) {
        if (p.country == country) subset.push_back(&p);
    }
    std::sort(subset.begin(), subset.end(), [](const auto* a, const auto* b) {
        return a->priority < b->priority;
    });
    std::vector<ConstructionProjectId> result;
    result.reserve(subset.size());
    for (const auto* p : subset) result.push_back(p->id);
    return result;
}

std::uint32_t ConstructionStore::pm_transition_progress_ppm(BuildingId building) const noexcept {
    for (const auto& p : projects_) {
        if (p.target_building == building && p.kind == ConstructionKind::UpgradeProductionMethod) {
            if (p.total_points_required == 0) return 1'000'000;
            return static_cast<std::uint32_t>(
                mul_div_nonnegative(p.progress_points, 1'000'000, p.total_points_required));
        }
    }
    return 1'000'000; // Not transitioning, 100% stable
}

EconomyAmount ConstructionStore::country_weekly_construction_capacity(CountryId country,
                                                                      const World& world) const {
    if (!country.valid() || static_cast<std::size_t>(country.value()) >= world.countries.size()) {
        return 10;
    }
    // Base capacity = 10 construction points per week
    EconomyAmount points = 10;
    const double gdp_val = world.countries.gdp(country);
    // +1 point per 50 units GDP
    points += static_cast<EconomyAmount>(gdp_val / 50.0);
    // +1 point per 10,000 investment pool funds
    const auto pool_cash = world.grand_strategy.investment_pool_cash(country);
    if (pool_cash > 0) {
        points += std::min<EconomyAmount>(50, pool_cash / 10'000);
    }
    return std::clamp<EconomyAmount>(points, 5, 500);
}

JobDispatchStats ConstructionStore::tick_weekly(World& world) {
    if (projects_.empty()) return JobDispatchStats{};

    const std::size_t num_countries = world.countries.size();
    std::vector<std::size_t> completed_indices;

    for (std::size_t ci = 0; ci < num_countries; ++ci) {
        const CountryId country{static_cast<CountryId::rep_type>(ci)};
        auto remaining_capacity = static_cast<std::uint32_t>(
            country_weekly_construction_capacity(country, world));

        for (std::size_t pi = 0; pi < projects_.size() && remaining_capacity > 0; ++pi) {
            auto& p = projects_[pi];
            if (p.country != country || p.paused) continue;

            const auto needed = p.total_points_required > p.progress_points
                ? p.total_points_required - p.progress_points
                : 0u;
            if (needed == 0) continue;

            // Maximum points this project can take in a single week (cap at 25 points to allow queue parallelism)
            const auto allocated_points = std::min({remaining_capacity, needed, 25u});
            if (allocated_points == 0) continue;

            // Calculate weekly capital and material goods cost
            const EconomyAmount weekly_cost = p.total_cost_milli > 0
                ? mul_div_nonnegative(p.total_cost_milli, allocated_points, p.total_points_required)
                : static_cast<EconomyAmount>(allocated_points * 500);

            // Attempt funding from domestic investment pool first, then treasury
            EconomyAmount funded = world.grand_strategy.withdraw_investment_pool_funds(country, weekly_cost);
            if (funded < weekly_cost) {
                const auto rem_cost = weekly_cost - funded;
                const auto treasury_avail = std::max<EconomyAmount>(0, world.countries.treasury_milli(country));
                const auto from_treasury = std::min(rem_cost, treasury_avail);
                if (from_treasury > 0) {
                    world.countries.add_treasury_milli(country, -from_treasury);
                    funded += from_treasury;
                }
            }

            // If funded, advance progress
            p.progress_points += allocated_points;
            p.paid_cost_milli = saturating_add(p.paid_cost_milli, funded);
            p.weekly_progress_ppm = static_cast<std::uint32_t>(
                mul_div_nonnegative(allocated_points, 1'000'000, p.total_points_required));
            remaining_capacity -= allocated_points;

            // If project is complete, execute transformation
            if (p.progress_points >= p.total_points_required) {
                switch (p.kind) {
                case ConstructionKind::ExpandBuilding: {
                    if (p.target_building.valid() &&
                        static_cast<std::size_t>(p.target_building.value()) < world.buildings.size()) {
                        const auto cur_lvl = world.buildings.level(p.target_building);
                        world.buildings.set_level(p.target_building, static_cast<std::uint16_t>(cur_lvl + 1));
                    }
                    break;
                }
                case ConstructionKind::UpgradeProductionMethod: {
                    if (p.target_building.valid() &&
                        static_cast<std::size_t>(p.target_building.value()) < world.buildings.size() &&
                        p.target_pm.valid()) {
                        world.buildings.set_production_method(p.target_building, p.target_pm);
                    }
                    break;
                }
                case ConstructionKind::ConstructMonument: {
                    world.countries.add_prestige(p.country, 25.0);
                    world.countries.set_gdp(p.country, world.countries.gdp(p.country) + 50.0);
                    break;
                }
                }
                completed_indices.push_back(pi);
            }
        }
    }

    // Remove completed projects in reverse order
    std::sort(completed_indices.rbegin(), completed_indices.rend());
    for (const auto idx : completed_indices) {
        if (idx < projects_.size()) {
            projects_.erase(projects_.begin() + idx);
        }
    }

    return JobDispatchStats{};
}

std::uint64_t ConstructionStore::checksum() const noexcept {
    Fnv1a64 h;
    h.add(projects_.size());
    for (const auto& p : projects_) {
        h.add(p.id.value());
        h.add(p.country.value());
        h.add(p.province.value());
        h.add(p.target_building.value());
        h.add(static_cast<std::uint8_t>(p.kind));
        h.add(p.target_pm.value());
        h.add(p.monument_key_hash);
        h.add(p.progress_points);
        h.add(p.total_points_required);
        h.add(p.total_cost_milli);
        h.add(p.paid_cost_milli);
        h.add(p.paused ? 1u : 0u);
        h.add(p.priority);
    }
    return h.value();
}

std::size_t ConstructionStore::memory_bytes() const noexcept {
    return sizeof(ConstructionStore) + projects_.capacity() * sizeof(ConstructionProjectRecord);
}

void ConstructionStore::clear() noexcept {
    projects_.clear();
    next_id_ = 0;
}

void ConstructionStore::restore_project(const ConstructionProjectRecord& rec) {
    projects_.push_back(rec);
    if (rec.id.valid() && rec.id.value() >= next_id_) {
        next_id_ = rec.id.value() + 1;
    }
}

} // namespace core
