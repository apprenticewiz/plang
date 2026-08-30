#pragma once

#include "plang/AST/Ast.h"
#include "plang/Basic/LangOptions.h"
#include "plang/Basic/ModuleImports.h"
#include "plang/Basic/SourceManager.h"

#include <memory>
#include <ostream>
#include <vector>

namespace plang {

/// Lowers a Pascal AST to textual LLVM IR using the LLVM C++ API.
/// The LLVM types are hidden behind a pimpl so that this header can be included
/// by Main.cpp (which uses exceptions) without pulling in LLVM's headers
/// (which expect -fno-exceptions).
class Codegen {
public:
    explicit Codegen(const LangOptions& Opts = {});
    ~Codegen(); ///< defined in CodeGen.cpp where Impl is complete

    /// Emits a complete LLVM module for the program to \p os as textual IR.
    /// Precondition: Sema has verified the program without errors.
    /// Returns true on success; false if LLVM IR verification fails (diagnostics
    /// are written to stderr and \p os is left empty).
    bool emit(const ProgramNode& prog, std::ostream& os);

    /// Turbo Tier 4, Cluster A item 2: emits a complete LLVM module for one
    /// standalone Turbo unit (`unit Name; interface ... implementation ...
    /// end.`) to \p os as textual IR -- real separate-compilation codegen,
    /// not item 1's "type-checks but stops there" placeholder.  No `main` is
    /// emitted (mirrors Codegen::emit's own IsModuleOnly path for an EP
    /// module compiled standalone): the object this produces is meant to be
    /// linked into a later program's own object, the same two-step
    /// `plang -c unit.pas -o unit.o` then `plang prog.pas unit.o -o prog`
    /// shape EP's own module separate compilation already uses.
    /// setUsedUnits below should be called first with whatever units \p Unit
    /// itself 'uses' (interface and/or implementation), exactly as a
    /// program's own compile does for its top-level 'uses'.
    /// Precondition: Sema::checkUnit has verified \p Unit without errors.
    bool emitUnit(const UnitNode& Unit, std::ostream& os);

    /// EP §6.11: tell codegen what each unit imports, as computed by
    /// Sema::importOwners.  See ImportedName.  It must outlive emit.
    void setImportOwners(const ImportOwnerTable& Owners);

    /// EP §6.11: the interfaces read from .pmi files, as Sema kept them.  What
    /// they declare has to be laid out here as if this unit had declared it.
    /// They must outlive emit.
    void setLoadedInterfaces(std::vector<const ModuleNode*> Ifaces);

    /// Turbo Tier 4, Cluster A item 1: the units this program's own top-level
    /// 'uses' clause named, in the order it named them, as Sema already
    /// parsed and checked them (Sema::loadUnitInterfaceExports /
    /// Sema::loadedUnit) -- NOT a general "codegen for a unit" mechanism
    /// (that is item 2/3's own job, once real separate compilation exists).
    /// This is deliberately narrow: only what is needed to make Cluster A
    /// item 1's own runtime shadowing/qualification test executable.  Each
    /// unit's INTERFACE-declared, SCALAR, foldable constants (an Integer,
    /// Char, Boolean, ... literal or constant expression -- not a typed
    /// constant, not an array/record/set value, not anything requiring real
    /// storage) are registered as compile-time immediates, both under their
    /// own name and under "UnitName.name" for explicit qualification, in
    /// 'uses' order so a later unit's same-named constant simply overwrites
    /// an earlier one's -- the same last-uses-wins order Sema's own
    /// SymbolTable scoping already resolved the identifiers with. A unit
    /// export this narrow mechanism does not cover (a variable, a
    /// procedure/function, a structured or typed constant) still type-checks
    /// under Sema, but referencing it from a program compiled this way is
    /// not yet expected to produce working code -- see this method's own
    /// definition (CodeGenProcs.cpp) for exactly what it skips and why.
    /// They must outlive emit.
    void setUsedUnits(std::vector<const UnitNode*> Units);

    /// -g: the SourceManager that resolves every node's SourceLocation to a
    /// filename/line/column, and the FileID of the main input file, from
    /// which DIFile/DICompileUnit take their name and directory.  Only
    /// consulted when LangOptions::Debug is set; must outlive emit.
    void setSourceManager(const SourceManager& SM, FileID MainFile);

private:
    struct Impl;
    std::unique_ptr<Impl> PImpl;
};

} // namespace plang
