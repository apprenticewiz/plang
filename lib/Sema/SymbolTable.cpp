#include "plang/Sema/SymbolTable.h"

#include "plang/Basic/StringUtil.h"

#include <utility>

using namespace plang;

std::string SymbolTable::lower(const std::string& S) { return toLower(S); }

void SymbolTable::pushScope(bool IsBlock) {
    Scopes.emplace_back();
    Scopes.back().IsBlock = IsBlock;
}

const Symbol* SymbolTable::lookupInEnclosingBlock(const std::string& Name) const {
    const std::string K = lower(Name);
    for (auto It = Scopes.rbegin(); It != Scopes.rend(); ++It) {
        const auto F = It->Symbols.find(K);
        // The first scope holding the name is the one the name denotes.  A hit
        // in a with-statement's scope is a field, not a declaration of the
        // block, and no outer declaration can rescue it: inside the with, the
        // name means the field.  Returning it here accepted a record field as
        // a for-statement's control variable.
        if (F != It->Symbols.end()) return It->IsBlock ? &F->second : nullptr;
        if (It->IsBlock) break;   // searched out to the block; no further
    }
    return nullptr;
}
void SymbolTable::popScope()  { if (!Scopes.empty()) Scopes.pop_back(); }

bool SymbolTable::define(Symbol Sym) {
    if (Scopes.empty()) return false;
    auto Key = lower(Sym.Name);
    auto& CurScope = Scopes.back();
    if (CurScope.Symbols.count(Key)) return false;
    Sym.ScopeDepth = Scopes.size();
    CurScope.Symbols.emplace(Key, std::move(Sym));
    return true;
}

const Symbol* SymbolTable::lookup(const std::string& Name) const {
    auto Key = lower(Name);
    // Under a ScopeCeiling only the outermost Ceiling scopes are visible, so a
    // denoter resolves where it was WRITTEN rather than where it is being used.
    const size_t Visible = Ceiling ? std::min(Ceiling, Scopes.size()) : Scopes.size();
    for (size_t I = Visible; I-- > 0; ) {
        auto Found = Scopes[I].Symbols.find(Key);
        if (Found != Scopes[I].Symbols.end()) return &Found->second;
    }
    return nullptr;
}

Symbol* SymbolTable::lookup(const std::string& Name) {
    return const_cast<Symbol*>(std::as_const(*this).lookup(Name));
}

const Symbol* SymbolTable::lookupCurrent(const std::string& Name) const {
    if (Scopes.empty()) return nullptr;
    auto Key = lower(Name);
    auto Found = Scopes.back().Symbols.find(Key);
    return (Found != Scopes.back().Symbols.end()) ? &Found->second : nullptr;
}

Symbol* SymbolTable::lookupCurrent(const std::string& Name) {
    return const_cast<Symbol*>(std::as_const(*this).lookupCurrent(Name));
}
