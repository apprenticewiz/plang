// CodegenProcParams.cpp — ISO §6.6.3.1 procedural and functional parameters.
//
// A procedure passed as a parameter travels as two pointers: where to jump,
// and the frame its body reads outer variables through.  The frame cannot be
// left behind, because this compiler builds a callee's static-link frame at
// the call site out of the caller's own visible names — and the place that
// finally calls a procedural parameter has no idea what those names were.
// Whoever passes the procedure is the last one who does, so the frame is built
// there and carried along.
//
// The two pointers are one pair regardless of what was passed, which matters
// because two procedures reaching the same formal parameter need not agree on
// whether they capture anything.  A top-level procedure takes no frame and a
// nested one does, so neither can be called directly through a shared
// signature; each is wrapped in a thunk that accepts a frame and passes it on
// only if the target wants one.

#include "CodegenImpl.h"

#include <optional>

using namespace plang;

void Codegen::Impl::storeProcPair(llvm::Value* cell, llvm::Value* fn,
                                  llvm::Value* frame) {
    auto* zero = llvm::ConstantInt::get(i32Ty, 0);
    auto* pair = procPairTy();
    builder.CreateStore(fn, builder.CreateGEP(pair, cell,
        {zero, llvm::ConstantInt::get(i32Ty, 0)}, "closure.fn"));
    builder.CreateStore(frame, builder.CreateGEP(pair, cell,
        {zero, llvm::ConstantInt::get(i32Ty, 1)}, "closure.frame"));
}

std::pair<llvm::Value*, llvm::Value*>
Codegen::Impl::loadProcPair(llvm::Value* cell) {
    auto* zero = llvm::ConstantInt::get(i32Ty, 0);
    auto* pair = procPairTy();
    auto* fnP  = builder.CreateGEP(pair, cell,
        {zero, llvm::ConstantInt::get(i32Ty, 0)}, "closure.fn.ptr");
    auto* frP  = builder.CreateGEP(pair, cell,
        {zero, llvm::ConstantInt::get(i32Ty, 1)}, "closure.frame.ptr");
    return {builder.CreateLoad(ptrTy, fnP, "closure.fn"),
            builder.CreateLoad(ptrTy, frP, "closure.frame")};
}

void Codegen::Impl::pushConformantArgs(std::vector<llvm::Value*>& args,
                                       const ExprNode& arg, size_t dims) {
    args.push_back(emitLValue(arg));

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
        if (auto* v = findVar(id->Name); v && v->isConformantArray)
            ave = v;

    // EP §6.4.7: an actual whose extent a discriminant fixes has no static
    // bounds either, and its recorded ones are the probe's.  The denoter its
    // bounds are written in is walked alongside the type so each dimension can
    // re-emit them against the discriminants the object carries.
    const TypeNode* pathDecl = nullptr;
    std::optional<SchemaPath> argPath;
    if (arg.ResolvedType && arg.ResolvedType->ExtentVaries) {
        argPath = schemaPathOf(arg);
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
                auto b = boundsOfDenoter(*at, argPath->root);
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
                if (slot) return builder.CreateLoad(i64Ty, slot, "pass.bound");
                if (const auto* bv = findVar(nm))
                    return builder.CreateLoad(i64Ty, bv->ptr, "pass.bound");
                return llvm::ConstantInt::get(i64Ty, 0);
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
            args.push_back(llvm::ConstantInt::get(i64Ty, lo));
            args.push_back(llvm::ConstantInt::get(i64Ty, hi));
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

llvm::FunctionType* Codegen::Impl::procParamFnType(const ProcedureTypeNode& node) {
    std::vector<llvm::Type*> params;
    params.push_back(ptrTy); // the frame, always present — see the thunk below

    for (const auto& pg : node.Params) {
        const size_t cdims = conformantDimCount(pg.Type.get());
        const unsigned discs = schemaParamDiscCount(pg.Type.get());
        const bool isProc =
            llvm::dyn_cast<ProcedureTypeNode>(pg.Type.get()) != nullptr;

        for (size_t i = 0; i < pg.Names.size(); ++i) {
            if (cdims) {
                params.push_back(ptrTy);            // the array
                for (size_t d = 0; d < cdims; ++d) {
                    params.push_back(i64Ty);        // lo
                    params.push_back(i64Ty);        // hi
                }
            } else if (discs) {
                params.push_back(ptrTy);            // the body
                for (unsigned d = 0; d < discs; ++d)
                    params.push_back(i64Ty);        // discriminant
            } else if (isProc) {
                params.push_back(ptrTy);            // entry point
                params.push_back(ptrTy);            // its own frame
            } else {
                params.push_back(pg.IsVar ? ptrTy : llvmTypeOfNode(*pg.Type));
            }
        }
    }

    llvm::Type* ret = node.ReturnType ? llvmTypeOfNode(*node.ReturnType)
                                      : llvm::Type::getVoidTy(ctx);
    return llvm::FunctionType::get(ret, params, false);
}

llvm::Function* Codegen::Impl::procParamThunk(llvm::Function* target,
                                              const ProcedureTypeNode& node) {
    auto* fnTy = procParamFnType(node);
    // Keyed by signature as well as target: congruity means every formal one
    // procedure reaches agrees on its shape, but that is a fact about Sema
    // rather than something this cache should depend on.
    if (auto it = procParamThunks_.find({target, fnTy});
        it != procParamThunks_.end())
        return it->second;

    auto* thunk = llvm::Function::Create(fnTy, llvm::Function::InternalLinkage,
                                         target->getName() + ".asparam", mod.get());

    // InsertPointGuard, not a bare saveIP/restoreIP pair: this builds a
    // whole separate function with no Pascal-level source identity of its
    // own, so its instructions get no debug location at all (valid --
    // AddMetadataToInst attaches nothing for an empty DebugLoc) rather
    // than incorrectly inheriting whatever the caller's own current
    // location happened to be, and a plain restoreIP would not restore
    // the caller's location afterward either.
    llvm::IRBuilderBase::InsertPointGuard guard(builder);
    builder.SetInsertPoint(llvm::BasicBlock::Create(ctx, "entry", thunk));
    builder.SetCurrentDebugLocation(llvm::DebugLoc());

    std::vector<llvm::Value*> args;
    auto arg = thunk->arg_begin();
    llvm::Value* frame = &*arg;
    ++arg;
    // Only a target that captures something declared a static-link parameter,
    // so the frame is dropped for one that did not.  It is still in the thunk's
    // own signature: every procedure reaching this formal has to look alike.
    if (nestedFunctions_.count(target->getName().str())) args.push_back(frame);
    for (; arg != thunk->arg_end(); ++arg) args.push_back(&*arg);

    auto* call = builder.CreateCall(target, args);
    if (fnTy->getReturnType()->isVoidTy()) builder.CreateRetVoid();
    else                                   builder.CreateRet(call);

    procParamThunks_[{target, fnTy}] = thunk;
    return thunk;
}

llvm::Value* Codegen::Impl::buildStaticLinkFrame(const std::string& mangledName) {
    if (!nestedFunctions_.count(mangledName)) return nullptr;

    const auto& varNames = funcOuterVarNames_.at(mangledName);
    std::vector<llvm::Type*> ptrFields(varNames.size(), ptrTy);
    auto* frameTy     = llvm::StructType::get(ctx, ptrFields);
    auto* frameAlloca = createEntryAlloca(frameTy, "frame");
    auto* zero        = llvm::ConstantInt::get(i32Ty, 0);

    // Each slot is for a particular VARIABLE, and a name does not name one:
    // two nesting levels may declare the same one.  The depth recorded beside
    // the name does, being fixed by the nesting and the same number in every
    // activation.
    //
    // Resolving by name alone was wrong twice over.  With `b` and `c` both
    // nested in `a` and `c` declaring its own `n`, `c` calling `b` handed b the
    // address of c's n.  And where the callee's frame had two slots spelled
    // alike, both were filled with the innermost binding, so the outer variable
    // never travelled at all: three levels deep, a procedure reading its
    // grandparent's `n` saw its parent's.
    //
    // A depth this activation has no binding for is one declared HERE, and
    // findVar answers it.
    const auto DIt = funcOuterVarDepths_.find(mangledName);
    const std::vector<size_t>* varDepths =
        DIt != funcOuterVarDepths_.end() ? &DIt->second : nullptr;

    for (size_t fi = 0; fi < varNames.size(); ++fi) {
        const VarEntry* ve = nullptr;
        if (varDepths && fi < varDepths->size()) {
            const auto it = outerVarBindings.find(
                {(*varDepths)[fi], toLower(varNames[fi])});
            if (it != outerVarBindings.end()) ve = &it->second;
        }
        // A slot this activation has no capture for is one declared HERE --
        // so the search starts at this function's own scope, not at whatever
        // scope the call happens to sit in.  Starting at the innermost let a
        // with-statement answer: calling a nested procedure from inside
        // `with r do` handed it the address of r's field of that name, and the
        // increments meant for the enclosing variable landed in the record.
        if (!ve) ve = findVarInFunctionScope(varNames[fi]);
        if (!ve)
            codegenICE("captured variable '" + varNames[fi]
                       + "' is not visible at the call site of '"
                       + mangledName + "'");
        // A frame slot is an address; anything reaching here without one would
        // be stored as a null the callee then reads through.
        if (!ve->ptr)
            codegenICE("captured variable '" + varNames[fi]
                       + "' has no storage to link to '" + mangledName + "'");
        auto* slot = builder.CreateGEP(
            frameTy, frameAlloca,
            {zero, llvm::ConstantInt::get(i32Ty, static_cast<unsigned>(fi))},
            "frame." + varNames[fi]);
        builder.CreateStore(ve->ptr, slot);
    }
    return frameAlloca;
}

void Codegen::Impl::pushProcParamArgs(std::vector<llvm::Value*>& args,
                                      const ExprNode& arg,
                                      const ProcedureTypeNode& node) {
    auto* id = llvm::dyn_cast<IdentExpr>(&arg);
    if (!id) codegenICE("procedural parameter argument is not a procedure name");

    // Handing on a procedural parameter this activation received: the pair is
    // already uniform, and rebuilding the frame here is exactly what cannot be
    // done, since the names it captured are not in scope.
    if (auto* ve = findVar(id->Name); ve && ve->isProcParam) {
        auto [fn, frame] = loadProcPair(ve->ptr);
        args.push_back(fn);
        args.push_back(frame);
        return;
    }

    const std::string mangled = findMangledProc(id->Name);
    auto* target = mod->getFunction(mangled);
    if (!target)
        codegenICE("no function named '" + mangled
                   + "' for procedural parameter argument '" + id->Name + "'");

    llvm::Value* frame = buildStaticLinkFrame(mangled);
    args.push_back(procParamThunk(target, node));
    args.push_back(frame ? frame : llvm::Constant::getNullValue(ptrTy));
}

llvm::Value*
Codegen::Impl::emitProcParamCall(const VarEntry& ve,
                                 std::span<const std::unique_ptr<ExprNode>> argExprs) {
    if (!ve.procType)
        codegenICE("call through a procedural parameter with no signature");

    auto* fnTy = procParamFnType(*ve.procType);
    auto [fn, frame] = loadProcPair(ve.ptr);

    std::vector<llvm::Value*> args;
    args.push_back(frame);

    size_t flat = 0;
    for (const auto& pg : ve.procType->Params) {
        auto*          inner = llvm::dyn_cast<ProcedureTypeNode>(pg.Type.get());
        const size_t   cdims = conformantDimCount(pg.Type.get());
        const unsigned discs = schemaParamDiscCount(pg.Type.get());

        for (size_t k = 0; k < pg.Names.size(); ++k, ++flat) {
            if (flat >= argExprs.size()) break;
            const auto& a = *argExprs[flat];
            if (cdims)          pushConformantArgs(args, a, cdims);
            else if (discs)     pushSchemaArgs(args, a, discs);
            else if (inner)     pushProcParamArgs(args, a, *inner);
            else if (pg.IsVar)  args.push_back(emitLValue(a));
            else                args.push_back(emitCallArg(a,
                                    args.size() < fnTy->getNumParams()
                                        ? fnTy->getParamType(args.size())
                                        : nullptr,
                                    /*byRef=*/false));
        }
    }

    auto* call = builder.CreateCall(fnTy, fn, args);
    if (fnTy->getReturnType()->isVoidTy()) return nullptr;
    // A string result comes back as the whole { length, bytes } struct, but
    // every consumer of a string expression expects its address -- the same
    // spill the direct-call path already does (CodegenExprs.cpp).  Missing
    // here, a functional parameter returning string(N) handed the raw struct
    // to plang_str_assign, which wants a pointer: "Call parameter type does
    // not match function signature!", an LLVM IR verifier abort.
    const Type* retTy = ve.procType->ReturnType
                       ? ve.procType->ReturnType->ResolvedType.get() : nullptr;
    if (varStrTypeOf(retTy) && call->getType()->isStructTy()) {
        auto* tmp = createEntryAlloca(call->getType(), "str.ret");
        builder.CreateStore(call, tmp);
        return tmp;
    }
    return call;
}
