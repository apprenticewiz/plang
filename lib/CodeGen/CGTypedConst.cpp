//===- CGTypedConst.cpp - TP-only typed constants (`const X: T = v;`). ===//
//
// See ConstDef::Type's own comment (AstDecl.h) and Symbol::IsTypedConst's
// (SymbolTable.h) for the overall design: a typed constant is not a constant
// at all -- it is a variable with static storage and a one-time initializer,
// which for a LOCAL one (declared inside a procedure) means it keeps its
// value across calls, like a C 'static' local, rather than getting a fresh
// per-activation stack slot the way every other local does.
//
// The genuinely new piece CodeGen needs for this: folding the initializer
// into a real compile-time llvm::Constant, so it can be the GlobalVariable's
// own Initializer rather than runtime store/GEP/memcpy instructions into an
// already-allocated pointer (which is all EP's own structured-value lowering,
// CGStructuredValue.cpp, ever needed -- every EP structured constant/value
// clause it handles is filled in at RUN time, once, into storage that already
// exists).  buildTypedConstInit below is that folding; llvm::ConstantStruct
// and llvm::ConstantArray are used to build the aggregate cases, the same
// llvm::Constant-building idiom StringRuntime.cpp already uses (its
// llvm::ConstantStruct::get for a string literal's {length, bytes} constant,
// its llvm::ConstantDataArray for the bytes themselves) -- narrow there (one
// fixed two-field shape), general here (arbitrary record/array nesting, over
// whatever fields checkTypedConstFoldable/typedConstTypeSupported (Sema.cpp)
// already required to fold).
#include "CodeGenImpl.h"
#include "ConstFold.h"
#include "plang/Basic/StringUtil.h"

using namespace plang;

namespace {
// The denoter written for a named field.  Fixed fields only: Sema's
// typedConstTypeSupported (Sema.cpp) already refuses a typed constant whose
// type is a record with a variant part, so a typed constant's own record
// initializer never needs to look through one.  Mirrors
// CGStructuredValue::fieldDenoter (private to that class, and does look
// through a variant part, which this deliberately does not need to).
const TypeNode* typedConstFieldDenoter(const RecordTypeNode& rtn, const std::string& name) {
    for (const auto& fd : rtn.Fields)
        for (const auto& n : fd.Names)
            if (eqCI(n, name)) return fd.Type.get();
    return nullptr;
}
} // namespace

llvm::Constant* Codegen::Impl::buildTypedConstInit(const ExprNode& value,
                                                    const TypeNode* denoter,
                                                    llvm::Type* ty) {
    if (auto* sv = llvm::dyn_cast<StructuredValueExpr>(&value)) {
        // See CGStructuredValue::emitStructuredValue's own comment on
        // `denoter`/`shape` for why a foreign denoter has to be followed
        // through initialStateShapeOf (NamedTypeNode::Denotes) rather than
        // read as written: it may name a type in a DIFFERENT scope than the
        // one currently being lowered.
        const TypeNode* shape = initialStateShapeOf(denoter);

        if (sv->ResolvedType && sv->ResolvedType->Kind == TypeKind::Array) {
            auto* atn = llvm::dyn_cast_or_null<ArrayTypeNode>(shape);
            if (!atn)
                codegenICE("typed constant array initializer has no array "
                           "declaration to take its element type from");
            auto* arrTy = llvm::dyn_cast_or_null<llvm::ArrayType>(ty);
            if (!arrTy)
                codegenICE("typed constant array initializer's storage is "
                           "not an LLVM array type");
            auto* elemTy = arrTy->getElementType();
            std::vector<llvm::Constant*> elems(
                arrTy->getNumElements(), llvm::Constant::getNullValue(elemTy));
            // Turbo's own array literal is purely positional -- see
            // parseTurboConstValue's and checkTypedConstFoldable's own
            // comments -- so an arm's position IN THE LITERAL is its
            // position in the array; there is no EP-style index label to
            // read instead.  checkTypedConstFoldable already checked the
            // count matches when it can; a mismatch here (only reachable
            // when it could not, e.g. a run-time-varying bound Turbo never
            // actually has) is clamped rather than trapped.
            for (size_t i = 0; i < sv->Arms.size() && i < elems.size(); ++i) {
                if (!sv->Arms[i].Value) continue;
                elems[i] = buildTypedConstInit(*sv->Arms[i].Value,
                                               atn->Element.get(), elemTy);
            }
            return llvm::ConstantArray::get(arrTy, elems);
        }

        if (sv->ResolvedType && sv->ResolvedType->Kind == TypeKind::Record) {
            auto* rtn = llvm::dyn_cast_or_null<RecordTypeNode>(shape);
            if (!rtn)
                codegenICE("typed constant record initializer has no record "
                           "declaration to take its fields from");
            // The layout, rather than a map built here from the fixed fields
            // -- see CGStructuredValue::emitStructuredValue's own comment
            // (#197) on why THIS constructor's own ResolvedType, not a
            // shared declaration node, is what layoutOf wants.
            const auto& L  = layoutOf(*rtn, sv->ResolvedType.get());
            auto*       st = L.Ty;
            std::vector<llvm::Constant*> elems(st->getNumElements());
            for (unsigned i = 0; i < st->getNumElements(); ++i)
                elems[i] = llvm::Constant::getNullValue(st->getElementType(i));
            for (const auto& arm : sv->Arms) {
                if (arm.Labels.empty() || !arm.Value) continue;
                auto* id = llvm::dyn_cast<IdentExpr>(arm.Labels[0].get());
                if (!id) continue;
                auto fit = L.Fields.find(toLower(id->Name));
                if (fit == L.Fields.end()) continue;
                const auto& P = fit->second;
                // typedConstTypeSupported already refused any record with a
                // variant part, so P.InVariant is never true here; skipped
                // rather than asserted, the same defensive shape
                // emitStructuredValue's own record arm uses.
                if (P.Index >= st->getNumElements() || P.InVariant) continue;
                elems[P.Index] = buildTypedConstInit(
                    *arm.Value, typedConstFieldDenoter(*rtn, id->Name), P.Ty);
            }
            return llvm::ConstantStruct::get(st, elems);
        }

        // typedConstTypeSupported (Sema.cpp) is meant to have already
        // refused every OTHER aggregate kind (Set, and everything else
        // checkStructuredValue accepts) as a typed constant's type, so a
        // well-typed program never reaches here.
        codegenICE("typed constant initializer is a structured value of a "
                   "type this first implementation does not fold to a "
                   "compile-time constant");
    }

    // Scalar leaf.  Sema's checkTypedConstFoldable (SemaExpr.cpp) already
    // required this to fold via constBound/constRealBound, and -- exactly
    // the same fold, recorded the same way -- populated ConstVal/
    // ConstRealVal; see constantValueOf's identical preference for Sema's
    // own fold over re-deriving one here (R2, that function's own comment).
    if (value.ConstVal) {
        auto* ity = llvm::dyn_cast<llvm::IntegerType>(ty);
        if (!ity)
            codegenICE("typed constant scalar initializer folded to an "
                       "integer but its declared storage is not one");
        return llvm::ConstantInt::get(ity, static_cast<uint64_t>(*value.ConstVal),
                                      /*isSigned=*/true);
    }
    if (value.ConstRealVal)
        return llvm::ConstantFP::get(ty, *value.ConstRealVal);

    codegenICE("typed constant initializer did not fold, though Sema's "
               "checkTypedConstFoldable was meant to have already refused "
               "one that does not");
}

void Codegen::Impl::emitGlobalTypedConst(const ConstDef& cd) {
    llvm::Type*     ty   = llvmTypeOf(cd.Type.get(), nullptr);
    llvm::Constant* init = buildTypedConstInit(*cd.Value, cd.Type.get(), ty);
    const std::string gname = globalPrefix + cd.Name;
    auto* gv = mod->getGlobalVariable(gname);
    if (!gv)
        gv = new llvm::GlobalVariable(*mod, ty, /*isConst=*/false,
                                      llvm::GlobalValue::ExternalLinkage,
                                      init, gname);
    defVar(cd.Name, gv, ty, cd.Type.get());
}

void Codegen::Impl::emitLocalStaticConst(const ConstDef& cd) {
    llvm::Type*     ty   = llvmTypeOf(cd.Type.get(), nullptr);
    llvm::Constant* init = buildTypedConstInit(*cd.Value, cd.Type.get(), ty);
    // Mangled with the enclosing procedure's scope, the same way a nested
    // procedure's own name is (namePrefix already carries every enclosing
    // level, each joined with PlangScopeSep) -- so two procedures each
    // declaring their own local typed constant called the same thing get two
    // distinct symbols.  namePrefix always starts with PlangProcPrefix
    // ("pas_..."); stripping that root and rebuilding under
    // PlangGlobalPrefix ("pasg_...") instead is CGLinkage's own convention
    // for turning a procedure-scoped name into the storage-prefix family
    // (mangledGlobal, CGLinkage.cpp) -- reused here directly rather than
    // through CGLinkage since this name is not an import/qualified lookup,
    // just this activation's own current scope.
    const std::size_t root = std::string_view(PlangProcPrefix).size();
    const std::string  gname = PlangGlobalPrefix + namePrefix.substr(root) + cd.Name;
    auto* gv = mod->getGlobalVariable(gname);
    if (!gv)
        gv = new llvm::GlobalVariable(*mod, ty, /*isConst=*/false,
                                      llvm::GlobalValue::InternalLinkage,
                                      init, gname);
    defVar(cd.Name, gv, ty, cd.Type.get());
}
