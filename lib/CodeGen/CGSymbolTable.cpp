#include "CGSymbolTable.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/StringUtil.h"

using namespace plang;

void CGSymbolTable::defVar(const std::string& name, llvm::Value* ptr, llvm::Type* type,
                            const TypeNode* typeNode, llvm::Value* debugIndirectPtr) {
    if (Scopes.empty()) return;
    const std::string Key = toLower(name);
    // A variable of this name hides a constant of it for as long as the scope
    // lasts.  See ShadowedConsts: the constant table is flat, so without this
    // the constant answered every read while the writes went to the variable.
    if (const auto It = Consts.find(Key); It != Consts.end()) {
        if (ShadowedConsts.size() == Scopes.size()
                && !ShadowedConsts.back().count(Key))
            ShadowedConsts.back()[Key] = It->second;
        Consts.erase(It);
    }
    Scopes.back()[Key] = VarEntry{ ptr, type, typeNode, name };

    DbgInfo.declareLocal(name, typeNode, ptr, debugIndirectPtr);
}

const VarEntry* CGSymbolTable::findVar(const std::string& name) const {
    std::string key = toLower(name);
    for (size_t i = Scopes.size(); i-- > 0;) {
        auto f = Scopes[i].find(key);
        if (f != Scopes[i].end()) return &f->second;
    }
    return nullptr;
}

const VarEntry* CGSymbolTable::findVarInFunctionScope(const std::string& name) const {
    const std::string K = toLower(name);
    const size_t Start = CurFuncScopeDepth ? CurFuncScopeDepth - 1 : 0;
    for (size_t i = Start + 1; i-- > 0;) {
        const auto It = Scopes[i].find(K);
        if (It != Scopes[i].end()) return &It->second;
    }
    return nullptr;
}

bool CGSymbolTable::boundInsideFunction(const std::string& name) const {
    for (size_t i = Scopes.size(); i-- > CurFuncScopeDepth;)
        if (Scopes[i].count(toLower(name))) return true;
    return false;
}

void CGSymbolTable::defineConst(const std::string& name, llvm::Value* value) {
    const std::string lo = toLower(name);
    Consts[lo] = value;
    RequiredConsts.erase(lo);
}
