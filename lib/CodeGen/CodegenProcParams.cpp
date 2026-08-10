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

    // Each dimension after the first reads its bounds from the actual's
    // element type, walking inward as the loop goes.
    const plang::Type* dimTy = arg.ResolvedType.get();
    for (size_t di = 0; di < dims; ++di) {
        // An actual that is itself a conformant parameter carries its bounds
        // in variables rather than in its type, and only for its outermost
        // dimension — inner ones came from the element type either way.
        bool fromConformant = false;
        if (di == 0) {
            if (auto* id = llvm::dyn_cast<IdentExpr>(&arg)) {
                auto* ave = findVar(id->Name);
                if (ave && ave->isConformantArray) {
                    auto* loVe = findVar(ave->conformantLoName);
                    auto* hiVe = findVar(ave->conformantHiName);
                    args.push_back(loVe
                        ? (llvm::Value*)builder.CreateLoad(i64Ty, loVe->ptr, "pass.lo")
                        : (llvm::Value*)llvm::ConstantInt::get(i64Ty, 0));
                    args.push_back(hiVe
                        ? (llvm::Value*)builder.CreateLoad(i64Ty, hiVe->ptr, "pass.hi")
                        : (llvm::Value*)llvm::ConstantInt::get(i64Ty, 0));
                    fromConformant = true;
                }
            }
        }
        if (!fromConformant) {
            int64_t lo = 0, hi = 0;
            if (dimTy && (dimTy->Kind == TypeKind::Array
                          || dimTy->Kind == TypeKind::ConformantArray)
                && dimTy->IndexType) {
                lo = dimTy->IndexType->SubLo;
                hi = dimTy->IndexType->SubHi;
            }
            args.push_back(llvm::ConstantInt::get(i64Ty, lo));
            args.push_back(llvm::ConstantInt::get(i64Ty, hi));
        }
        if (dimTy && dimTy->ElemType) dimTy = dimTy->ElemType.get();
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

    auto savedIP = builder.saveIP();
    builder.SetInsertPoint(llvm::BasicBlock::Create(ctx, "entry", thunk));

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

    builder.restoreIP(savedIP);
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

    for (size_t fi = 0; fi < varNames.size(); ++fi) {
        auto* ve = findVar(varNames[fi]);
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
    return fnTy->getReturnType()->isVoidTy() ? nullptr : call;
}
