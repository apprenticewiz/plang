#pragma once

namespace plang {

/// RAII bump/unbump of a recursion-depth counter across one activation of a
/// recursive function -- the shape every one of this codebase's existing
/// per-consumer depth guards (Parser's Expr/Type/Stmt/Block/Value/
/// TurboConstValue depths; Sema's own checkExpr guard) already hand-rolls as
/// its own private nested struct, byte-for-byte identical apart from the
/// name: `++Counter` on construction, `--Counter` on destruction, and (if a
/// LimitHit flag is given) clearing it back to false once Counter returns
/// to 0 so a "too deeply nested" diagnostic fires once per burst of
/// activations that hit the ceiling, not once per still-unwinding frame.
///
/// Existing guards are NOT retrofitted to this type in the PR that adds it
/// (see issue #300's own re-triage comment) -- each already works, and
/// mechanically swapping a private nested struct for this one is pure
/// churn with no behavior change. What this closes is the cost of adding
/// the NEXT one: a future recursive AST consumer (or an existing one found
/// to be missing a guard, as parsePower's own '**'/'pow' right-recursion
/// was -- issue #550) can reach for this instead of hand-rolling another
/// copy of the same eleven lines.
class RecursionGuard {
public:
    /// Counter is bumped for the lifetime of this guard. LimitHit, if given
    /// (non-null), is cleared to false once Counter unwinds back to 0.
    explicit RecursionGuard(unsigned& Counter, bool* LimitHit = nullptr)
        : Counter_(Counter), LimitHit_(LimitHit) {
        ++Counter_;
    }

    ~RecursionGuard() {
        if (--Counter_ == 0 && LimitHit_) *LimitHit_ = false;
    }

    RecursionGuard(const RecursionGuard&)            = delete;
    RecursionGuard& operator=(const RecursionGuard&) = delete;

private:
    unsigned& Counter_;
    bool*     LimitHit_;
};

} // namespace plang
