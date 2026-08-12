#pragma once

#include "plang/Basic/SwitchTable.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plang {

/// Language options that control which dialect-specific features the compiler
/// accepts.  Built by the driver from command-line flags and propagated unchanged
/// through Scanner, Parser, Sema, and CodeGen.
///
/// Default-constructed LangOptions represents strict ISO 7185 mode.
struct LangOptions {
    /// The dialects, from Dialects.def, which is also where the driver and the
    /// front end get the spellings they validate -std= against.
    enum class Standard {
#define DIALECT(Id, Spelling, Implemented) Id,
#include "plang/Basic/Dialects.def"
    };

    /// Active language standard (default: ISO7185).  It is enforced strictly:
    /// a construct outside it is an error, so there is no separate flag for
    /// asking that it be taken seriously.
    Standard Std          = Standard::ISO7185;
    /// If true, emit ISO array-index and subrange assignment checks
    /// (-fno-range-checks turns them off).  Division by zero and unmatched
    /// case labels are always checked; those cost nothing measurable.
    bool     RangeChecks  = true;
    /// If true, emit the ISO §6.5.4 check that a pointer being dereferenced is
    /// not nil (-fno-nil-checks turns it off).  Separate from RangeChecks:
    /// asking for indexing not to be checked is a statement about the cost of
    /// a bounds test in a loop, and says nothing about wanting a nil
    /// dereference to become a bare segmentation fault.  The two were one flag
    /// until 0.1.2, which meant -fno-range-checks quietly took this with it.
    bool     NilChecks    = true;
    /// What Turbo's `{$I}` starts at: whether an I/O operation is checked as
    /// soon as it is done, rather than left for IOResult to report.
    ///
    /// There is no flag for it, because there is nothing yet for it to switch
    /// off: ISO 7185 and Extended Pascal report an I/O failure by aborting, and
    /// IOResult, which is the whole point of turning the check off, is Turbo's
    /// and arrives with the file runtime.  A -fno-io-checks that changed
    /// nothing would be a flag that appears to work.  It is here so that
    /// `{$I-}` has a default to override.
    bool     IOChecks     = true;
    /// -O0..-O3.  Selects the LLVM optimization pipeline run over the module
    /// before it is written out; 0 runs none.
    unsigned OptLevel     = 0;

    // ---- Positional switch state ------------------------------------------

    /// Where Turbo's `{$R+}`-style switches changed, in source order; null
    /// when no directive was seen, which is always for ISO 7185 and Extended
    /// Pascal.  Held by pointer because LangOptions is copied by value into the
    /// scanner, the parser, Sema and codegen, and all four have to see the same
    /// table -- the scanner fills it in while the others are already holding
    /// their copy.
    std::shared_ptr<const SwitchTable> Switches;

    /// What a switch is when no directive has said otherwise.  Derived from
    /// the command line, so that -fno-range-checks is the starting state a
    /// `{$R+}` then overrides rather than something a directive cannot reach.
    [[nodiscard]] CompilerState defaultSwitches() const {
        CompilerState C = CompilerState::turboDefaults();
        C.set(Switch::RangeChecks, RangeChecks);
        C.set(Switch::IOChecks,    IOChecks);
        return C;
    }

    /// Whether \p S is on at \p Loc.  With no table this is the command-line
    /// default and nothing is searched, which is what keeps a dialect that has
    /// no directives on exactly the path it was on before.
    [[nodiscard]] bool switchOn(Switch S, SourceLocation Loc) const {
        if (!Switches) return defaultSwitches().on(S);
        return Switches->on(S, Loc, defaultSwitches());
    }

    // ---- Which dialect, and what it can do --------------------------------
    //
    // Two questions, and they are not the same one.
    //
    // `extendedPascal()` asks which standard this is, and is the right question
    // for the thirty-odd extensions that are Extended Pascal's alone -- schema
    // types, `type of`, `bindable`, `restricted`, `**`, `><`, `value`,
    // `for ... in`, modules.  Turbo must never inherit those, so those sites go
    // on asking exactly this.
    //
    // `has(Feature::X)` asks whether the active dialect has a capability, and
    // is the right question for the handful more than one dialect has --
    // declaration order, constant expressions, case ranges.  Turbo wants those
    // too, and re-gating them by name is what hands them over when it arrives
    // rather than re-deriving each from a growing chain of `Std ==` tests.
    //
    // A feature's dialects are written once, in LangFeatures.def, and derived
    // from Std rather than stored: there is no seeded copy that can fall out of
    // step with the standard actually selected.  If per-feature overrides are
    // ever wanted -- FPC's `{$MODESWITCH}` is the obvious reason -- they go
    // behind `has()` without any call site changing.

    /// Dialects, as bits, so that a capability can name more than one.
    enum DialectBits : unsigned {
        D_ISO7185  = 1u << 0,
        D_ISO10206 = 1u << 1,
        D_Turbo    = 1u << 2,
        D_Delphi   = 1u << 3,
        D_FPC      = 1u << 4,
    };

    enum class Feature {
#define FEATURE(Id, Dialects) Id,
#include "plang/Basic/LangFeatures.def"
    };

    /// The dialects that have \p F.
    static constexpr unsigned featureDialects(Feature F) {
        switch (F) {
#define FEATURE(Id, Dialects) case Feature::Id: return (Dialects);
#include "plang/Basic/LangFeatures.def"
        }
        return 0;
    }

    /// This dialect, as a bit.
    constexpr unsigned dialectBit() const {
        switch (Std) {
#define DIALECT(Id, Spelling, Implemented) case Standard::Id: return D_##Id;
#include "plang/Basic/Dialects.def"
        }
        return D_ISO7185;
    }

    /// The Standard \p Name spells, or nothing when it spells none of them.
    static constexpr std::optional<Standard> parseDialect(std::string_view Name) {
#define DIALECT(Id, Spelling, Implemented) if (Name == Spelling) return Standard::Id;
#include "plang/Basic/Dialects.def"
        return std::nullopt;
    }

    /// True when \p Name spells a dialect plang can actually compile, as
    /// opposed to one it merely recognises the name of.
    static constexpr bool isImplementedDialect(std::string_view Name) {
#define DIALECT(Id, Spelling, Implemented) if (Name == Spelling) return (Implemented);
#include "plang/Basic/Dialects.def"
        return false;
    }

    /// The dialect names, for a diagnostic that has to list them.  Not
    /// translated: these are what a user types on a command line.
    static std::string knownDialects() {
        std::string Out;
#define DIALECT(Id, Spelling, Implemented) \
        if (!Out.empty()) Out += ", ";     \
        Out += Spelling;
#include "plang/Basic/Dialects.def"
        return Out;
    }

    static std::string implementedDialects() {
        std::string Out;
#define DIALECT(Id, Spelling, Implemented)     \
        if (Implemented) {                     \
            if (!Out.empty()) Out += ", ";     \
            Out += Spelling;                   \
        }
#include "plang/Basic/Dialects.def"
        return Out;
    }

    /// True when the active dialect is any of \p Mask.
    constexpr bool inDialect(unsigned Mask) const { return (dialectBit() & Mask) != 0; }

    /// True when the active dialect has \p F.
    constexpr bool has(Feature F) const { return inDialect(featureDialects(F)); }

    /// How wide an unqualified `integer` is.
    ///
    /// ISO 7185 and Extended Pascal have one integer type, and plang has always
    /// given it 64 bits: §6.4.2.2 leaves the range implementation-defined and
    /// only requires maxint.  Turbo's `Integer` is 16 bits, and it is not a
    /// free choice -- FPC's -Mtp does not load objpas, so `Integer` stays
    /// `system.integer` = `smallint` and MaxInt is 32767.  A program that
    /// overflows at 32767 on a real Turbo and not here is not compiling as
    /// Turbo Pascal.
    [[nodiscard]] constexpr unsigned defaultIntWidth() const {
        return turbo() ? 16u : 64u;
    }

    constexpr bool extendedPascal() const { return Std == Standard::ISO10206; }
    constexpr bool turbo()          const { return Std == Standard::Turbo;    }

    /// Directories to search for .pmi module interface files (from -I flags).
    std::vector<std::string> ModuleSearchPaths;
};

} // namespace plang
