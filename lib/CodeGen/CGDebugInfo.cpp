#include "CGDebugInfo.h"

#include <filesystem>

#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/SourceManager.h"
#include "plang/Basic/Version.h"
#include "plang/Sema/Type.h"

using namespace plang;

namespace {
/// Splits a source buffer's name into the (filename, directory) pair
/// DIFile wants, resolving a relative name against the working directory
/// so a debugger started somewhere else can still find the file.  Only
/// path-string manipulation and the (error_code-checked, non-throwing)
/// current directory lookup -- PlangCodeGen is built -fno-exceptions, and
/// std::filesystem's throwing overloads are not safe to call from it.
std::pair<std::string, std::string> splitDebugFilePath(std::string_view Name) {
    std::filesystem::path P(Name);
    if (P.is_relative()) {
        std::error_code ec;
        auto cwd = std::filesystem::current_path(ec);
        if (!ec) P = cwd / P;
    }
    return {P.filename().string(), P.parent_path().string()};
}
} // namespace

CGDebugInfo::CGDebugInfo(llvm::Module& Mod, llvm::LLVMContext& Ctx, llvm::IRBuilder<>& B,
                          const LangOptions& Opts, const SourceManager* SrcMgr,
                          FileID MainFileID, const std::string& ProgName)
    : Ctx(Ctx), B(B), Opts(Opts), SrcMgr(SrcMgr) {
    if (!Opts.Debug) return;
    DBuilder = std::make_unique<llvm::DIBuilder>(Mod);
    std::string Filename = ProgName, Directory;
    if (SrcMgr)
        std::tie(Filename, Directory) = splitDebugFilePath(SrcMgr->getBufferName(MainFileID));
    DebugFile = DBuilder->createFile(Filename, Directory);
    DebugCU = DBuilder->createCompileUnit(
        llvm::DISourceLanguageName(llvm::dwarf::DW_LANG_Pascal83),
        DebugFile, "plang " PLANG_VERSION_STRING,
        /*isOptimized=*/Opts.OptLevel > 0, /*Flags=*/"", /*RV=*/0);
    // DWARF cannot be read back without a producer that states which
    // version of the metadata schema it wrote; DIBuilder's own nodes
    // say nothing about this on their own.
    Mod.addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                       llvm::DEBUG_METADATA_VERSION);
}

// -g.  Composite kinds (Record, Array, Set, File, ...) fall through to
// null: field-level detail for them is explicitly out of scope for this
// pass (a natural, clearly-separated fast-follow -- see the phase this
// shipped in), and a pointer to one gets no DIType for its pointee rather
// than a placeholder invented for the occasion.  A null pointee is a
// documented, ordinary createPointerType input (a C `void*`'s own DIType
// is built the same way), not a special case this function has to guard.
llvm::DIType* CGDebugInfo::debugTypeOfSemaType(const Type& T) {
    if (!DBuilder) return nullptr;
    if (auto it = debugTypes_.find(&T); it != debugTypes_.end()) return it->second;

    llvm::DIType* DT = nullptr;
    switch (T.Kind) {
        case TypeKind::Integer:
            DT = DBuilder->createBasicType("integer", 64, llvm::dwarf::DW_ATE_signed);
            break;
        case TypeKind::Real:
            DT = DBuilder->createBasicType("real", 64, llvm::dwarf::DW_ATE_float);
            break;
        case TypeKind::Boolean:
            DT = DBuilder->createBasicType("boolean", 8, llvm::dwarf::DW_ATE_boolean);
            break;
        case TypeKind::Char:
            DT = DBuilder->createBasicType("char", 8, llvm::dwarf::DW_ATE_unsigned_char);
            break;
        case TypeKind::Enum: {
            std::vector<llvm::Metadata*> Elements;
            Elements.reserve(T.EnumValues.size());
            // The ordinal of each name is its own index -- see isValid's
            // sibling read of this same field, EnumValues[V], elsewhere in
            // this file: there is no separately-stored value to read here.
            for (size_t I = 0; I < T.EnumValues.size(); ++I)
                Elements.push_back(DBuilder->createEnumerator(
                    T.EnumValues[I], static_cast<uint64_t>(I)));
            DT = DBuilder->createEnumerationType(
                DebugFile, T.Name, DebugFile, /*LineNumber=*/0,
                /*SizeInBits=*/64, /*AlignInBits=*/64,
                DBuilder->getOrCreateArray(Elements),
                DBuilder->createBasicType("integer", 64, llvm::dwarf::DW_ATE_signed));
            break;
        }
        case TypeKind::Subrange:
            // The base ordinal type's own DIType, not a qualified DWARF
            // subrange encoding: enough to print a value correctly, which
            // is the bar this pass clears; the bound information itself
            // is a nice-to-have left for a future pass.
            DT = T.SubBase ? debugTypeOfSemaType(*T.SubBase) : nullptr;
            break;
        case TypeKind::Pointer: {
            // No cycle risk despite the direct (non-RAUW) recursion: a
            // composite pointee (the only way a real cycle could arise --
            // Pascal has no way to write `type P = ^P;` directly) hits the
            // default case above and returns immediately, without
            // recursing into whatever it itself contains.
            llvm::DIType* PointeeDT = T.PointeeType
                ? debugTypeOfSemaType(*T.PointeeType) : nullptr;
            DT = DBuilder->createPointerType(PointeeDT, 64);
            break;
        }
        default:
            break;
    }
    debugTypes_[&T] = DT;
    return DT;
}

llvm::DISubprogram* CGDebugInfo::emitFunctionStart(llvm::Function* Fn, llvm::DIScope* Scope,
                                                    const std::string& Name,
                                                    SourceLocation Loc) {
    if (!DBuilder) return nullptr;
    const unsigned Line = SrcMgr ? SrcMgr->getPresumedLoc(Loc).Line : 0;
    auto* SubTy = DBuilder->createSubroutineType(
        DBuilder->getOrCreateTypeArray({}));
    auto* SP = DBuilder->createFunction(
        Scope, Name, Fn->getName(), DebugFile, Line, SubTy, Line,
        llvm::DINode::FlagZero,
        llvm::DISubprogram::SPFlagDefinition
            | (Opts.OptLevel > 0 ? llvm::DISubprogram::SPFlagOptimized
                                  : llvm::DISubprogram::SPFlagZero));
    Fn->setSubprogram(SP);
    // B's current debug location is not function-scoped state -- it
    // survives across whichever function was emitted right before this one,
    // and anything emitted before this function's own first emitStmt call
    // (a parameter-to-alloca copy, a file-parameter bind) would otherwise
    // inherit that PREVIOUS function's last statement location, scoped to
    // its own, different DISubprogram.  The verifier rejects that outright
    // ("!dbg attachment points at wrong subprogram for function"), not just
    // misattributes a line -- caught by compiling a program with more than
    // one procedure under -g, which no test before this one exercised.
    B.SetCurrentDebugLocation(llvm::DILocation::get(Ctx, Line, 0, SP));
    return SP;
}

llvm::DISubprogram* CGDebugInfo::emitThunkStart(llvm::Function* Fn, llvm::DIScope* Scope,
                                                 const std::string& Name) {
    if (!DBuilder) return nullptr;
    auto* SubTy = DBuilder->createSubroutineType(
        DBuilder->getOrCreateTypeArray({}));
    auto* SP = DBuilder->createFunction(
        Scope, Name, Fn->getName(), DebugFile, /*LineNo=*/0, SubTy, /*ScopeLine=*/0,
        llvm::DINode::FlagArtificial,
        llvm::DISubprogram::SPFlagDefinition
            | (Opts.OptLevel > 0 ? llvm::DISubprogram::SPFlagOptimized
                                  : llvm::DISubprogram::SPFlagZero));
    Fn->setSubprogram(SP);
    return SP;
}

void CGDebugInfo::declareLocal(const std::string& name, const TypeNode* typeNode,
                                llvm::Value* ptr, llvm::Value* debugIndirectPtr) {
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
    if (!DBuilder || !typeNode || !typeNode->ResolvedType) return;
    auto* DT = debugTypeOfSemaType(*typeNode->ResolvedType);
    if (!DT) return;
    const unsigned line = SrcMgr ? SrcMgr->getPresumedLoc(typeNode->Loc).Line : 0;
    if (auto* GV = llvm::dyn_cast<llvm::GlobalVariable>(ptr)) {
        // A module's own global is declared exactly once, through its own
        // defVar -- but an EP module importing it (Codegen::Impl::
        // resolveImportedVar's "compiled alongside this one" branch) binds
        // the SAME llvm::GlobalVariable into ITS OWN symbol table by
        // calling defVar again, under whatever local/qualified name the
        // importer spells it, which reaches this same choke point a
        // second time for one storage location. This project builds one
        // llvm::Module (and so one DICompileUnit) for a whole program, not
        // one per Pascal module, so there is no "declaration in the
        // importing CU, definition in the owning one" split to give a
        // second DW_TAG_variable a legitimate reason to exist (the Clang
        // precedent for an extern declared in one TU and used in
        // another): a second DIGlobalVariableExpression here is just a
        // spurious duplicate, under the IMPORTER's own alias rather than
        // the variable's real declared name, and (confirmed with
        // llvm-dwarfdump on a two-module program) a bogus line -- the
        // typeNode passed for a re-import is null (see
        // resolveImportedVar), so `line` above is always 0, not the
        // variable's real declaring line, regardless of which import
        // reaches here first.  GlobalVariable::getDebugInfo (not a bare
        // hasMetadata/MD_dbg check, so a global that has SOME unrelated
        // metadata but no debug info yet still gets its first, correct
        // declaration) is the guard: skip whenever this GV already has
        // one, no matter which name/typeNode is in hand this time.
        llvm::SmallVector<llvm::DIGlobalVariableExpression*, 1> existing;
        GV->getDebugInfo(existing);
        if (existing.empty()) {
            auto* GVE = DBuilder->createGlobalVariableExpression(
                DebugCU, name, /*LinkageName=*/"", DebugFile, line, DT,
                /*IsLocalToUnit=*/false);
            GV->addDebugInfo(GVE);
        }
    } else if (CurScope && B.GetInsertBlock()) {
        auto* DV = DBuilder->createAutoVariable(CurScope, name, DebugFile, line, DT);
        llvm::Value* storage = debugIndirectPtr ? debugIndirectPtr : ptr;
        auto* expr = debugIndirectPtr
            ? DBuilder->createExpression({llvm::dwarf::DW_OP_deref})
            : DBuilder->createExpression();
        DBuilder->insertDeclare(
            storage, DV, expr,
            llvm::DILocation::get(Ctx, line, 0, CurScope),
            B.GetInsertBlock());
    }
}

llvm::DILocalScope* CGDebugInfo::enterShadowScope(SourceLocation Loc) {
    // No-op when Debug is unset, matching every other method's convention.
    if (!DBuilder) return CurScope;
    // Idempotent: a second shadowed name later in the same activation
    // finds CurScope already a lexical block and reuses it -- see this
    // method's own header comment for why sharing one block is correct
    // here, not just convenient.  isa_and_nonnull rather than isa: this
    // is only ever reached from inside some activation's own scope (see
    // CGSymbolTable::defVar), so CurScope is never actually null here,
    // but nothing about a debug-info helper should assert on a defensive
    // caller's behalf.
    if (llvm::isa_and_nonnull<llvm::DILexicalBlock>(CurScope)) return CurScope;
    const unsigned line = SrcMgr ? SrcMgr->getPresumedLoc(Loc).Line : 0;
    CurScope = DBuilder->createLexicalBlock(CurScope, DebugFile, line, 0);
    return CurScope;
}

void CGDebugInfo::setLocation(SourceLocation Loc) {
    if (!DBuilder || !CurScope || !SrcMgr) return;
    const PresumedLoc PL = SrcMgr->getPresumedLoc(Loc);
    if (PL.isValid())
        B.SetCurrentDebugLocation(llvm::DILocation::get(Ctx, PL.Line, PL.Column, CurScope));
}
