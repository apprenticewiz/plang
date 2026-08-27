// VarEntry.h — one symbol-table binding.
//
// Free-standing (not nested in Codegen::Impl) so CGSymbolTable.h can name
// it without a circular include; CodeGenImpl.h uses the same type,
// unqualified lookup finds it exactly as it did when it was a nested type.
//
// Unchanged in shape from Impl::VarEntry: still covers four unrelated
// concerns in one struct (identity/storage, conformant-array parameters,
// undiscriminated schema formals, procedural parameters, with-bound schema
// paths). Slimming this into a stable core plus BindingId-keyed side
// tables is real, future work -- deliberately not part of this move; see
// project memory on the CodeGen decomposition for why.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Value.h"

namespace plang {
struct TypeNode;
struct ProcedureTypeNode;
struct Type;
}

struct VarEntry {
    llvm::Value*     ptr;               // alloca or GlobalVariable (used as ptr)
    llvm::Type*      type;              // the *value* type (pointee)
    const plang::TypeNode* typeNode{nullptr}; // original Pascal TypeNode (for file detection)
    /// The name as the programmer spelled it, kept alongside the
    /// case-folded map key (Pascal is case-insensitive, so the key
    /// cannot answer this).  A nested procedure capturing this variable
    /// through the static link re-registers it under whatever name it
    /// is handed; without this, that re-registration had only the
    /// lowercased key to offer -g's DILocalVariable, so a debugger
    /// asked for `localN` from inside the capturing procedure and
    /// found only `localn`.
    std::string displayName{};
    // EP §6.7.3.7: conformant array fields.
    bool        isConformantArray{false};  // true if this is a conformant array param
    std::string conformantLoName{};        // name of the lo bound variable
    std::string conformantHiName{};        // name of the hi bound variable
    llvm::Type* conformantElemTy{nullptr}; // LLVM element type of the conformant array
    /// The bound-variable names of every dimension, outermost first.  A
    /// multi-dimensional conformant array is one flat block whose row
    /// width is only known from the inner bounds, so indexing needs all of
    /// them and not just the outermost pair.
    std::vector<std::pair<std::string, std::string>> conformantDims{};
    /// The bound variables themselves, in the same order.
    ///
    /// The names above are what the programmer wrote in the parameter
    /// list, and re-resolving them wherever a subscript appears is what
    /// let an inner scope answer instead: a record with fields spelled
    /// `lo` and `hi` made `x[5]` inside `with r do` adjust by r.lo rather
    /// than by the array's own bound, and read out of the block.  The
    /// bound belongs to this activation and its address is known when the
    /// prologue creates it, so it is kept rather than looked up again.
    std::vector<std::pair<llvm::Value*, llvm::Value*>> conformantDimPtrs{};
    // EP §6.4.7: set for an undiscriminated schema formal parameter.  The
    // discriminants arrive as extra arguments, so they are plain Values
    // that stay live for the whole activation.
    const plang::Type*        schemaTy{nullptr};
    std::vector<llvm::Value*> schemaDiscs{};
    /// The names those discriminants were spilled to in the declaring
    /// procedure.  A nested procedure reaches an outer variable through a
    /// static link, which carries ADDRESSES: schemaDiscs above are the
    /// parent's own function arguments and mean nothing there, so a nested
    /// binding reloads them from these cells instead.
    std::vector<std::string> schemaDiscNames{};
    // ISO §6.6.3.1: set for a procedural or functional formal parameter.
    // ptr addresses a { ptr, ptr } cell holding the closure pair: where to
    // jump, and the frame the target reads its own outer variables
    // through.  The pair arrives in registers and could have been kept
    // there, but then a nested procedure could not reach it — a static
    // link carries one address per outer variable, and a value belonging
    // to another activation is not something this one may name.
    bool                       isProcParam{false};
    const plang::ProcedureTypeNode* procType{nullptr};
    /// EP §6.4.7: a `with`-bound field of a run-time-laid-out record has a
    /// capacity its object carries, and once bound it is an ordinary name
    /// with no path back to the object.  Recorded here so that
    /// `with p^ do s := ...` checks against the real capacity rather than
    /// the probe's string(1).  Last, and default-initialised, so that every
    /// existing aggregate initialisation of this struct is unaffected.
    llvm::Value*     strCapV{nullptr};
    /// EP §6.4.7: a `with`-bound component of a run-time-laid-out object.
    /// Binding it as a bare address loses the layout for anything reached
    /// THROUGH it -- an array field indexed against the probe's bounds, a
    /// nested record addressed by the probe struct -- so the denoter its
    /// extents are written in is kept, and schemaPathOf resumes from here.
    const plang::TypeNode*  pathDecl{nullptr};
    /// The schema the path above is rooted in.  Separate from schemaTy on
    /// purpose: schemaTy means "this NAME is a schematic object", which a
    /// with-bound FIELD is not.  Writing the root there made every bound
    /// field answer schemaRefOf, so indexing a fixed array field went
    /// looking for an array body on the enclosing RECORD and killed the
    /// compiler on a legal program.
    const plang::Type* pathRootTy{nullptr};
    std::vector<llvm::Value*> pathDiscs{};
    /// ISO §6.4.3.1 / issue #192: set when `ptr` was bound by a with-statement
    /// to a field of a PACKED record.  The address sits at a byte offset the
    /// field's own LLVM type need not satisfy -- exactly like an ordinary
    /// r.field access once the record is packed -- but once bound to a bare
    /// name the AST shows an IdentExpr with no FieldExpr left for
    /// packedAccessAlign (CGFieldAccess.cpp) to inspect, so the fact is kept
    /// here instead.  Mirrors Sema's own FromPackedWith (SymbolTable.h), set
    /// by the same with-statement field binding for the same underlying
    /// reason: that one blocks passing the name as a var parameter, this one
    /// blocks IRBuilder's default load/store alignment.  Last, and
    /// default-initialised, so that every existing aggregate initialisation
    /// of this struct is unaffected.
    bool packedWithField{false};
};
