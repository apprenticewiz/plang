#include "CGSymbolTable.h"

#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/StringUtil.h"

using namespace plang;

namespace plang {
// See this function's own comment, CGSymbolTable.h -- shared with
// CodeGenProcs.cpp's parameter-list walk (issue #543), so it lives here
// rather than in this file's own anonymous namespace the way it used to
// when defVar was its only caller.
const ProcedureTypeNode* resolveProcTypeAlias(const TypeNode* tn) {
    while (tn) {
        if (auto* pt = llvm::dyn_cast<ProcedureTypeNode>(tn)) return pt;
        auto* nt = llvm::dyn_cast<NamedTypeNode>(tn);
        if (!nt || !nt->Denotes) return nullptr;
        tn = nt->Denotes;
    }
    return nullptr;
}
} // namespace plang

void CGSymbolTable::defVar(const std::string& name, llvm::Value* ptr, llvm::Type* type,
                            const TypeNode* typeNode, llvm::Value* debugIndirectPtr,
                            bool suppressDebugDecl) {
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
    // Issue #19: a name already bound in THIS scope frame, about to be
    // overwritten, means a nested procedure's own parameter or local
    // shares a captured outer variable's spelling.  The closure-capture
    // loop (CodeGenProcs.cpp) binds every outer variable this activation
    // can see into this same Scopes.back() before this activation's own
    // parameters/locals are bound, and an ordinary same-block
    // redeclaration is caught earlier, by Sema -- so this is the only
    // shape that reaches here.  The map write below is already correct
    // either way (findVar always answers with whichever entry was written
    // last), but without this, the two DILocalVariables declareLocal
    // builds -- one for the capture, one for the shadowing declaration --
    // would land flatly under the same DISubprogram, and a debugger
    // resolving unqualified `x` prefers the first (the captured, outer,
    // WRONG one) regardless of which the current PC is actually inside.
    // Opening a lexical block for the rest of this activation gives the
    // shadowing declaration somewhere strictly innermost to live instead.
    if (Scopes.back().count(Key))
        DbgInfo.enterShadowScope(typeNode ? typeNode->Loc : plang::SourceLocation{});
    VarEntry VE{ ptr, type, typeNode, name };
    // Turbo procedural VALUES: a procedural PARAMETER's own {entry point,
    // frame} cell is registered through this same function but always with
    // typeNode == nullptr (CodeGenProcs.cpp's parameter loop, which sets
    // isProcParam/procType itself afterward -- see its own comment for why),
    // so resolveProcTypeAlias(nullptr) answers null there and this is a
    // no-op for it; only a genuinely declared VARIABLE reaches this branch.
    if (const auto* pt = resolveProcTypeAlias(typeNode)) {
        VE.isProcVar = true;
        VE.procType  = pt;
    }
    Scopes.back()[Key] = std::move(VE);

    DbgInfo.declareLocal(name, typeNode, ptr, debugIndirectPtr, suppressDebugDecl);
}

void CGSymbolTable::markPackedWithField(const std::string& name) {
    if (Scopes.empty()) return;
    auto it = Scopes.back().find(toLower(name));
    if (it != Scopes.back().end()) it->second.packedWithField = true;
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
