# Core 0.8 Economy Architecture

## Goal

Core 0.8 proves that a Victoria-style POP/building/market loop can scale while preserving deterministic
results across worker counts. It is a vertical slice, not the final economic design: trade routes,
credit, ownership shares, qualifications, migration and a full labor matcher remain later work.

## Runtime data layout

Mutable high-cardinality data is stored as SoA columns:

- `PopStore`: market, population, employed population, employer, need profile, income, standard of living.
- `BuildingStore`: market, building type, level, employees, wage offer, cash, last profit.
- `MarketStore`: owner country plus flat `[market][good]` price/supply/demand arrays.
- `MarketEntityIndex`: immutable/rebuilt CSR-style market -> POP/building ID lists.

Cold recipe names and content keys live in `EconomyDefinitions`; the weekly hot path uses compact typed
IDs and flat recipe/need-flow arrays.

## Deterministic fixed point

Market quantities, prices and money use signed 64-bit integer fixed point. Values are expressed in
milli-units where appropriate. This avoids floating-point summation order becoming simulation state.
Country aggregate values still cross the legacy `CountryStore` double boundary once per country after a
stable market-order fold; the high-cardinality market loop itself is integer.

## Parallel ownership model

The primary parallel unit is a market, grouped into fixed four-market jobs. A job owns all writes to its
market's supply/demand/price row and to entities indexed under that market. This avoids atomics in the
normal economic path.

Weekly phases:

1. Employment/capacity reconciliation.
2. Building production and industrial input demand.
3. POP income plus need-profile population aggregation.
4. Market price convergence.
5. Building settlement, POP standard-of-living calculation, taxes, and stable country aggregation.

Country treasury/GDP/population aggregation is folded in market ID order after parallel settlement so
multiple markets owned by one country cannot become scheduling-order dependent.

## Hot-path optimizations

### Column views

Public entity APIs retain checked typed-ID accessors, but EconomySystem validates the world layout once
then holds `std::span` column views inside a phase. This removed millions of repeated bounds checks and
function calls in the 300k-POP benchmark.

### Direct employer capacity index

Employment does not search a market's buildings for every POP. `BuildingId` indexes a reusable
`building_remaining_` array directly. Market ownership guarantees no two market jobs mutate the same
building slot.

### Need-profile aggregation

POP consumption does not traverse an identical goods basket for every cohort. It first sums population
by `(market, need_profile)`, then evaluates each profile's goods flows once per market. Settlement also
precomputes the current basket cost per `(market, need_profile)` before scanning POPs.

This is especially important because realistic worlds may have hundreds of thousands of POP cohorts but
only a small number of need profiles.

## Current model boundaries

0.8 deliberately keeps several systems simple:

- POPs retain an assigned employer; unemployed workers are not yet searched across all alternative jobs.
- Markets use price response to supply/demand imbalance rather than stockpile logistics.
- No trade-route network or inter-market arbitrage yet.
- No ownership dividends, investment pool, banking, credit, qualifications or migration yet.
- Wage adjustment is a bounded weekly feedback rule, not the final bargaining model.
- Price history/ring buffers are not yet implemented.

These are content/simulation layers on top of the current stores, not reasons to replace the data layout.
