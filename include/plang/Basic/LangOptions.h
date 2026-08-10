#pragma once

#include <string>
#include <vector>

namespace plang {

/// Language options that control which dialect-specific features the compiler
/// accepts.  Built by the driver from command-line flags and propagated unchanged
/// through Scanner, Parser, Sema, and CodeGen.
///
/// Default-constructed LangOptions represents strict ISO 7185 mode.
struct LangOptions {
    enum class Standard {
        ISO7185,   // -std=iso7185 (default)
        ISO10206,  // -std=iso10206 — Extended Pascal
        FPC,       // -std=fpc — Free Pascal extensions
        Delphi,    // -std=delphi
        Turbo,     // -std=turbo
    };

    /// Active language standard (default: ISO7185).  It is enforced strictly:
    /// a construct outside it is an error, so there is no separate flag for
    /// asking that it be taken seriously.
    Standard Std          = Standard::ISO7185;
    /// If true, emit ISO array-index and subrange assignment checks
    /// (-fno-range-checks turns them off).  Division by zero and unmatched
    /// case labels are always checked; those cost nothing measurable.
    bool     RangeChecks  = true;
    /// -O0..-O3.  Selects the LLVM optimization pipeline run over the module
    /// before it is written out; 0 runs none.
    unsigned OptLevel     = 0;

    bool extendedPascal() const { return Std == Standard::ISO10206; }

    /// Directories to search for .pmi module interface files (from -I flags).
    std::vector<std::string> ModuleSearchPaths;
};

} // namespace plang
