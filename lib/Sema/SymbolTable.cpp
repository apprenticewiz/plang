#include "plang/Sema/SymbolTable.h"

#include "plang/Basic/StringUtil.h"

#include <utility>

using namespace plang;

std::string SymbolTable::lower(const std::string& S) { return toLower(S); }

void SymbolTable::pushScope() { Scopes.emplace_back(); }
void SymbolTable::popScope()  { if (!Scopes.empty()) Scopes.pop_back(); }

bool SymbolTable::define(Symbol Sym) {
    if (Scopes.empty()) return false;
    auto Key = lower(Sym.Name);
    auto& CurScope = Scopes.back();
    if (CurScope.Symbols.count(Key)) return false;
    CurScope.Symbols.emplace(Key, std::move(Sym));
    return true;
}

const Symbol* SymbolTable::lookup(const std::string& Name) const {
    auto Key = lower(Name);
    for (auto It = Scopes.rbegin(); It != Scopes.rend(); ++It) {
        auto Found = It->Symbols.find(Key);
        if (Found != It->Symbols.end()) return &Found->second;
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
