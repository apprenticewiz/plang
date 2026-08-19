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
    ~Codegen(); ///< defined in Codegen.cpp where Impl is complete

    /// Emits a complete LLVM module for the program to \p os as textual IR.
    /// Precondition: Sema has verified the program without errors.
    /// Returns true on success; false if LLVM IR verification fails (diagnostics
    /// are written to stderr and \p os is left empty).
    bool emit(const ProgramNode& prog, std::ostream& os);

    /// EP §6.11: tell codegen what each unit imports, as computed by
    /// Sema::importOwners.  See ImportedName.  It must outlive emit.
    void setImportOwners(const ImportOwnerTable& Owners);

    /// EP §6.11: the interfaces read from .pmi files, as Sema kept them.  What
    /// they declare has to be laid out here as if this unit had declared it.
    /// They must outlive emit.
    void setLoadedInterfaces(std::vector<const ModuleNode*> Ifaces);

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
