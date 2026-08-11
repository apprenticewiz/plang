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
    /// If true, emit the ISO §6.5.4 check that a pointer being dereferenced is
    /// not nil (-fno-nil-checks turns it off).  Separate from RangeChecks:
    /// asking for indexing not to be checked is a statement about the cost of
    /// a bounds test in a loop, and says nothing about wanting a nil
    /// dereference to become a bare segmentation fault.  The two were one flag
    /// until 0.1.2, which meant -fno-range-checks quietly took this with it.
    bool     NilChecks    = true;
    /// -O0..-O3.  Selects the LLVM optimization pipeline run over the module
    /// before it is written out; 0 runs none.
    unsigned OptLevel     = 0;

    bool extendedPascal() const { return Std == Standard::ISO10206; }

    /// Directories to search for .pmi module interface files (from -I flags).
    std::vector<std::string> ModuleSearchPaths;
};

} // namespace plang
