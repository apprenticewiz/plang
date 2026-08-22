#include "CodeGenImpl.h"

#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"

using namespace plang;

namespace {
/// The target's data layout, or nothing when the target is unavailable.
///
/// A module without one gets LLVM's defaults, which give i64 a four-byte
/// alignment and so lay a record out differently from the machine llc will
/// generate for.  That disagreement is invisible until something in this
/// process reasons about offsets, at which point it silently reads the wrong
/// field — so the optimizer refuses to run without this; see optimize.
std::optional<llvm::DataLayout> layoutFor(const llvm::Triple& triple) {
    static const bool Init = [] {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        return true;
    }();
    (void)Init;

    std::string err;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, err);
    if (!target) return std::nullopt;

    std::unique_ptr<llvm::TargetMachine> tm(target->createTargetMachine(
        triple, "generic", "", llvm::TargetOptions{}, llvm::Reloc::PIC_));
    if (!tm) return std::nullopt;
    return tm->createDataLayout();
}
} // namespace

// ====================================================================
// Initialize / reset for a new module
// ====================================================================

void Codegen::Impl::init(const std::string& progName) {
    mod = std::make_unique<llvm::Module>(progName, ctx);
    llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
    mod->setTargetTriple(triple);
    if (auto dl = layoutFor(triple)) mod->setDataLayout(*dl);

    // Leaf units: constructed fresh here, after mod exists, rather than in
    // Impl's member-initializer list -- a unique_ptr<Module> stays null until
    // this point, so a unit needing one cannot be bound any earlier.  Fresh
    // construction also gives each a clean cache with no separate reset() --
    // and, for strings_ specifically, means its two caches start empty
    // together (the old strGVs/strStructGVs pair only ever cleared the
    // first of the two, harmless only because Impl is never reused for a
    // second emit()).
    runtimeFns_ = std::make_unique<RuntimeFunctionCache>(ctx, *mod);
    strings_    = std::make_unique<StringRuntime>(ctx, *mod, builder, *runtimeFns_,
        [this](int64_t cap){ return strStructType(cap); });
    // namePrefix/globalPrefix/currentUnit_ stay Impl fields (see linkage_'s
    // own constructor comment); importOwners_ is captured by value here,
    // valid because Codegen::setImportOwners is always called before
    // emit()/init() runs, never after.
    linkage_ = std::make_unique<CGLinkage>(*mod, namePrefix, globalPrefix,
        currentUnit_, importOwners_);
    schemaTypes_ = std::make_unique<SchemaTypeRegistry>();
    // LlvmTypeOfNode/arrayIndexRange are CGTypes territory (cgTypes_ below,
    // constructed later in this same init() once the common type aliases
    // and complexOps_/setOps_ exist) -- narrow closures rather than a
    // concrete dependency here, same seam every prior wave has used for a
    // unit built later in this ordering.  Each closure calls through(this),
    // which resolves to whatever CURRENTLY answers that method name at the
    // moment it's actually invoked -- always during real codegen, long
    // after init() has finished and cgTypes_ exists -- so no rebinding is
    // needed once Impl::llvmTypeOfNode/arrayIndexRange become one-line
    // forwarders to it.
    schemaLayout_ = std::make_unique<SchemaLayoutEngine>(ctx, *mod, builder,
        *schemaTypes_, *runtimeFns_,
        [this](const TypeNode& tn){ return llvmTypeOfNode(tn); },
        [this](const ArrayTypeNode& at){ return arrayIndexRange(at); });

    // srcMgr_/mainFileID_ captured by value here, valid because
    // Codegen::setSourceManager is always called before emit()/init() runs,
    // never after -- same ordering shape as importOwners_ above.  dbgInfo_
    // is constructed unconditionally; internally a no-op wherever
    // langOpts.Debug is unset.
    dbgInfo_ = std::make_unique<CGDebugInfo>(*mod, ctx, builder, langOpts,
        srcMgr_, mainFileID_, progName);

    // scopes/consts/shadowedConsts/requiredConsts/varLookupFloor_/
    // curFuncScopeDepth stay Impl fields (see CGSymbolTable.h's own
    // comment); symTab_ holds references into them plus a CGDebugInfo&,
    // through which defVar makes its one -g call.
    symTab_ = std::make_unique<CGSymbolTable>(
        scopes, consts, shadowedConsts, requiredConsts, varLookupFloor_,
        curFuncScopeDepth, *dbgInfo_);

    i1Ty  = llvm::Type::getInt1Ty(ctx);
    i8Ty  = llvm::Type::getInt8Ty(ctx);
    i32Ty = llvm::Type::getInt32Ty(ctx);
    i64Ty = llvm::Type::getInt64Ty(ctx);
    dblTy = llvm::Type::getDoubleTy(ctx);
    ptrTy = llvm::PointerType::get(ctx, 0); // opaque ptr (LLVM 15+)

    // ComplexOps needs dblTy/ptrTy by value, so it's constructed here rather
    // than alongside runtimeFns_/strings_ above.
    complexOps_ = std::make_unique<ComplexOps>(ctx, dblTy, ptrTy, builder, *runtimeFns_,
        [this](llvm::Value* v){ return toDouble(v); },
        [this](llvm::Type* t, const std::string& n){ return createEntryAlloca(t, n); });
    // RangeCheckGuards binds a reference into curFunc/nilChecks rather than a
    // copy, so it sees each function activation's/-fno-nil-checks' current
    // value with no rebinding needed later.
    rangeGuards_ = std::make_unique<RangeCheckGuards>(ctx, builder, curFunc,
        *runtimeFns_, *strings_, langOpts, nilChecks,
        [this](llvm::Value* v){ return toI64(v); });
    setOps_ = std::make_unique<SetOps>(ctx, *mod, builder,
        [this](llvm::Value* v){ return toI64(v); });
    // Built after complexOps_/setOps_ (needs ComplexOps&/SetOps& directly,
    // not closures, since both already exist by this point) and after the
    // common type aliases above (needs them by value).  typeAliases/consts
    // stay Impl fields, referenced -- typeAliases is copied/restored
    // wholesale in emitFunctionDef, and consts is touched directly by
    // SchemaBindingScope, both from outside this unit.
    cgTypes_ = std::make_unique<CGTypes>(ctx, *mod, langOpts, *schemaLayout_,
        *complexOps_, *setOps_, typeAliases, consts,
        i1Ty, i8Ty, i32Ty, i64Ty, dblTy, ptrTy);
    // Schema value/access-path resolution.  scopes stays an Impl field,
    // referenced -- touched directly by setVarStrCap/setVarSchemaPath from
    // outside this unit too.  EmitExpr/EmitLValue/EmitStrAddr/ToI64 and the
    // four string-shape predicates are narrow closures into methods that
    // are either not yet extracted (still Impl/CodeGenExprs.cpp/
    // CodeGenRuntime.cpp) or, for the predicates, deliberately staying put
    // (stateless, used far outside schema code too).
    // SchemaArgDiscCountOf stands in for direct paramMeta_ access: its
    // value type, ParamMeta, is used far outside this unit, so bridging
    // just this one derived query is narrower than giving ParamMeta a
    // free-standing header as a side effect of this extraction.
    schemaAccess_ = std::make_unique<SchemaAccess>(ctx, *mod, builder,
        *schemaTypes_, *schemaLayout_, *cgTypes_, *runtimeFns_, *strings_,
        *rangeGuards_, *symTab_, scopes, i64Ty, i8Ty, ptrTy,
        [this](const ExprNode& e){ return emitExpr(e); },
        [this](const ExprNode& e){ return emitLValue(e); },
        [this](const ExprNode& e){ return emitStrAddr(e); },
        [this](llvm::Value* v){ return toI64(v); },
        [this](const ExprNode& e){ return exprIsVarStr(e); },
        [this](const ExprNode& e){ return exprIsCharStr(e); },
        [this](const ExprNode& e){ return exprStrCap(e); },
        [this](const ExprNode& e){ return exprCharStrLen(e); },
        [this](const std::string& mangledName, size_t astArgIdx) -> unsigned {
            auto it = paramMeta_.find(mangledName);
            if (it == paramMeta_.end() || astArgIdx >= it->second.size()) return 0;
            return it->second[astArgIdx].schemaDiscCount;
        });
    // Call-argument marshalling + the EP string-store/address operations.
    // EmitExpr/EmitLValue/CreateEntryAlloca/CoerceToType are narrow
    // closures into methods not yet extracted (CodeGenExprs.cpp/
    // CodeGenTypes.cpp); the three string-shape predicates stay on Impl
    // (stateless, used far outside this unit too), same treatment
    // SchemaAccess already gives these same three.
    strCallMarshal_ = std::make_unique<StringCallMarshalling>(ctx, builder,
        *strings_, *rangeGuards_, *runtimeFns_, *cgTypes_, *schemaAccess_,
        i64Ty, ptrTy,
        [this](const ExprNode& e){ return emitExpr(e); },
        [this](const ExprNode& e){ return emitLValue(e); },
        [this](llvm::Type* t, const std::string& n){ return createEntryAlloca(t, n); },
        [this](llvm::Value* v, llvm::Type* t){ return coerceToType(v, t); },
        [this](const ExprNode& e){ return exprIsCharStr(e); },
        [this](const ExprNode& e){ return exprIsVarStr(e); },
        [this](const ExprNode& e){ return exprCharStrLen(e); });
    // Procedural-parameter ABI + conformant-array marshalling.
    // BuildStaticLinkFrame/IsNestedFunction deliberately don't absorb
    // buildStaticLinkFrame/nestedFunctions_ -- the closure-capture loop's
    // own state stays on Impl, this project's standing extra-caution zone.
    // EmitLValue/EmitCallArg/CreateEntryAlloca are narrow closures into
    // methods not yet extracted (CodeGenExprs.cpp/CodeGenRuntime.cpp/
    // CodeGenTypes.cpp).
    closureAbi_ = std::make_unique<ClosureAndCallABI>(ctx, *mod, builder,
        *schemaAccess_, *schemaLayout_, *cgTypes_, *symTab_, *linkage_,
        i32Ty, i64Ty, ptrTy,
        [this](const ExprNode& e){ return emitLValue(e); },
        [this](const ExprNode& e, llvm::Type* t, bool byRef){
            return emitCallArg(e, t, byRef); },
        [this](llvm::Type* t, const std::string& n){ return createEntryAlloca(t, n); },
        [this](const std::string& mangledName){ return buildStaticLinkFrame(mangledName); },
        [this](const std::string& mangledName){ return nestedFunctions_.count(mangledName) != 0; });
    // File-variable address/type/size helpers.  EmitLValue is the one
    // dependency not yet extracted (still CodeGenExprs.cpp).
    fileVarHelpers_ = std::make_unique<FileVarHelpers>(*mod, builder,
        *symTab_, *cgTypes_, *runtimeFns_, i64Ty, i8Ty, ptrTy,
        [this](const ExprNode& e){ return emitLValue(e); });
    // Built-in write/writeln/read/readln/writestr/readstr.  EmitExpr/
    // EmitLValue/ToI64/CoerceToType/CreateEntryAlloca are narrow closures
    // into methods not yet extracted (CodeGenExprs.cpp/CodeGenTypes.cpp);
    // the three string-shape predicates stay on Impl (stateless, used far
    // outside this unit too), same treatment SchemaAccess/
    // StringCallMarshalling already give these same three.
    builtinIO_ = std::make_unique<BuiltinIO>(ctx, *mod, builder,
        *fileVarHelpers_, *runtimeFns_, *strings_, *schemaAccess_,
        *strCallMarshal_, *complexOps_, *symTab_, *rangeGuards_, *cgTypes_,
        i8Ty, i64Ty, dblTy, ptrTy,
        [this](const ExprNode& e){ return emitExpr(e); },
        [this](const ExprNode& e){ return emitLValue(e); },
        [this](llvm::Value* v){ return toI64(v); },
        [this](llvm::Value* v, llvm::Type* t){ return coerceToType(v, t); },
        [this](llvm::Type* t, const std::string& n){ return createEntryAlloca(t, n); },
        [this](const ExprNode& e){ return exprIsVarStr(e); },
        [this](const ExprNode& e){ return exprIsCharStr(e); },
        [this](const ExprNode& e){ return exprCharStrLen(e); });
    // DefineBuf/LookupBuf are narrow closures into defVar/findVar
    // (CGSymbolTable territory, not yet extracted) -- LookupBuf hands back
    // just the llvm::Value*, the only field of a VarEntry this engine needs.
    gotoEngine_ = std::make_unique<LabelGotoEngine>(ctx, *mod, builder, curFunc,
        *runtimeFns_,
        [this](const std::string& n, llvm::Value* p, llvm::Type* t){ defVar(n, p, t); },
        [this](const std::string& n) -> llvm::Value* {
            const auto* ve = findVar(n); return ve ? ve->ptr : nullptr; });

    scopes.clear();
    consts.clear();

    // Predefined Pascal constants (ISO 7185 + EP).  ISO §6.2.2.10: these are
    // declared in a region enclosing the program, so a program that declares
    // one of the names again means its own — which is what requiredConsts
    // records, so that a lookup can tell the two apart.
    requiredConsts.clear();
    // The largest value the dialect's integer holds.  Sema knows this too,
    // from the same LangOptions; the two agreeing is what keeps `to maxint`
    // terminating rather than wrapping.
    consts["maxint"]  = llvm::ConstantInt::get(
        llvm::Type::getIntNTy(ctx, langOpts.defaultIntWidth()),
        static_cast<uint64_t>(~0ULL >> (64 - langOpts.defaultIntWidth() + 1)),
        /*isSigned=*/true);
    consts["pi"]      = llvm::ConstantFP::get(dblTy, std::numbers::pi);
    // EP §6.4.2.2
    consts["maxchar"] = llvm::ConstantInt::get(i8Ty,  255, /*isSigned=*/false);
    consts["minreal"] = llvm::ConstantFP::get(dblTy, DBL_MIN);
    consts["maxreal"] = llvm::ConstantFP::get(dblTy, DBL_MAX);
    consts["epsreal"] = llvm::ConstantFP::get(dblTy, DBL_EPSILON);
    for (const auto& [name, _] : consts) requiredConsts.insert(name);

    // structTypes/strStructTypes used to be cleared here too; now they're
    // CGTypes-private, and cgTypes_'s own fresh construction above already
    // gives them a clean cache, same as every other leaf unit.
    typeAliases.clear();
    curFunc = nullptr;
    curRetAlloca = nullptr;
    curRetType = nullptr;
    curFuncName.clear();
    namePrefix   = PlangProcPrefix;
    globalPrefix = PlangGlobalPrefix;
    currentUnit_.clear();
    moduleGlobals_.clear();
}

// ====================================================================
// Symbol table
// ====================================================================

void Codegen::Impl::defVar(const std::string& name, llvm::Value* ptr, llvm::Type* type,
                            const TypeNode* typeNode, llvm::Value* debugIndirectPtr) {
    symTab_->defVar(name, ptr, type, typeNode, debugIndirectPtr);
}

const VarEntry* Codegen::Impl::findVar(const std::string& name) const {
    return symTab_->findVar(name);
}

const VarEntry*
Codegen::Impl::resolveImportedVar(const std::string& name, const Type* semaTy) {
    const std::string owner = importOwner(name);
    // What the declaring module calls it, which is what it emitted the global
    // under; EP §6.11.2 renaming makes that differ from the name in hand.
    const std::string bare  = toLower(importLinkName(name));

    // A module compiled alongside this one already emitted the variable, and
    // its entry carries the TypeNode, which the Sema type alone cannot supply
    // and which file and string accesses need.
    if (!owner.empty()) {
        auto it = moduleGlobals_.find(owner + "." + bare);
        if (it != moduleGlobals_.end()) {
            defVar(name, it->second.ptr, it->second.type, it->second.typeNode);
            return findVar(name);
        }
    }

    // Otherwise the owning module was compiled separately; declare the symbol
    // and let the linker match it up.
    if (!semaTy || semaTy->isError()) return nullptr;
    const std::string gname = mangledGlobal(name);
    auto* gv = mod->getGlobalVariable(gname);
    if (!gv)
        gv = new llvm::GlobalVariable(*mod, llvmTypeOfSemaType(*semaTy),
                                       /*isConst=*/false,
                                       llvm::GlobalValue::ExternalLinkage,
                                       nullptr, gname);
    defVar(name, gv, gv->getValueType(), nullptr);
    return findVar(name);
}

// ====================================================================
// Alloca helpers
// ====================================================================

llvm::AllocaInst* Codegen::Impl::createEntryAlloca(llvm::Type* ty, const std::string& name) {
    // InsertPointGuard, not a bare saveIP/restoreIP pair: SetInsertPoint at
    // an existing instruction (entry.begin(), here, since the entry block
    // already has the prologue's allocas in it) also inherits THAT
    // instruction's !dbg, and plain restoreIP does not restore it back --
    // silently mis-scoping every instruction emitted afterward, in the
    // *caller's* block, until the next statement boundary corrects it.  A
    // hoisted alloca has no source line of its own worth keeping regardless.
    llvm::IRBuilderBase::InsertPointGuard guard(builder);
    auto& entry = curFunc->getEntryBlock();
    builder.SetInsertPoint(&entry, entry.begin());
    return builder.CreateAlloca(ty, nullptr, name);
}

llvm::Value* Codegen::Impl::createDynStrAlloca(llvm::Value* capV,
                                               const std::string& name) {
    // The header is 8 bytes and the payload is the capacity, rounded up so the
    // next thing on the stack stays aligned -- the same shape strStructType
    // builds, just measured rather than declared.
    auto* bytes = alignUpV(builder.CreateAdd(llvm::ConstantInt::get(i64Ty, 8),
                                             capV, "str.tmp.size"), 8);
    dynAllocaUsed_ = true;
    auto* mem = builder.CreateAlloca(i8Ty, bytes, name);
    mem->setAlignment(llvm::Align(8));
    // A fresh temporary has no characters in it yet, and every runtime entry
    // point reads the length before it writes one.
    builder.CreateStore(llvm::ConstantInt::get(i64Ty, 0), mem);
    return mem;
}

// The save is emitted up front and thrown away again if it turned out to be
// unnecessary.  Recording an insertion point and coming back to it does not
// work: a scope that opens on an empty block records end(), and end() is still
// end() once the statement has filled the block, so the save landed after the
// block's own branch and the IR did not verify.
Codegen::Impl::StackScope::StackScope(Impl& I)
        : I(I), SavedUsed(I.dynAllocaUsed_) {
    I.dynAllocaUsed_ = false;
    Save = I.builder.CreateStackSave("stack.mark");
}

Codegen::Impl::StackScope::~StackScope() {
    if (I.dynAllocaUsed_ && !I.isTerminated()) {
        I.builder.CreateStackRestore(Save);
        // Given back here, so an enclosing scope has nothing left to give back
        // on this account.
        I.dynAllocaUsed_ = false;
    } else if (Save->use_empty()) {
        // Nothing took a dynamic allocation, so the mark is dead.  Removing it
        // is what lets this scope sit on every simple statement without costing
        // anything in the ordinary case -- and it is why the IR for a program
        // with no schema strings in it is unchanged.
        auto* decl = llvm::cast<llvm::CallInst>(Save)->getCalledFunction();
        Save->eraseFromParent();
        // The DECLARATION outlives the call it was created for, and a module
        // carrying an unused `declare ptr @llvm.stacksave.p0()` is not the
        // module it was before.  The IR gate caught exactly that: 116 ISO 7185
        // listings differing by a declaration and an attribute group and
        // nothing else.  A later scope that needs it will declare it again.
        if (decl && decl->isDeclaration() && decl->use_empty())
            decl->eraseFromParent();
    }
    I.dynAllocaUsed_ = SavedUsed || I.dynAllocaUsed_;
}
