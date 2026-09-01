#include "ClosureAndCallABI.h"

#include <optional>

#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Sema/Type.h"

#include "CGCallMarshal.h"
#include "CodegenICE.h"

using namespace plang;

using SchemaPath = SchemaAccess::SchemaPath;

void ClosureAndCallABI::storeProcPair(llvm::Value* cell, llvm::Value* fn,
                                       llvm::Value* frame) {
    auto* zero = llvm::ConstantInt::get(I32Ty, 0);
    auto* pair = procPairTy();
    B.CreateStore(fn, B.CreateGEP(pair, cell,
        {zero, llvm::ConstantInt::get(I32Ty, 0)}, "closure.fn"));
    B.CreateStore(frame, B.CreateGEP(pair, cell,
        {zero, llvm::ConstantInt::get(I32Ty, 1)}, "closure.frame"));
}

std::pair<llvm::Value*, llvm::Value*>
ClosureAndCallABI::loadProcPair(llvm::Value* cell) {
    auto* zero = llvm::ConstantInt::get(I32Ty, 0);
    auto* pair = procPairTy();
    auto* fnP  = B.CreateGEP(pair, cell,
        {zero, llvm::ConstantInt::get(I32Ty, 0)}, "closure.fn.ptr");
    auto* frP  = B.CreateGEP(pair, cell,
        {zero, llvm::ConstantInt::get(I32Ty, 1)}, "closure.frame.ptr");
    return {B.CreateLoad(PtrTy, fnP, "closure.fn"),
            B.CreateLoad(PtrTy, frP, "closure.frame")};
}

void ClosureAndCallABI::pushConformantArgs(std::vector<llvm::Value*>& args,
                                            const ExprNode& arg, size_t dims) {
    args.push_back(EmitLValue(arg));

    // Each dimension reads its bounds from the actual's element type, walking
    // inward as the loop goes.
    const plang::Type* dimTy = arg.ResolvedType.get();

    // An actual that is itself a conformant parameter carries its bounds in
    // variables rather than in its type -- for EVERY dimension, which is what
    // conformantDims holds a name pair each for.
    //
    // Only the outermost was read, and only through the scalar
    // conformantLoName/conformantHiName pair.  The inner ones fell to the
    // element type, and a ConformantArray type has no static bounds, so they
    // came out 0..0.  §6.6.3.7.2 permits relaying a conformant parameter to
    // another conformant formal, and it is the ordinary way to factor code over
    // one: relayed, a matrix declared 1..2 of 3..7 arrived as 1..2 of 0..0, and
    // the callee indexed the flat block with the wrong row width.
    const VarEntry* ave = nullptr;
    if (auto* id = llvm::dyn_cast<IdentExpr>(&arg))
        if (auto* v = SymTab.findVar(id->Name); v && v->isConformantArray)
            ave = v;

    // EP §6.4.7: an actual whose extent a discriminant fixes has no static
    // bounds either, and its recorded ones are the probe's.  The denoter its
    // bounds are written in is walked alongside the type so each dimension can
    // re-emit them against the discriminants the object carries.
    const TypeNode* pathDecl = nullptr;
    std::optional<SchemaPath> argPath;
    if (arg.ResolvedType && arg.ResolvedType->ExtentVaries) {
        argPath = Schema.schemaPathOf(arg);
        if (argPath) pathDecl = argPath->decl;
    }

    for (size_t di = 0; di < dims; ++di) {
        bool fromConformant = false;
        if (argPath && pathDecl) {
            const TypeNode* d = pathDecl;
            while (auto* pk = llvm::dyn_cast_or_null<PackedTypeNode>(d))
                d = pk->Inner.get();
            if (auto* at = llvm::dyn_cast_or_null<ArrayTypeNode>(d);
                    at && at->Low && at->High) {
                // R3: the bounds a conformant array parameter is told about
                // come from the form, evaluated against the discriminants of
                // the actual being passed -- not re-emitted in the CALLER's
                // scope, where the declaration's names mean whatever the
                // caller happens to have declared.
                auto b = SchemaLayout.boundsOfDenoter(*at, argPath->root.discs);
                auto* lo = b ? b->first  : nullptr;
                auto* hi = b ? b->second : nullptr;
                if (lo && hi) {
                    args.push_back(lo);
                    args.push_back(hi);
                    fromConformant = true;
                }
                pathDecl = at->Element.get();
            } else {
                pathDecl = nullptr;
            }
        }
        if (ave) {
            // By address where we have one.  The names are what the programmer
            // wrote in the parameter list, and any scope opened since can
            // answer them: relaying from inside `with r do`, where r has fields
            // spelled like the bounds, passed the record's fields as the
            // array's bounds and the callee walked a hundred elements off the
            // end of a five-element array.  The subscript side already carries
            // these addresses; the relay side did not.
            llvm::Value* loSlot = nullptr;
            llvm::Value* hiSlot = nullptr;
            if (di < ave->conformantDimPtrs.size()) {
                loSlot = ave->conformantDimPtrs[di].first;
                hiSlot = ave->conformantDimPtrs[di].second;
            }
            std::string loNm, hiNm;
            if (di < ave->conformantDims.size()) {
                loNm = ave->conformantDims[di].first;
                hiNm = ave->conformantDims[di].second;
            } else if (di == 0) {
                loNm = ave->conformantLoName;
                hiNm = ave->conformantHiName;
            }
            const auto boundOf = [&](llvm::Value* slot,
                                     const std::string& nm) -> llvm::Value* {
                if (slot) return B.CreateLoad(I64Ty, slot, "pass.bound");
                if (const auto* bv = SymTab.findVar(nm))
                    return B.CreateLoad(I64Ty, bv->ptr, "pass.bound");
                return llvm::ConstantInt::get(I64Ty, 0);
            };
            if (loSlot || !loNm.empty()) {
                args.push_back(boundOf(loSlot, loNm));
                args.push_back(boundOf(hiSlot, hiNm));
                fromConformant = true;
            }
        }
        // EP §6.4.9: a DISCRIMINATED schema is an ordinary fixed-size type, so
        // `vec(5)` is an array wherever a conformant actual is wanted.  Sema's
        // isConformable was widened to unwrap SchemaInstance and reach the
        // array; this was not, so the test below failed and the fallback
        // pushed literal 0, 0.  The callee then saw an empty array and its
        // loop ran no times -- and v0.1.5 REJECTED the call outright, so this
        // branch turned a compile error into a silent wrong answer.
        const auto unwrapSchema = [](const Type* T) {
            T = schemaUnderlying(T);
            return T;
        };
        if (!fromConformant) {
            int64_t lo = 0, hi = 0;
            const Type* d = unwrapSchema(dimTy);
            if (d && (d->Kind == TypeKind::Array
                      || d->Kind == TypeKind::ConformantArray)
                && d->IndexType) {
                lo = d->IndexType->SubLo;
                hi = d->IndexType->SubHi;
            }
            args.push_back(llvm::ConstantInt::get(I64Ty, lo));
            args.push_back(llvm::ConstantInt::get(I64Ty, hi));
        }
        // The next dimension is reached through the body too, for the same
        // reason: a schema instance has no ElemType of its own.
        if (const Type* d = unwrapSchema(dimTy); d && d->ElemType)
            dimTy = d->ElemType.get();
    }
}

/// How many LLVM arguments the parameters in \p pg take, and of what shape.
///
/// Three of the parameter forms are wider than one value: a conformant array
/// adds two bounds per dimension, a schema adds one discriminant each, and a
/// procedural parameter adds its frame.  A procedural parameter's signature
/// has to expand them the same way emitFunctionDef does, or the call will not
/// match the procedure that eventually receives it.
static size_t conformantDimCount(const TypeNode* t) {
    size_t n = 0;
    while (auto* cn = llvm::dyn_cast_or_null<ConformantArrayTypeNode>(t)) {
        n += cn->Specs.size();
        t  = cn->Element.get();
    }
    return n;
}

llvm::FunctionType* ClosureAndCallABI::procParamFnType(const ProcedureTypeNode& node) {
    std::vector<llvm::Type*> params;
    params.push_back(PtrTy); // the frame, always present — see the thunk below

    for (const auto& pg : node.Params) {
        const size_t cdims = conformantDimCount(pg.Type.get());
        const unsigned discs = schemaParamDiscCount(pg.Type.get());
        // Turbo untyped parameter: pg.Type is deliberately null
        // (ParamGroup::Type's own comment), so resolveProcTypeAlias's own
        // null check covers it here -- and IsVar is always true for one
        // (checked against fpc -Mtp: only the 'var' form is legal), so the
        // *pg.Type dereference in the plain 'else' branch just below is
        // never reached for it either.  A NAMED procedural type nested
        // inside a procedural parameter's OWN parameter list -- Turbo's only
        // legal spelling, same as an outer procedural parameter's own
        // (issue #543) -- has to be walked through NamedTypeNode::Denotes
        // the same way, or the nested slot's own two-pointer shape is
        // missed and this signature no longer matches the one built for the
        // relayed argument (CGSymbolTable.h's resolveProcTypeAlias, shared).
        const bool isProc = resolveProcTypeAlias(pg.Type.get()) != nullptr;

        for (size_t i = 0; i < pg.Names.size(); ++i) {
            if (cdims) {
                params.push_back(PtrTy);            // the array
                for (size_t d = 0; d < cdims; ++d) {
                    params.push_back(I64Ty);        // lo
                    params.push_back(I64Ty);        // hi
                }
            } else if (discs) {
                params.push_back(PtrTy);            // the body
                for (unsigned d = 0; d < discs; ++d)
                    params.push_back(I64Ty);        // discriminant
            } else if (isProc) {
                params.push_back(PtrTy);            // entry point
                params.push_back(PtrTy);            // its own frame
            } else {
                params.push_back(pg.IsVar ? PtrTy : Types.llvmTypeOfNode(*pg.Type));
            }
        }
    }

    llvm::Type* ret = node.ReturnType ? Types.llvmTypeOfNode(*node.ReturnType)
                                      : llvm::Type::getVoidTy(Ctx);
    return llvm::FunctionType::get(ret, params, false);
}

llvm::Function* ClosureAndCallABI::procParamThunk(llvm::Function* target,
                                                    const ProcedureTypeNode& node) {
    auto* fnTy = procParamFnType(node);
    // Keyed by signature as well as target: congruity means every formal one
    // procedure reaches agrees on its shape, but that is a fact about Sema
    // rather than something this cache should depend on.
    if (auto it = procParamThunks_.find({target, fnTy});
        it != procParamThunks_.end())
        return it->second;

    auto* thunk = llvm::Function::Create(fnTy, llvm::Function::InternalLinkage,
                                         target->getName() + ".asparam", &Mod);

    // InsertPointGuard, not a bare saveIP/restoreIP pair: this builds a
    // whole separate function with no Pascal-level source identity of its
    // own, so a plain restoreIP would not restore the caller's own insert
    // point/debug location afterward either.
    //
    // The thunk still gets a real, minimal DISubprogram (DIFlagArtificial --
    // "this frame exists but isn't user code," the
    // standard DWARF way to mark a compiler-synthesized shim) rather than
    // being left with none at all: an unattributed function gets no
    // line-table entries whatsoever, which is not the harmless "no debug
    // info" it looks like -- confirmed with gdb, `step` on a call made
    // through a procedural parameter then runs the ENTIRE call to
    // completion instead of entering anything, silently skipping over both
    // the thunk and the real target. Every instruction below is given
    // line 0 in the thunk's own new scope, not DebugLoc() -- that alone is
    // what puts the thunk's address range into the line table at all, so a
    // debugger's step steps transparently through the thunk into the real
    // target instead of vaulting over the whole call.
    llvm::IRBuilderBase::InsertPointGuard guard(B);
    B.SetInsertPoint(llvm::BasicBlock::Create(Ctx, "entry", thunk));
    if (auto* SP = DbgInfo.emitThunkStart(thunk, DbgInfo.getFile(), thunk->getName().str()))
        B.SetCurrentDebugLocation(llvm::DILocation::get(Ctx, 0, 0, SP));
    else
        B.SetCurrentDebugLocation(llvm::DebugLoc());

    std::vector<llvm::Value*> args;
    auto arg = thunk->arg_begin();
    llvm::Value* frame = &*arg;
    ++arg;
    // Only a target that captures something declared a static-link parameter,
    // so the frame is dropped for one that did not.  It is still in the thunk's
    // own signature: every procedure reaching this formal has to look alike.
    if (IsNestedFunction(target->getName().str())) args.push_back(frame);
    for (; arg != thunk->arg_end(); ++arg) args.push_back(&*arg);

    auto* call = B.CreateCall(target, args);
    if (fnTy->getReturnType()->isVoidTy()) B.CreateRetVoid();
    else                                   B.CreateRet(call);

    procParamThunks_[{target, fnTy}] = thunk;
    return thunk;
}

void ClosureAndCallABI::pushProcParamArgs(std::vector<llvm::Value*>& args,
                                           const ExprNode& arg,
                                           const ProcedureTypeNode& node) {
    auto* id = llvm::dyn_cast<IdentExpr>(&arg);
    if (!id) codegenICE("procedural parameter argument is not a procedure name");

    // Handing on a procedural parameter this activation received: the pair is
    // already uniform, and rebuilding the frame here is exactly what cannot be
    // done, since the names it captured are not in scope.
    if (auto* ve = SymTab.findVar(id->Name); ve && ve->isProcParam) {
        auto [fn, frame] = loadProcPair(ve->ptr);
        args.push_back(fn);
        args.push_back(frame);
        return;
    }

    const std::string mangled = Linkage.findMangledProc(id->Name);
    auto* target = Mod.getFunction(mangled);
    if (!target)
        codegenICE("no function named '" + mangled
                   + "' for procedural parameter argument '" + id->Name + "'");

    llvm::Value* frame = BuildStaticLinkFrame(mangled);
    args.push_back(procParamThunk(target, node));
    args.push_back(frame ? frame : llvm::Constant::getNullValue(PtrTy));
}

namespace {
// Issue #299 Phase 2: everything CGCallMarshal::marshalArgs needs to know
// about one flat AST argument position, computed directly from a
// ProcedureTypeNode's own ParamGroup list instead of looked up from
// Codegen::Impl's paramMeta_ by mangled name -- there is no mangled name
// here, only the procedural-parameter/-variable's own declared signature,
// already in hand.  Same five facts ParamMeta (CodeGenImpl.h) records for a
// real callee, just computed on the spot rather than read back.
struct FlatProcParamMeta {
    const ProcedureTypeNode* procType{nullptr};
    unsigned                 schemaDiscCount{0};
    size_t                   conformantDims{0};
    int64_t                  setBase{0};
    bool                     byRef{false};
};

/// Flattens \p params (a procedural type's own parameter groups) into one
/// FlatProcParamMeta per formal name, in declaration order -- so index i
/// answers exactly the same question paramMeta_[mangledName][i] would for a
/// real callee's i-th AST argument position.
std::vector<FlatProcParamMeta> flattenProcParams(
        const std::vector<ParamGroup>& params,
        std::function<size_t(const TypeNode*)> ConformantDimCount,
        std::function<unsigned(const TypeNode*)> SchemaParamDiscCount) {
    std::vector<FlatProcParamMeta> meta;
    for (const auto& pg : params) {
        // Turbo untyped parameter: pg.Type is deliberately null;
        // resolveProcTypeAlias's own null check covers it (ParamGroup::
        // Type's own comment).  A NAMED procedural type -- Turbo's only
        // legal spelling, issue #543 -- has to be walked through
        // NamedTypeNode::Denotes the same way procParamFnType's identical
        // isProc check just above does now, or a nested procedural slot in
        // this signature is missed here too.
        auto*          inner = resolveProcTypeAlias(pg.Type.get());
        const size_t   cdims = ConformantDimCount(pg.Type.get());
        const unsigned discs = SchemaParamDiscCount(pg.Type.get());
        // Same rule CodeGenProcs.cpp's own plain-parameter arm uses to fill
        // paramMeta_'s setBase for a real callee (0 for anything that is not
        // itself a Set-typed parameter -- alignSetArg's own type guard makes
        // that value inert whenever the actual isn't a set anyway, so this
        // is safe to compute unconditionally rather than only for the
        // plain-value arm below).
        const int64_t sb = (pg.Type && pg.Type->ResolvedType
                             && pg.Type->ResolvedType->Kind == TypeKind::Set)
                                ? setOffsetOf(*pg.Type->ResolvedType) : 0;
        for (size_t k = 0; k < pg.Names.size(); ++k)
            meta.push_back(FlatProcParamMeta{inner, discs, cdims, sb, pg.IsVar});
    }
    return meta;
}
} // namespace

llvm::Value*
ClosureAndCallABI::emitProcParamCall(const VarEntry& ve,
                                      std::span<const std::unique_ptr<ExprNode>> argExprs) {
    if (!ve.procType)
        codegenICE("call through a procedural parameter with no signature");

    auto* fnTy = procParamFnType(*ve.procType);
    auto [fn, frame] = loadProcPair(ve.ptr);

    std::vector<llvm::Value*> args;
    args.push_back(frame);

    // Issue #299 Phase 2: build this call's own transient ParamMeta-shaped
    // vector (flattenProcParams, just above) and feed CGCallMarshal's shared
    // per-argument marshalling core with it -- the same core
    // CGProcCall::emitUserProcCall/CGFuncCall::emitUserFuncCall/
    // emitMethodCallExpr already share (Phase 1) -- instead of maintaining a
    // second, independent copy of the same cdims/discs/inner/IsVar dispatch
    // chain here.  The five lookup callbacks below all close over `meta` and
    // ignore the mangledName argument marshalArgs passes them (there is none
    // to give); ExprIsVarStr/ExprIsShortStr are unused stubs -- this call
    // never invokes CGCallMarshal::spillStructReturnIfNeeded (the return
    // spill just below still uses its own pre-existing check, which is about
    // ve.procType->ReturnType, not an ExprNode's), so nothing ever reads
    // them.
    const std::vector<FlatProcParamMeta> meta = flattenProcParams(
        ve.procType->Params,
        [](const TypeNode* t){ return conformantDimCount(t); },
        [this](const TypeNode* t){ return schemaParamDiscCount(t); });

    CGCallMarshal marshal(B, *this, Schema, Sets, StrCall,
        CreateEntryAlloca,
        [&meta](const std::string&, size_t i) -> const ProcedureTypeNode* {
            return i < meta.size() ? meta[i].procType : nullptr;
        },
        [&meta](const std::string&, size_t i) {
            return i < meta.size() && meta[i].byRef;
        },
        [&meta](const std::string&, size_t i) -> size_t {
            return i < meta.size() ? meta[i].conformantDims : 0;
        },
        [&meta](const std::string&, size_t i) -> std::optional<int64_t> {
            return i < meta.size() ? std::optional<int64_t>(meta[i].setBase)
                                    : std::nullopt;
        },
        [&meta](const std::string&, size_t i) -> unsigned {
            return i < meta.size() ? meta[i].schemaDiscCount : 0;
        },
        [](const ExprNode&){ return false; },
        [](const ExprNode&){ return false; });
    marshal.marshalArgs(/*mangledName=*/"", fnTy, argExprs, args);

    auto* call = B.CreateCall(fnTy, fn, args);
    if (fnTy->getReturnType()->isVoidTy()) return nullptr;
    // A string result comes back as the whole { length, bytes } struct, but
    // every consumer of a string expression expects its address -- the same
    // spill the direct-call path already does (CodeGenExprs.cpp).  Missing
    // here, a functional parameter returning string(N) handed the raw struct
    // to plang_str_assign, which wants a pointer: "Call parameter type does
    // not match function signature!", an LLVM IR verifier abort.
    const Type* retTy = ve.procType->ReturnType
                       ? ve.procType->ReturnType->ResolvedType.get() : nullptr;
    if (varStrTypeOf(retTy) && call->getType()->isStructTy()) {
        auto* tmp = CreateEntryAlloca(call->getType(), "str.ret");
        B.CreateStore(call, tmp);
        return tmp;
    }
    return call;
}

llvm::FunctionType* ClosureAndCallABI::procVarFnType(const ProcedureTypeNode& node) {
    // No leading frame parameter -- the one difference from procParamFnType,
    // whose own comment explains why every OTHER shape (a conformant array's
    // pointer + lo/hi pair per dimension, a schema's body pointer + one i64
    // per discriminant, a nested procedural parameter's own {entry point,
    // frame} pair) is identical: those are properties of an ARGUMENT, and a
    // procedural variable's parameter list takes arguments the same way any
    // other routine's does. -std=turbo has no conformant-array or schema
    // syntax at all (Opts.extendedPascal() gates both, and procedural TYPES
    // are Opts.turbo()-gated), so unlike procParamFnType this never has to
    // build those two shapes; a nested procedural parameter is still
    // possible (ISO §6.6.3.1's own parameter form is not turbo-gated) and is
    // handled the same ptr+ptr way procParamFnType handles it.
    std::vector<llvm::Type*> params;
    for (const auto& pg : node.Params) {
        // Turbo untyped parameter: pg.Type is deliberately null
        // (ParamGroup::Type's own comment); resolveProcTypeAlias's own null
        // check covers it, and (IsVar always true for one) the plain 'else'
        // branch's *pg.Type is never reached for it -- same reasoning as
        // procParamFnType just above, including the NAMED-procedural-type
        // walk (issue #543).
        const bool isProc = resolveProcTypeAlias(pg.Type.get()) != nullptr;
        for (size_t i = 0; i < pg.Names.size(); ++i) {
            if (isProc) {
                params.push_back(PtrTy); // entry point
                params.push_back(PtrTy); // its own frame
            } else {
                params.push_back(pg.IsVar ? PtrTy : Types.llvmTypeOfNode(*pg.Type));
            }
        }
    }
    llvm::Type* ret = node.ReturnType ? Types.llvmTypeOfNode(*node.ReturnType)
                                      : llvm::Type::getVoidTy(Ctx);
    return llvm::FunctionType::get(ret, params, false);
}

llvm::Value*
ClosureAndCallABI::emitProcVarCall(const VarEntry& ve,
                                    std::span<const std::unique_ptr<ExprNode>> argExprs) {
    if (!ve.procType)
        codegenICE("call through a procedural variable with no signature");

    auto* fnTy   = procVarFnType(*ve.procType);
    // ve.ptr is the variable's OWN storage (an ordinary flat-pointer slot,
    // never a {entry point, frame} cell -- see VarEntry::isProcVar's own
    // comment), so the callee address is simply what is stored there.
    auto* callee = B.CreateLoad(PtrTy, ve.ptr, "procvar.fn");
    // A nil procedural variable is exactly as much a bad-pointer call as a
    // nil object pointer's method call is (CGProcCall.cpp/CGFuncCall.cpp's
    // own vmt nil checks) -- calling through it segfaults (or worse, jumps
    // to address 0) with no diagnostic at all otherwise.  Same RTE 216 idiom
    // as every other pointer dereference in this codebase (issue #646).
    RangeGuards.emitNilCheck(callee);

    std::vector<llvm::Value*> args;
    size_t flat = 0;
    for (const auto& pg : ve.procType->Params) {
        // Turbo untyped parameter: pg.Type is deliberately null;
        // resolveProcTypeAlias's own null check covers it (ParamGroup::
        // Type's own comment).  Same NAMED-procedural-type walk as
        // procParamFnType/procVarFnType above (issue #543): a nested
        // parameter here is exactly as likely to be spelled with a named
        // type as an outer one is.
        auto* inner = resolveProcTypeAlias(pg.Type.get());
        for (size_t k = 0; k < pg.Names.size(); ++k, ++flat) {
            if (flat >= argExprs.size()) break;
            const auto& a = *argExprs[flat];
            if (inner) {
                // Relaying a routine name (or an already-received procedural
                // parameter) into a nested procedural-parameter slot of this
                // variable's own signature -- exactly what a direct call's
                // identical arm (CGFuncCall/CGProcCall's ProcParamArg check)
                // does, reused rather than reimplemented.
                pushProcParamArgs(args, a, *inner);
            } else if (pg.IsVar) {
                args.push_back(EmitLValue(a));
            } else {
                std::optional<int64_t> destSetBase;
                // pg.Type is null only for a Turbo untyped parameter, which
                // is always IsVar (checked against fpc -Mtp) and so never
                // reaches this plain-value 'else' branch at all -- guarded
                // anyway rather than relying on that invariant silently.
                if (pg.Type && pg.Type->ResolvedType
                    && pg.Type->ResolvedType->Kind == TypeKind::Set)
                    destSetBase = setOffsetOf(*pg.Type->ResolvedType);
                args.push_back(Sets.alignSetArg(
                    EmitCallArg(a,
                        args.size() < fnTy->getNumParams()
                            ? fnTy->getParamType(args.size())
                            : nullptr,
                        /*byRef=*/false),
                    a, destSetBase));
            }
        }
    }

    auto* call = B.CreateCall(fnTy, callee, args);
    if (fnTy->getReturnType()->isVoidTy()) return nullptr;
    // A string result comes back as the whole { length, bytes } struct; see
    // emitProcParamCall's identical spill for why every consumer of a string
    // expression instead expects its address.
    const Type* retTy = ve.procType->ReturnType
                       ? ve.procType->ReturnType->ResolvedType.get() : nullptr;
    if (varStrTypeOf(retTy) && call->getType()->isStructTy()) {
        auto* tmp = CreateEntryAlloca(call->getType(), "str.ret");
        B.CreateStore(call, tmp);
        return tmp;
    }
    return call;
}

unsigned ClosureAndCallABI::schemaParamDiscCount(const TypeNode* t) const {
    if (!t || !t->ResolvedType || t->ResolvedType->Kind != TypeKind::Schema)
        return 0;
    return static_cast<unsigned>(t->ResolvedType->SchemaDiscs.size());
}

/// The EP string(N) type \p T denotes, or null.
///
/// Looks THROUGH a schema whose body is a string.  EP §6.4.3.3 makes
/// `string` a schema, so `type s(n: integer) = string(n); var v: s(10)`
/// declares a string as surely as `var v: string(10)` does -- but v's type
/// is a SchemaInstance, and asking only about Kind said no.
const Type* ClosureAndCallABI::varStrTypeOf(const Type* T) {
    if (!T) return nullptr;
    if (T->Kind == TypeKind::VarString) return T;
    if ((T->Kind == TypeKind::Schema || T->Kind == TypeKind::SchemaInstance)
            && T->SchemaBody) {
        const Type* U = schemaUnderlying(T->SchemaBody.get());
        if (U->Kind == TypeKind::VarString) return U;
    }
    return nullptr;
}
