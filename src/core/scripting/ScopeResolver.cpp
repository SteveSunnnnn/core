#include "core/scripting/ScopeResolver.hpp"
#include "core/simulation/World.hpp"

namespace core {

bool ScopeResolver::valid(const World& w, ScopeRef s) noexcept {
    switch (s.type) {
        case ScopeType::Country: return static_cast<std::size_t>(s.raw_id) < w.countries.size();
        case ScopeType::State: return static_cast<std::size_t>(s.raw_id) < w.geography.state_count();
        case ScopeType::Province: return static_cast<std::size_t>(s.raw_id) < w.geography.province_count();
        case ScopeType::Pop: return static_cast<std::size_t>(s.raw_id) < w.pops.size();
        case ScopeType::Market: return static_cast<std::size_t>(s.raw_id) < w.markets.size();
        default: return false;
    }
}

ScopeRef ScopeResolver::owner(const World& w, ScopeRef s) noexcept {
    if (!valid(w, s)) return {};
    switch (s.type) {
        case ScopeType::Country: return s;
        case ScopeType::State: return ScopeRef::country(w.geography.state_owner(StateId{s.raw_id}));
        case ScopeType::Province: return ScopeRef::country(w.geography.province_owner(ProvinceId{s.raw_id}));
        case ScopeType::Market: return ScopeRef::country(w.markets.owner(MarketId{s.raw_id}));
        case ScopeType::Pop: {
            const auto p = w.pops.province(PopId{s.raw_id});
            if (p.valid()) return ScopeRef::country(w.geography.province_owner(p));
            const auto m = w.pops.market(PopId{s.raw_id});
            return m.valid() ? ScopeRef::country(w.markets.owner(m)) : ScopeRef{};
        }
        default: return {};
    }
}

ScopeRef ScopeResolver::market(const World& w, ScopeRef s) noexcept {
    if (!valid(w, s)) return {};
    switch (s.type) {
        case ScopeType::Market: return s;
        case ScopeType::State: return ScopeRef::market(w.geography.state_market(StateId{s.raw_id}));
        case ScopeType::Province: return ScopeRef::market(w.geography.province_market(ProvinceId{s.raw_id}));
        case ScopeType::Pop: return ScopeRef::market(w.pops.market(PopId{s.raw_id}));
        default: return {};
    }
}

ScopeRef ScopeResolver::state(const World& w, ScopeRef s) noexcept {
    if (!valid(w, s)) return {};
    switch (s.type) {
        case ScopeType::State: return s;
        case ScopeType::Province: return ScopeRef::state(w.geography.province_state(ProvinceId{s.raw_id}));
        case ScopeType::Pop: {
            const auto p = w.pops.province(PopId{s.raw_id});
            return p.valid() ? ScopeRef::state(w.geography.province_state(p)) : ScopeRef{};
        }
        default: return {};
    }
}

ScopeRef ScopeResolver::province(const World& w, ScopeRef s) noexcept {
    if (!valid(w, s)) return {};
    if (s.type == ScopeType::Province) return s;
    if (s.type == ScopeType::Pop) {
        const auto id = w.pops.province(PopId{s.raw_id});
        return id.valid() ? ScopeRef::province(id) : ScopeRef{};
    }
    return {};
}

std::vector<ScopeRef> ScopeResolver::all(const World& w, ScopeType t) {
    std::size_t n = 0;
    switch (t) {
        case ScopeType::Country: n = w.countries.size(); break;
        case ScopeType::State: n = w.geography.state_count(); break;
        case ScopeType::Province: n = w.geography.province_count(); break;
        case ScopeType::Pop: n = w.pops.size(); break;
        case ScopeType::Market: n = w.markets.size(); break;
        default: break;
    }
    std::vector<ScopeRef> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) out.push_back({t, static_cast<std::uint32_t>(i)});
    return out;
}

std::vector<ScopeRef> ScopeResolver::children(const World& w, ScopeRef s, ScopeType target) {
    std::vector<ScopeRef> out;
    if (!valid(w, s) || target == ScopeType::None) return out;
    if (s.type == target) {
        out.push_back(s);
        return out;
    }

    if (s.type == ScopeType::Country) {
        const auto c = CountryId{s.raw_id};
        if (target == ScopeType::State) {
            for (std::size_t i = 0; i < w.geography.state_count(); ++i) {
                const auto id = StateId{static_cast<std::uint32_t>(i)};
                if (w.geography.state_owner(id) == c) out.push_back(ScopeRef::state(id));
            }
        } else if (target == ScopeType::Province) {
            for (std::size_t i = 0; i < w.geography.province_count(); ++i) {
                const auto id = ProvinceId{static_cast<std::uint32_t>(i)};
                if (w.geography.province_owner(id) == c) out.push_back(ScopeRef::province(id));
            }
        } else if (target == ScopeType::Market) {
            for (std::size_t i = 0; i < w.markets.size(); ++i) {
                const auto id = MarketId{static_cast<std::uint32_t>(i)};
                if (w.markets.owner(id) == c) out.push_back(ScopeRef::market(id));
            }
        }
    }

    if (s.type == ScopeType::State && target == ScopeType::Province) {
        const auto st = StateId{s.raw_id};
        for (std::size_t i = 0; i < w.geography.province_count(); ++i) {
            const auto id = ProvinceId{static_cast<std::uint32_t>(i)};
            if (w.geography.province_state(id) == st) out.push_back(ScopeRef::province(id));
        }
    }

    if (target == ScopeType::Pop) {
        for (std::size_t i = 0; i < w.pops.size(); ++i) {
            const auto p = PopId{static_cast<std::uint32_t>(i)};
            bool match = false;
            if (s.type == ScopeType::Province) {
                match = w.pops.province(p) == ProvinceId{s.raw_id};
            } else if (s.type == ScopeType::Market) {
                match = w.pops.market(p) == MarketId{s.raw_id};
            } else if (s.type == ScopeType::State) {
                const auto pr = w.pops.province(p);
                match = pr.valid() && w.geography.province_state(pr) == StateId{s.raw_id};
            } else if (s.type == ScopeType::Country) {
                const auto pr = w.pops.province(p);
                if (pr.valid()) match = w.geography.province_owner(pr) == CountryId{s.raw_id};
                else {
                    const auto m = w.pops.market(p);
                    match = m.valid() && w.markets.owner(m) == CountryId{s.raw_id};
                }
            }
            if (match) out.push_back(ScopeRef::pop(p));
        }
    }
    return out;
}

} // namespace core
