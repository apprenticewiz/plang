// The rest of this file's TEST() cases migrated to test/Sema/ (issue
// #43, Phase D) -- each is one program run through `plang -dump-ast`, with
// the exit code and/or a diagnostic-substring FileCheck covering exactly
// what its `check(...).Ok`/`.hasError(...)`/`.hasWarning(...)` assertion did.
//
// These two are a deliberate, permanent GoogleTest exception: both drive
// `check()` from an X-macro loop over #include "plang/Basic/Builtins.def",
// once per builtin name in the table, so the Pascal source under test is
// assembled at C++ run time rather than fixed -- there is no single .pas
// file that can stand in for "every name in the table, whichever one that
// currently is." Losing that property (iterating the real table, not a
// snapshot of it) is exactly the kind of silent coverage regression a
// mechanical migration must not introduce.

#include "TestHelper.h"

#include "plang/Basic/BuiltinIDs.h"

#include <gtest/gtest.h>

#include <string>

using namespace plang;

// ---------------------------------------------------------------------------
// The builtin catalogue
//
// Builtins.def is one list, and these check the two things folding three lists
// into one was for: that a name is declared whatever the dialect, and that the
// arity checked is the arity written beside the declaration.
// ---------------------------------------------------------------------------

TEST(Builtins, ANameOfAnotherDialectIsDeclaredRatherThanUndefined) {
    // Nineteen of these -- the whole complex, direct-access, date/time and
    // binding block -- were declared only inside `if (extendedPascal())`, so
    // `cmplx(1.0, 2.0)` under -std=iso7185 came back as "undefined function".
    // That reads as a typo rather than as a dialect boundary, and the ten names
    // that were declared both ways showed what it should have said.
    //
    // Driven from the .def so that a name added there is covered here without
    // anyone remembering to add it.
    const auto probe = [](const char* Name, bool IsFunc) {
        std::string Src = "program p(output);\nvar r: real;\nbegin ";
        if (IsFunc) Src += "r := ";
        Src += Name;
        Src += "(r) end.\n";
        return check(Src);
    };
    std::size_t Probed = 0;
    // Assert (Dialects = TP) is the first name this loop meets that is not
    // Extended Pascal's: Sema::checkEPOnly picks between err_ep_required_name
    // and err_turbo_required_name by the same "has Turbo, does not have EP"
    // test mirrored here, so a name declared only for Turbo has to be probed
    // for the OTHER message -- asserting the EP one for it would be asserting
    // the bug this same PR fixed (err_ep_required_name's wording promises
    // -std=iso10206 accepts a name that, for a Turbo-only one, no dialect but
    // Turbo does).
#define BUILTIN(Id_, Spelling_, Kind_, Dialects_, Min_, Max_, Result_)         \
    if (!LangOptions{}.inDialect(Dialects_)) {                                 \
        ++Probed;                                                              \
        const auto R = probe(Spelling_, builtinIsFunction(BuiltinID::Id_));    \
        const bool TurboOnly = ((Dialects_) & LangOptions::D_Turbo)            \
                             && !((Dialects_) & LangOptions::D_ISO10206);      \
        EXPECT_TRUE(R.hasError(TurboOnly                                       \
            ? "'" Spelling_ "' is a Turbo Pascal extension"                    \
            : "'" Spelling_ "' is an Extended Pascal"))                        \
            << Spelling_;                                                      \
        EXPECT_FALSE(R.hasError("undefined")) << Spelling_;                    \
    }
#include "plang/Basic/Builtins.def"
    // A loop written over a .def can pass by iterating over nothing, and this
    // one would if the dialect masks were ever all-inclusive.
    EXPECT_GT(Probed, 0u);
}

TEST(Builtins, ANameOfThisDialectIsNotRefusedForBeingOne) {
    // The other half of the pair above: declaring every name whatever the
    // dialect must not start refusing the ones that belong to the dialect in
    // force.  A wrong dialect mask in the .def would fail exactly here.
    std::size_t Probed = 0;
#define BUILTIN(Id_, Spelling_, Kind_, Dialects_, Min_, Max_, Result_)         \
    if (LangOptions{}.inDialect(Dialects_)) {                                  \
        ++Probed;                                                              \
        std::string Src = "program p(output);\nvar r: real;\nbegin ";          \
        if (builtinIsFunction(BuiltinID::Id_)) Src += "r := ";                 \
        Src += Spelling_;                                                      \
        Src += "(r) end.\n";                                                   \
        EXPECT_FALSE(check(Src).hasError("is an Extended Pascal"))             \
            << Spelling_;                                                      \
    }
#include "plang/Basic/Builtins.def"
    // Every builtin is in exactly one of the two halves, so the two counts
    // together are the whole list -- which is what says both loops really ran
    // over it rather than over an empty selection.
    EXPECT_GT(Probed, 0u);
    std::size_t EPOnly = 0;
#define BUILTIN(Id_, Spelling_, Kind_, Dialects_, Min_, Max_, Result_)         \
    if (!LangOptions{}.inDialect(Dialects_)) ++EPOnly;
#include "plang/Basic/Builtins.def"
    EXPECT_EQ(Probed + EPOnly, NumBuiltins);
}
