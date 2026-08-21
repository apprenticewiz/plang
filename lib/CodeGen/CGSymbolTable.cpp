#include "CGSymbolTable.h"

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/SourceManager.h"
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

    // -g: the single choke point every named Pascal variable, parameter,
    // local, captured outer variable and with-bound field passes through,
    // so this is the one place a DILocalVariable/DIGlobalVariableExpression
    // needs building rather than one per caller.  A parameter is registered
    // as an auto variable, not a formal parameter: preserving that
    // distinction (info args vs. info locals) would mean threading an
    // ArgNo through every one of defVar's ~30 call sites for a purely
    // cosmetic difference -- print <name> finds either kind identically.
    // ptr is documented (see VarEntry::ptr) to always be an address -- an
    // alloca, a GlobalVariable, or (for a var parameter) the argument
    // itself, already a pointer at the LLVM level -- so it is always valid
    // to declare a variable's location at, whichever case this is.  Valid,
    // but not always STABLE: an alloca's own value is a compile-time-fixed
    // frame offset, good for the whole function, but a bare SSA value (a
    // var parameter's raw Argument, or a captured variable's loaded
    // pointer) is subject to ordinary register allocation/live-range
    // splitting like any other value, so LLVM can only describe it with a
    // location list valid for whatever narrow PC range the backend happens
    // to keep it live -- outside that range a debugger sees "optimized
    // out" at best, or (confirmed live, for a captured variable inspected
    // from inside the capturing procedure) silently wrong data at worst,
    // with no diagnostic either way.  debugIndirectPtr is the caller's fix
    // for its own unstable ptr: a fresh alloca (stable) that already holds
    // ptr's value, so declaring through it with one DW_OP_deref reaches
    // the exact same address as declaring against ptr directly would,
    // just via a stable hop.
    if (DBuilderRef && typeNode && typeNode->ResolvedType) {
        if (auto* DT = DebugTypeOf(*typeNode->ResolvedType)) {
            const unsigned line = SrcMgr
                ? SrcMgr->getPresumedLoc(typeNode->Loc).Line : 0;
            if (auto* GV = llvm::dyn_cast<llvm::GlobalVariable>(ptr)) {
                auto* GVE = DBuilderRef->createGlobalVariableExpression(
                    DebugCURef, name, /*LinkageName=*/"", DebugFileRef, line, DT,
                    /*IsLocalToUnit=*/false);
                GV->addDebugInfo(GVE);
            } else if (CurrentDebugScopeRef && B.GetInsertBlock()) {
                auto* DV = DBuilderRef->createAutoVariable(
                    CurrentDebugScopeRef, name, DebugFileRef, line, DT);
                llvm::Value* storage = debugIndirectPtr ? debugIndirectPtr : ptr;
                auto* expr = debugIndirectPtr
                    ? DBuilderRef->createExpression({llvm::dwarf::DW_OP_deref})
                    : DBuilderRef->createExpression();
                DBuilderRef->insertDeclare(
                    storage, DV, expr,
                    llvm::DILocation::get(Ctx, line, 0, CurrentDebugScopeRef),
                    B.GetInsertBlock());
            }
        }
    }
}

const VarEntry* CGSymbolTable::findVar(const std::string& name) const {
    std::string key = toLower(name);
    // Down to VarLookupFloor and no further; see DeclarationScopeOnly.
    for (size_t i = Scopes.size(); i-- > VarLookupFloor;) {
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
