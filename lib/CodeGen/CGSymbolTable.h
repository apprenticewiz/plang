// CGSymbolTable.h — the variable/constant scope stack.
//
// scopes/consts/shadowedConsts/requiredConsts/curFuncScopeDepth all stay
// owned by Codegen::Impl -- referenced here, not
// moved -- because they are read/written directly (not through
// defVar/findVar) from ~20 external call sites (the closure-capture loop's
// "define, then re-find-by-name to mutate" idiom in CodeGenProcs.cpp,
// SchemaBindingScope's overlay in CodeGenTypes.cpp, ConstFold's
// evalConst/tryEvalConstInt callers). Moving that storage is real, deferred
// work -- see project memory on the CodeGen decomposition.
//
// defVar's -g half has finally split out into CGDebugInfo::declareLocal --
// this class holds a CGDebugInfo& and makes exactly one call into it, per
// the same "one fused entry point, not thirty two-call sites" reasoning
// that put defVar here in the first place.
#pragma once

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "CGDebugInfo.h"
#include "VarEntry.h"

namespace plang {
struct TypeNode;
struct ProcedureTypeNode;

// Turbo procedural TYPES: \p tn may name a procedural type through any
// number of `type Alias = OtherAlias;` hops (NamedTypeNode::Denotes, set by
// Sema::resolveNamed at every one of them) before finally being written out
// as `procedure(...)`/`function(...): T`. Shared between CGSymbolTable::
// defVar (a procedural VARIABLE's declared type, resolved once at the
// variable's own declaration) and CodeGenProcs.cpp's parameter-list walk (a
// procedural PARAMETER's declared type, fix for issue #543 -- a NAMED
// parameter type used to fail this same recognition and fall through to
// being evaluated as an implicit zero-argument call instead of having its
// address taken).  Returns null for anything that is not, eventually, a
// procedural type -- the overwhelming majority of callers' arguments.
const ProcedureTypeNode* resolveProcTypeAlias(const TypeNode* tn);

// Turbo Tier 5, issues #571/#623: the reserved CGSymbolTable binding name an
// unqualified call Sema resolved to an IMPLICIT receiver's own method
// (CallStmt/CallExpr::ImplicitMethodReceiverType) is found under --
// defined alongside 'Self' itself at a method's own entry
// (CodeGenProcs.cpp) and alongside an object with-target's own fields
// (CGWith.cpp), read back by CGProcCall::emitCallStmt/CGFuncCall::
// emitCallExpr.  A name distinct from "Self" (an explicit 'Self.Field'
// written inside a nested 'with objInstance do' must keep reaching the
// ENCLOSING method's own instance, never the with-target), but bound
// through the SAME CGSymbolTable scope stack "Self"/an ordinary field
// already is, so the innermost active receiver (an active with-block's own
// object, if any, else the enclosing method's own Self) is exactly what an
// unqualified call reaches -- the same shadowing priority a field of the
// same name already gets for free by being bound in the same nested
// scopes, under its own real name.  '$' can never appear in a Pascal
// identifier, so this can never collide with a name the program itself
// could write (same reasoning as openArrayLowBoundName/
// openArrayHighBoundName, AstType.h).
inline const char* implicitCallReceiverVarName() { return "$implicitcallrecv"; }
}

class CGSymbolTable {
public:
    CGSymbolTable(
        std::vector<std::unordered_map<std::string, VarEntry>>& Scopes,
        std::unordered_map<std::string, llvm::Value*>& Consts,
        std::vector<std::map<std::string, llvm::Value*>>& ShadowedConsts,
        std::set<std::string>& RequiredConsts,
        size_t& CurFuncScopeDepth,
        CGDebugInfo& DbgInfo)
        : Scopes(Scopes), Consts(Consts), ShadowedConsts(ShadowedConsts),
          RequiredConsts(RequiredConsts),
          CurFuncScopeDepth(CurFuncScopeDepth), DbgInfo(DbgInfo) {}

    void pushScope() { Scopes.emplace_back(); ShadowedConsts.emplace_back(); }
    void popScope() {
        // Put back any constant a variable in this scope was shadowing.
        if (!ShadowedConsts.empty() && ShadowedConsts.size() == Scopes.size()) {
            for (auto& [K, V] : ShadowedConsts.back()) Consts[K] = V;
            ShadowedConsts.pop_back();
        }
        if (!Scopes.empty()) Scopes.pop_back();
    }

    /// debugIndirectPtr: when non-null (only ever passed when debug info is
    /// active), a stable alloca holding ptr's own value, for a caller whose
    /// ptr is itself unstable (a bare SSA value -- a load result, or a raw
    /// Argument -- rather than an alloca/GlobalVariable).  Registers the
    /// debug declare against the alloca with a DW_OP_deref expression
    /// instead of against ptr directly with an empty one; ordinary codegen
    /// still reads/writes through ptr, unchanged.
    /// \p suppressDebugDecl: issue #142's one exception -- see CGDebugInfo::
    /// declareLocal's own \p suppress for why a schema var/value parameter
    /// needs its VarEntry bound here (typeNode and all) while its debug
    /// declaration is built separately, through declareSchemaParamRef.
    void defVar(const std::string& name, llvm::Value* ptr, llvm::Type* type,
                const plang::TypeNode* typeNode = nullptr,
                llvm::Value* debugIndirectPtr = nullptr,
                bool suppressDebugDecl = false);
    const VarEntry* findVar(const std::string& name) const;

    /// Marks the innermost (just-defined) binding of \p name as one whose
    /// address cannot claim its value type's ABI alignment -- a with-bound
    /// field of a packed record (VarEntry::packedWithField; see
    /// packedAccessAlign, CGFieldAccess.cpp).  Called right after defVar
    /// binds such a field, rather than growing defVar's own signature for
    /// the one caller (CGWith) that needs this: the same "define, then
    /// mutate the entry" idiom the closure-capture loop in
    /// CodeGenProcs.cpp already uses directly on Scopes, wrapped here
    /// because CGWith, like every decomposed unit, only ever sees this
    /// class and not Scopes itself.
    void markPackedWithField(const std::string& name);

    /// Whether \p name is bound by a scope opened INSIDE the current function
    /// body -- which in practice means a with-statement.
    [[nodiscard]] const VarEntry* findVarInFunctionScope(const std::string& name) const;
    [[nodiscard]] bool boundInsideFunction(const std::string& name) const;

    /// True if \p lowerName is a required constant that nothing has replaced.
    [[nodiscard]] bool isRequiredConst(const std::string& lowerName) const {
        return RequiredConsts.count(lowerName) != 0;
    }
    /// Records what a declared constant stands for.  A name declared here is
    /// the program's own from now on, whatever the language calls it.
    void defineConst(const std::string& name, llvm::Value* value);

private:
    std::vector<std::unordered_map<std::string, VarEntry>>& Scopes;
    std::unordered_map<std::string, llvm::Value*>& Consts;
    std::vector<std::map<std::string, llvm::Value*>>& ShadowedConsts;
    std::set<std::string>& RequiredConsts;
    size_t& CurFuncScopeDepth;
    CGDebugInfo& DbgInfo;
};
