# CoreScript in Core 1.0

CoreScript is Core's typed, moddable content language. This document describes the
current Core 1.0 development implementation; it does not declare a new product
version, and it does not claim full Jomini parity.

Source text is parsed and linked during content loading. Simulation code receives
compact primitive IDs, stable keys and compiled instruction trees, so primitive
names and scope properties are not resolved from strings in hot loops.

## Pipeline

```text
.core source
  -> bounded lexer/parser
  -> symbol interning and stable-key collision checks
  -> typed script/value compilation
  -> whole-database link validation
  -> compact fast path or scoped VM
```

`ContentLoader` parses all effective VFS files before calling
`ScriptProgramDatabase::validate_links`. Cross-file calls can therefore resolve
independently of file boundaries. A content transaction with parser, compiler or
linker diagnostics must not become live content.

## Programs and execution paths

A script declares a root scope and may contain repeated trigger and effect blocks:

```text
script fiscal_relief {
    scope = country
    trigger = { treasury_above = 10 }
    effect = { add_treasury = 2 }
}
```

All trigger blocks in one script are ANDed. An explicit empty trigger is true.
Core chooses one coherent backend for the complete program:

- flat numeric AND conditions use a contiguous `fast_all` array;
- same-scope `all` / `any` / `not` expressions use compact RPN instructions;
- scope navigation, variables, calls and iterators use the scoped tree VM.

If any block needs the scoped backend, every block of that kind is compiled there;
mixing a classic block with an advanced block does not silently discard either.
Simple numeric scripts therefore keep their short-circuiting fast path while
advanced content pays the interpreter cost.

Implemented root scope types are Country, State, Province, Pop and Market. The
primitive registry owns the accepted scope and argument kinds for each native
trigger/effect. A mismatch is rejected while compiling and checked again at the
runtime boundary.

## Typed parameters and linking

Scripts and scripted values can declare named signatures:

```text
script grant_relief {
    scope = country
    parameters = {
        amount = number
        recipient = country
        bonus = { type = number required = no default = 2 }
    }
    effect = {
        add_treasury = arg:amount
        add_treasury = arg:bonus
    }
}

script caller {
    scope = country
    parameters = { recipient = country }
    effect = {
        scripted_effect = {
            name = grant_relief
            amount = 5
            recipient = arg:recipient
        }
    }
}
```

Current parameter kinds are Number, Symbol/Key, Boolean, generic Scope, and the
Country/State/Province/Pop/Market scope subtypes. Parameters are required by default;
an optional non-scope parameter may provide a literal default. Named bindings are stored in
stable-key order and reject non-finite numbers. Numeric negative zero is canonicalized
so it cannot create a checksum-only difference.

The whole-database linker reports:

- unknown scripted trigger/effect/value targets;
- caller/callee root-scope mismatch;
- missing, extra or duplicate named arguments;
- statically provable argument-kind and scope-subtype mismatch;
- references to undeclared typed parameters;
- direct or indirect script and scripted-value call cycles.

Runtime entry points bind the same schema again. This protects calls originating
from C++ or restored state, not only calls produced by the compiler. Callee frames
are lexical and are removed after the call; callee-local parameters and variables
do not leak into their caller.

## Arguments and scripted values

Compiled arguments can come from a literal, variable, parameter, event target,
THIS/ROOT/FROM/PREV, or another scripted value. `ScriptArgument` currently carries
Number, SymbolHash, Boolean or Scope.

Scripted values support typed parameters and the following direct sources:

- Country: population, GDP, treasury and tax rate;
- Pop: size, employment, standard of living, literacy, qualification, wealth and
  political strength;
- Market: aggregate supply and demand;
- State and Province: population;
- a runtime argument or another scripted value.

The current value expression is `source * multiply + add`; it is intentionally not
yet a general arithmetic bytecode. Results and external numeric arguments must be
finite.

## Scope context, targets and collections

`ScriptExecutionContext` contains ROOT, current scope, FROM, the PREV stack, lexical
call frames, event-target bindings, typed collections and deterministic random-draw
counters. Scope selectors include THIS, ROOT, FROM, PREV, owner/parent navigation,
Country/Market/State/Province direct selectors and a saved/event target.

Implemented state operations include:

- set/change/clear and compare variables;
- save/clear event targets (`save_scope_as` remains a compatibility alias);
- add/remove/clear collection values;
- `any_*`, `every_*` and deterministic `random_*` scope iteration;
- `any_in:name`, `every_in:name` and `random_in:name` collection iteration.

A collection is homogeneous. Scope collections also retain the concrete element
scope type, so a Country collection cannot later accept a Pop even though both are
represented as `ScriptArgumentKind::Scope`. Unique insertion preserves deterministic
first-in order.

These bindings are transient for a standalone invocation. A live Event or Journal
instance can retain the complete context across ticks. The `GCT1` save section then
encodes it with bounded counts, stable keys, scope-reference validation, deterministic
checksum coverage and staging-before-commit restore. Completed gameplay instances
release their context. This is a gameplay persistence boundary, not yet a universal
global-variable store.

## Deterministic random semantics

Random iterator callsite salts derive from the program's stable name and compiled
structural path. They do not depend on source line numbers or SymbolTable insertion
order. The execution context stores sorted per-callsite draw counters; repeated
execution at one callsite advances a deterministic sequence rather than returning
the same choice forever. Live gameplay contexts save and checksum those counters.

Weighted lists, ordered selection, scripted ordering values and fallback semantics
remain outstanding.

## Safety and diagnostics

The parser rejects malformed tokens, numeric overflow/non-finite literals, excessive
nesting and excessive AST size. Compilation rejects unknown primitives, scope and
argument mismatches, malformed call/signature blocks, stable-name hash collisions and
invalid defaults. The VM has call-depth checks and a per-public-invocation work budget;
the remaining budget is deliberately transient and excluded from save/checksum.

Use `core_content_check` for startup-equivalent parsing, compilation and link
diagnostics without starting the game. Production diagnostics still need richer mod
source chains, script call stacks, cost profiling and desync-context diffs.

## Save/checksum contract

Persistent contexts use stable 64-bit binding keys and are validated against the
restored World. Their checksum covers ROOT/current/FROM/PREV, parameters, variables,
targets, collections, seed and random-draw counters in deterministic order. Resource
budgets and other VM-local guard state are not simulation state and are not serialized.

Dedicated regression coverage includes insertion-order-independent checksums,
scope-subtype rejection, frame isolation, mixed-backend compilation, empty triggers,
stable random salts, repeated draw counters, parser limits, non-finite rejection,
typed signature defaults and failures, link-cycle diagnostics, and save/restore of a
live event context followed by identical continuation.

## Remaining major gaps

CoreScript is a substantial foundation, but it is not yet a complete general
Clausewitz/Jomini content runtime. Important missing work includes:

- broader scope registration for politics, diplomacy, warfare, companies, characters
  and other domain objects;
- ordered/filtered/count/weighted iterator IR with scripted sort and weight values;
- full arithmetic/conditional/aggregate value bytecode and modifier integration;
- Date, Duration, Collection and other signature/result types;
- domain-owned persistent variables/lists outside gameplay instances (the
  world-level global store is persisted by the `GLB1` save extension);
- a typed on-action bus and saved delayed-effect/event scheduler;
- compact parameter slots and allocation-free high-frequency call frames;
- structured profiling, source provenance, compiled-content cache and editor-safe
  incremental reload.

New language features must remain data driven and preserve the existing fast path:
high-cardinality simulation loops should query SoA/CSR views or compiled numeric IDs,
not perform per-object string reflection or unbounded allocation.
