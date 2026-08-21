// CGSymbolTable.h — the variable/constant scope stack.
//
// scopes/consts/shadowedConsts/requiredConsts/varLookupFloor_/
// curFuncScopeDepth all stay owned by Codegen::Impl -- referenced here, not
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
}

class CGSymbolTable {
public:
    CGSymbolTable(
        std::vector<std::unordered_map<std::string, VarEntry>>& Scopes,
        std::unordered_map<std::string, llvm::Value*>& Consts,
        std::vector<std::map<std::string, llvm::Value*>>& ShadowedConsts,
        std::set<std::string>& RequiredConsts,
        size_t& VarLookupFloor,
        size_t& CurFuncScopeDepth,
        CGDebugInfo& DbgInfo)
        : Scopes(Scopes), Consts(Consts), ShadowedConsts(ShadowedConsts),
          RequiredConsts(RequiredConsts), VarLookupFloor(VarLookupFloor),
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

    /// Hide every enclosing variable scope for the duration, leaving only the
    /// scope just pushed.  Constants are a separate table and stay visible,
    /// which is precisely the set of names a schema body may legally use.
    class DeclarationScopeOnly {
    public:
        explicit DeclarationScopeOnly(CGSymbolTable& T)
            : T(T), Saved(T.VarLookupFloor) {
            T.VarLookupFloor = T.Scopes.empty() ? 0 : T.Scopes.size() - 1;
            // Hiding the variable is not enough on its own.  defVar does not
            // merely shadow a constant of the same spelling -- it REMOVES it
            // from `consts` and parks it in shadowedConsts until the scope
            // closes.  So with the variable hidden and the constant still
            // parked, the body's name resolved to nothing at all and codegen
            // emitted a reference to a global that never existed
            // ("undefined symbol: pasg_k").  Put the parked constants back
            // for the duration: they are what the declaration scope would
            // have had.  EVERY level, not just the ones above the floor: the
            // variable that parked the constant lives in the scope we are
            // hiding, which is below it.  Exactly one level can hold a given
            // constant -- once erased, an inner defVar finds nothing left to
            // park -- so there is no ambiguity about which saved value is
            // the original.
            for (const auto& Level : T.ShadowedConsts)
                for (const auto& [K, V] : Level)
                    if (!T.Consts.count(K)) { T.Consts[K] = V; Restored.push_back(K); }
        }
        ~DeclarationScopeOnly() {
            for (const auto& K : Restored) T.Consts.erase(K);
            T.VarLookupFloor = Saved;
        }
        DeclarationScopeOnly(const DeclarationScopeOnly&) = delete;
        DeclarationScopeOnly& operator=(const DeclarationScopeOnly&) = delete;
    private:
        CGSymbolTable& T;
        size_t Saved;
        std::vector<std::string> Restored;
    };

    /// debugIndirectPtr: when non-null (only ever passed when debug info is
    /// active), a stable alloca holding ptr's own value, for a caller whose
    /// ptr is itself unstable (a bare SSA value -- a load result, or a raw
    /// Argument -- rather than an alloca/GlobalVariable).  Registers the
    /// debug declare against the alloca with a DW_OP_deref expression
    /// instead of against ptr directly with an empty one; ordinary codegen
    /// still reads/writes through ptr, unchanged.
    void defVar(const std::string& name, llvm::Value* ptr, llvm::Type* type,
                const plang::TypeNode* typeNode = nullptr,
                llvm::Value* debugIndirectPtr = nullptr);
    const VarEntry* findVar(const std::string& name) const;

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
    size_t& VarLookupFloor;
    size_t& CurFuncScopeDepth;
    CGDebugInfo& DbgInfo;
};
