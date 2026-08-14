#include "CodegenImpl.h"

#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"

using namespace plang;

// See NumTypeKinds in AstBase.h.
static_assert(NumTypeKinds == 14,
              "a new type denoter needs a case in llvmTypeOfNode");

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

    i1Ty  = llvm::Type::getInt1Ty(ctx);
    i8Ty  = llvm::Type::getInt8Ty(ctx);
    i32Ty = llvm::Type::getInt32Ty(ctx);
    i64Ty = llvm::Type::getInt64Ty(ctx);
    dblTy = llvm::Type::getDoubleTy(ctx);
    ptrTy = llvm::PointerType::get(ctx, 0); // opaque ptr (LLVM 15+)

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

    strGVs.clear();
    structTypes.clear();
    externFuncs.clear();
    typeAliases.clear();
    strStructTypes.clear();
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
                            const TypeNode* typeNode) {
    if (scopes.empty()) return;
    const std::string Key = toLower(name);
    // A variable of this name hides a constant of it for as long as the scope
    // lasts.  See shadowedConsts: the constant table is flat, so without this
    // the constant answered every read while the writes went to the variable.
    if (const auto It = consts.find(Key); It != consts.end()) {
        if (shadowedConsts.size() == scopes.size()
                && !shadowedConsts.back().count(Key))
            shadowedConsts.back()[Key] = It->second;
        consts.erase(It);
    }
    scopes.back()[Key] = VarEntry{ ptr, type, typeNode };
}

const Codegen::Impl::VarEntry* Codegen::Impl::findVar(const std::string& name) const {
    std::string key = toLower(name);
    // Down to varLookupFloor_ and no further; see DeclarationScopeOnly.
    for (size_t i = scopes.size(); i-- > varLookupFloor_;) {
        auto f = scopes[i].find(key);
        if (f != scopes[i].end()) return &f->second;
    }
    return nullptr;
}

std::optional<std::pair<int64_t, int64_t>>
Codegen::Impl::arrayIndexRange(const ArrayTypeNode& n) const {
    // R1.  Sema folded these bounds in the scope they were WRITTEN in; folding
    // them here folds them where the denoter is being LOWERED, against a
    // constant table that holds whatever is innermost at that moment.  A record
    // whose field is `array[1..n]` and whose layout is first computed inside a
    // procedure declaring its own `n` came out sized for the stranger's n --
    // caught, on a legal ISO 7185 program, as "takes 16 bytes as it is written
    // and 80 bytes as Sema resolved it".
    //
    // Inside a schema instantiation the syntax is still the only answer: a
    // bound over a discriminant is a constant per instance and not in the
    // syntax, and Sema's annotation there is the probe's.  Same exemption the
    // size-agreement guard makes.
    if (schemaCtx.empty() && n.ResolvedType
            && n.ResolvedType->Kind == TypeKind::Array && n.ResolvedType->IndexType)
        if (auto R = ordinalRange(*n.ResolvedType->IndexType)) return R;
    if (n.Low && n.High) {
        const auto lo = tryEvalConstInt(*n.Low,  &consts);
        const auto hi = tryEvalConstInt(*n.High, &consts);
        if (lo && hi) return std::pair{*lo, *hi};
        // A bound written in terms of a schema discriminant — `array [0..n]`
        // in the body of `poly(n: integer)` — is a constant in each instance
        // but not in the syntax, which has no instance in hand.  Sema resolved
        // the body once per instantiation and knows what n was each time.
    }
    // Written as an ordinal type, so the range belongs to that type and only
    // Sema has resolved it.
    if (n.ResolvedType && n.ResolvedType->Kind == TypeKind::Array
            && n.ResolvedType->IndexType)
        return ordinalRange(*n.ResolvedType->IndexType);
    return std::nullopt;
}

const Codegen::Impl::VarEntry*
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
// Type resolution
// ====================================================================

llvm::StructType* Codegen::Impl::strStructType(int64_t cap) {
    auto it = strStructTypes.find(cap);
    if (it != strStructTypes.end()) return it->second;
    auto* arr = llvm::ArrayType::get(i8Ty, static_cast<uint64_t>(cap));
    auto* st  = llvm::StructType::get(ctx, {i64Ty, arr});
    strStructTypes[cap] = st;
    return st;
}

llvm::Type* Codegen::Impl::llvmTypeOfName(const std::string& name) {
    std::string lo = toLower(name);
    // As wide as the dialect makes it; see Type::Width.  Answering i64
    // regardless is what made the two readings of `integer` disagree under
    // -std=turbo, where Sema had already resolved it to sixteen bits.
    if (lo == "integer")  return llvm::Type::getIntNTy(ctx, langOpts.defaultIntWidth());
    if (lo == "real")     return dblTy;
    if (lo == "complex")  return complexTy(); // EP §6.4.2.2: { double, double }
    if (lo == "boolean")  return i1Ty;
    if (lo == "char")     return i8Ty;
    if (lo == "string")   return ptrTy;
    if (lo == "text")      return fileStructType(); // PascalFile { FILE*, int } = 16 bytes
    if (lo == "timestamp")   return timestampStructType();  // EP §6.4.3.4
    if (lo == "bindingtype") return bindingStructType();    // EP §6.4.3.4
    // User-defined type alias — resolve through the typedef table.
    auto it = typeAliases.find(lo);
    if (it != typeAliases.end()) return llvmTypeOfNode(*it->second);
    // Unknown here is not an error yet: the caller retries via the Sema type.
    return nullptr;
}

llvm::Type* Codegen::Impl::variantBlobType(uint64_t size, uint64_t align) {
    llvm::Type* cell = i8Ty;
    uint64_t    unit = 1;
    if      (align >= 8) { cell = i64Ty;                        unit = 8; }
    else if (align >= 4) { cell = i32Ty;                        unit = 4; }
    else if (align >= 2) { cell = llvm::Type::getInt16Ty(ctx);  unit = 2; }
    return llvm::ArrayType::get(cell, (size + unit - 1) / unit);
}

uint64_t Codegen::Impl::layoutVariantCase(const VariantCase& vc, RecordLayout& L,
                                           bool packed, unsigned blobIdx,
                                           uint64_t base, uint64_t& align) {
    const auto& dl = mod->getDataLayout();
    uint64_t at = base;

    auto place = [&](const std::string& name, llvm::Type* ft) {
        // ISO §6.4.3.1: a packed component is stored as economically as the
        // implementation can manage, which here means no padding in front of it.
        const uint64_t fa = packed ? 1 : dl.getABITypeAlign(ft).value();
        align = std::max(align, fa);
        at = (at + fa - 1) / fa * fa;
        // A name used in two alternatives keeps its first placement; Sema has
        // already reported it, and inventing a second one would only confuse
        // the diagnostics that follow.
        L.Fields.emplace(toLower(name), FieldPlace{blobIdx, ft, true, at});
        at += dl.getTypeAllocSize(ft);
    };

    for (const auto& fd : vc.Fields) {
        llvm::Type* ft = llvmTypeOf(fd.Type.get(), nullptr);
        for (const auto& nm : fd.Names) place(nm, ft);
    }

    // A nested variant follows this alternative's own fields, and its
    // alternatives in turn share the storage after them.
    if (vc.NestedVariant) {
        const auto& nv = *vc.NestedVariant;
        if (!nv.TagField.empty() && nv.TagType)
            place(nv.TagField, llvmTypeOfNode(*nv.TagType));
        uint64_t end = at;
        for (const auto& inner : nv.Cases)
            end = std::max(end, layoutVariantCase(inner, L, packed, blobIdx,
                                                  at, align));
        at = end;
    }
    return at;
}

void Codegen::Impl::layoutVariantPart(const VariantPart& vp, RecordLayout& L,
                                       bool packed,
                                       std::vector<llvm::Type*>& elems) {
    if (!vp.TagField.empty() && vp.TagType) {
        llvm::Type* tt = llvmTypeOfNode(*vp.TagType);
        L.Fields[toLower(vp.TagField)] =
            FieldPlace{static_cast<unsigned>(elems.size()), tt, false, 0};
        elems.push_back(tt);
    }

    const auto blobIdx = static_cast<unsigned>(elems.size());
    uint64_t size = 0, align = 1;
    for (const auto& vc : vp.Cases)
        size = std::max(size, layoutVariantCase(vc, L, packed, blobIdx, 0, align));
    // Every alternative may be empty — `case b: boolean of true: (); false: ()`
    // is a record with a tag and nothing else — and then there is nothing to
    // reserve and no field that would have referred to it.
    if (size > 0) elems.push_back(variantBlobType(size, packed ? 1 : align));
}

Codegen::Impl::SchemaBindingScope::SchemaBindingScope(Impl& I, const Type& T)
        : I(I), SavedCtx(I.schemaCtx) {
    for (const auto& [name, value] : T.SchemaBindings) {
        const std::string key = toLower(name);
        auto it = I.consts.find(key);
        Saved.emplace_back(key, it != I.consts.end()
                                    ? std::optional{it->second}
                                    : std::nullopt);
        I.consts[key] = llvm::ConstantInt::get(I.i64Ty,
                            static_cast<uint64_t>(value), /*isSigned=*/true);
        I.schemaCtx += "|" + key + "=" + std::to_string(value);
    }
}

Codegen::Impl::SchemaBindingScope::~SchemaBindingScope() {
    for (const auto& [key, prior] : Saved) {
        if (prior) I.consts[key] = *prior;
        else       I.consts.erase(key);
    }
    I.schemaCtx = SavedCtx;
}

const Codegen::Impl::RecordLayout*
Codegen::Impl::layoutOfRecord(const Type& T) {
    if (T.Kind != TypeKind::Record || !T.RecordDecl) return nullptr;
    SchemaBindingScope bind(*this, T);
    return &layoutOf(*T.RecordDecl);
}

const Codegen::Impl::RecordLayout&
Codegen::Impl::layoutOf(const RecordTypeNode& rt) {
    const auto key0 = std::pair{&rt, schemaCtx};
    if (auto it = recordLayouts.find(key0); it != recordLayouts.end())
        return it->second;

    RecordLayout L;
    std::vector<llvm::Type*> elems;
    // ISO §6.4.3.1 leaves what `packed` does to the implementation, and plang
    // used to do nothing with it.  It packs now, in every dialect: Turbo needs
    // it for {$PACKRECORDS 1} and for a record image a real Turbo program can
    // read, and a `packed` that packs nothing is a word the language has that
    // means nothing.
    const bool packed = rt.Packed;
    for (const auto& fd : rt.Fields) {
        llvm::Type* ft = llvmTypeOf(fd.Type.get(), nullptr);
        for (const auto& nm : fd.Names) {
            L.Fields[toLower(nm)] =
                FieldPlace{static_cast<unsigned>(elems.size()), ft, false, 0};
            elems.push_back(ft);
        }
    }
    if (rt.Variant) layoutVariantPart(*rt.Variant, L, packed, elems);

    // Two records laid out the same way share one struct type.  The names are
    // not part of the key: they are what the layout is for, and the struct only
    // has to give a GEP the right shape.
    // Packedness is part of the key.  Two records with the same field types
    // and different packing are different layouts, and sharing one struct
    // between them would give the packed one the padded one's offsets.
    std::string key = packed ? "P:" : "U:";
    for (auto* t : elems) key += std::to_string(std::bit_cast<uintptr_t>(t)) + ",";
    auto it = structTypes.find(key);
    L.Ty = (it != structTypes.end()) ? it->second
                                     : llvm::StructType::get(ctx, elems, packed);
    structTypes[key] = L.Ty;
    return recordLayouts.emplace(key0, std::move(L)).first->second;
}

llvm::StructType* Codegen::Impl::structTypeFor(const RecordTypeNode& rt) {
    return layoutOf(rt).Ty;
}

// Lowers a type denoter.  The syntax is used where it carries information the
// semantic type does not — array bounds, for instance, which codegen can fold
// in cases Sema leaves symbolic.  Anything the syntax cannot answer falls back
// to the type Sema resolved for the node, and a denoter neither can lower is an
// internal error rather than a silent integer.
llvm::Type* Codegen::Impl::llvmTypeOfNode(const TypeNode& node) {
    if (auto* n = llvm::dyn_cast<NamedTypeNode>(&node)) {
        // A bare `string` names a capacity-less type everywhere except in a
        // formal parameter list, where EP §6.7.3.1 gives it one.  The name
        // cannot tell the two apart, so a resolved capacity wins over it.
        if (node.ResolvedType && node.ResolvedType->Kind == TypeKind::VarString)
            return strStructType(node.ResolvedType->StrCapacity);
        // A record's struct is reached through Type::RecordDecl, a pointer to
        // the declaration it came from, while llvmTypeOfName below goes through
        // a table rebuilt per procedure and answered by SPELLING.  A procedure
        // declaring its own type of this name re-aimed an outer variable's type
        // at the inner record -- caught by the size-agreement check as "type
        // 't' takes 1 bytes as it is written and 24 bytes as Sema resolved it",
        // which is an internal error on a program fpc compiles and runs.
        if (node.ResolvedType && node.ResolvedType->Kind == TypeKind::Record
                && node.ResolvedType->RecordDecl)
            return llvmTypeOfSemaType(*node.ResolvedType);
        // R1.  The two cases above are this rule applied one TypeKind at a
        // time, each added when a spelling collision was traced back to here.
        // A NamedTypeNode is nothing BUT a name, so there is no information in
        // it that Sema did not already use: Sema bound the name in the scope it
        // was written in and hung the answer on the node.  llvmTypeOfName below
        // answers out of typeAliases, a flat table rebuilt per procedure and
        // keyed by spelling, which can only agree by coincidence -- and when it
        // disagrees it hands back a type of a different SIZE, which is how an
        // inner procedure's homonym came to size an outer variable.
        //
        // Inside a schema instantiation the annotation is the last instance's
        // and not this one's, so there the syntax is still the only answer;
        // that is the same exemption the size-agreement guard already makes.
        if (schemaCtx.empty() && node.ResolvedType && !node.ResolvedType->isError()
                && canLowerSemaType(*node.ResolvedType))
            return llvmTypeOfSemaType(*node.ResolvedType);
        if (auto* t = llvmTypeOfName(n->Name)) return t;
        return llvmTypeOfNodeViaSema(node, "unknown type name '" + n->Name + "'");
    }

    if (auto* n = llvm::dyn_cast<ArrayTypeNode>(&node)) {
        // A bound this cannot fold used to become zero, and an array whose
        // extent came out zero or negative became a [0 x T] that every index
        // then ran off the end of.  Sema resolved the same type and rejects a
        // bound that is not constant, so defer to it rather than guess.
        auto range = arrayIndexRange(*n);
        const int64_t cnt = range ? range->second - range->first + 1 : 0;
        if (cnt <= 0)
            return llvmTypeOfNodeViaSema(node, "array bounds did not fold");
        return llvm::ArrayType::get(llvmTypeOfNode(*n->Element),
                                    static_cast<uint64_t>(cnt));
    }
    if (auto* n = llvm::dyn_cast<RecordTypeNode>(&node))
        return structTypeFor(*n);

    if (llvm::dyn_cast<PointerTypeNode>(&node))   return ptrTy;
    // A subrange and an enumeration are as wide as Sema resolved them to be.
    // Answering i64 here regardless is what would make the two readings of a
    // narrow type disagree, and the check at the end of llvmTypeOfNodeChecked
    // would then fire on every one of them.
    if (llvm::dyn_cast<SubrangeTypeNode>(&node))  return ordinalTyOf(node);
    if (auto* n = llvm::dyn_cast<StringTypeNode>(&node)) {
        // R2.  The capacity Sema resolved, then the capacity the syntax folds
        // to.  This used to fold first and fall back to 255 -- the very thing
        // tryEvalConstInt's own comment says must never be done, because a
        // fabricated extent is indistinguishable from a real one downstream.
        //
        // 255 survives for exactly one case: a `string(cap)` in a schema body
        // being lowered as the PROBE type.  There the capacity is genuinely
        // unknown until an instance exists, the type built here is nobody's
        // storage, and CodegenSchema lays the real one out at run time.  Every
        // other route now has an answer or is an internal error, rather than a
        // number that describes nothing.
        if (schemaCtx.empty() && node.ResolvedType
                && node.ResolvedType->Kind == TypeKind::VarString)
            return strStructType(node.ResolvedType->StrCapacity);
        if (auto Cap = tryEvalConstInt(*n->Capacity, &consts))
            return strStructType(*Cap);
        if (schemaCtx.empty())
            codegenICE("a string capacity that is neither resolved nor "
                       "constant-foldable");
        return strStructType(PlangMaxStringCapacity);
    }
    if (llvm::dyn_cast<EnumTypeNode>(&node))      return ordinalTyOf(node);
    if (llvm::dyn_cast<SetTypeNode>(&node))       return setTy();
    if (llvm::dyn_cast<FileTypeNode>(&node))      return fileStructType();
    if (auto* n = llvm::dyn_cast<PackedTypeNode>(&node))
        return llvmTypeOfNode(*n->Inner);
    // EP §6.7.3.7: conformant array params are passed as ptr; return ptr as the
    // "value type" so callers that do not special-case it get a valid type.
    if (llvm::dyn_cast<ConformantArrayTypeNode>(&node))
        return ptrTy;
    // EP §6.4.8: schema instantiation — use the resolved body type.
    if (auto* n = llvm::dyn_cast<SchemaTypeNode>(&node)) {
        if (n->ResolvedBody && n->ResolvedBody->Kind == TypeKind::SchemaInstance
                && n->ResolvedBody->SchemaBody)
            return llvmTypeOfSemaType(*n->ResolvedBody->SchemaBody);
        return llvmTypeOfNodeViaSema(node,
            "schema instantiation '" + n->Name + "' was not resolved");
    }
    // EP §6.4.9 `type of x` and anything else without a syntactic lowering.
    return llvmTypeOfNodeViaSema(node, "unhandled type denoter");
}

llvm::Type* Codegen::Impl::llvmTypeOfNodeViaSema(const TypeNode& node,
                                                 const std::string& what) {
    if (node.ResolvedType && !node.ResolvedType->isError())
        return llvmTypeOfSemaType(*node.ResolvedType);
    codegenICE(what + " and Sema left it unresolved");
}

// See NumSemaTypeKinds in Sema/Type.h.  A kind this file has not been taught
// falls into the default of canLowerSemaType, which reports it as not
// lowerable, and a variable of it is then refused for a reason that names
// nothing -- or reaches llvmTypeOfSemaType and is lowered as an i64.
static_assert(NumSemaTypeKinds == 21,
              "a new semantic type kind needs a case in canLowerSemaType and "
              "in llvmTypeOfSemaType");

/// Whether llvmTypeOfSemaType has a lowering for \p T.  An undiscriminated
/// schema has none — its extent is not known until it is passed or allocated —
/// and neither has the error type.
bool Codegen::Impl::canLowerSemaType(const Type& T) {
    switch (T.Kind) {
    case TypeKind::Integer:  case TypeKind::Subrange: case TypeKind::Enum:
    case TypeKind::Set:      case TypeKind::Real:     case TypeKind::Complex:
    case TypeKind::Boolean:  case TypeKind::Char:     case TypeKind::String:
    case TypeKind::Pointer:  case TypeKind::Nil:      case TypeKind::VarString:
    case TypeKind::File:     case TypeKind::Array:    case TypeKind::Record:
    case TypeKind::ConformantArray:
        return true;
    case TypeKind::SchemaInstance:
        return T.SchemaBody && canLowerSemaType(*T.SchemaBody);
    default:
        return false;
    }
}

llvm::Type* Codegen::Impl::llvmTypeOf(const TypeNode* denoter,
                                      const Type* resolved) {
    if (!denoter) {
        if (!resolved || resolved->isError())
            codegenICE("a value was allocated with neither a declaration nor a "
                       "resolved type to say how big it is");
        return llvmTypeOfSemaType(*resolved);
    }

    auto* fromSyntax = llvmTypeOfNode(*denoter);
    if (!resolved) resolved = denoter->ResolvedType.get();

    // Where both can answer, they have to agree.  There are two ways to say
    // what a type is and about thirty places that pick one of them, and a
    // caller that picks differently from the one that laid the storage out
    // gets an offset into a variable of a size nobody agreed on.  Size is the
    // part worth checking: it is the difference between a variable and a
    // variable the next one starts inside, and it is what goes wrong quietly.
    //
    // Inside a schema body the two are meant to differ.  One declaration
    // serves every instantiation and carries the annotation of whichever was
    // resolved last, while the syntax read under the discriminants in force is
    // this instance's own — so there the syntax is the only one asked.
    if (schemaCtx.empty() && resolved && !resolved->isError()
            && canLowerSemaType(*resolved)) {
        auto* fromSema = llvmTypeOfSemaType(*resolved);
        const auto& dl  = mod->getDataLayout();
        if (fromSema != fromSyntax && fromSema->isSized() && fromSyntax->isSized()
                && dl.getTypeAllocSize(fromSema) != dl.getTypeAllocSize(fromSyntax))
            codegenICE("type '" + resolved->Name + "' takes "
                       + llvm::Twine(dl.getTypeAllocSize(fromSyntax).getFixedValue())
                       + " bytes as it is written and "
                       + llvm::Twine(dl.getTypeAllocSize(fromSema).getFixedValue())
                       + " bytes as Sema resolved it");
    }
    return fromSyntax;
}

// ====================================================================
// Semantic-type → LLVM type conversion (for schema body types)
// ====================================================================

/// The integer type an ordinal denoter lowers to, taken from what Sema
/// resolved it to.  Falls back to the dialect's own integer width, for a
/// denoter inside a schema body that no instantiation has annotated yet.
llvm::Type* Codegen::Impl::ordinalTyOf(const TypeNode& node) {
    const Type* T = node.ResolvedType.get();
    return llvm::Type::getIntNTy(ctx, T ? T->Width : langOpts.defaultIntWidth());
}

/// Asserts that Sema's idea of how big \p T is matches the type just built for
/// it.
///
/// Sema works the size out without a DataLayout, because a Turbo `const N =
/// SizeOf(Integer)` has to fold before there is one.  Two ways of answering
/// one question is the arrangement that goes wrong quietly: a SizeOf that
/// disagrees with the layout sizes a GetMem or a BlockRead buffer wrong, and
/// nothing reports it until the memory past the end of it is read back.
///
/// So they are checked against each other on every type codegen lowers, which
/// is the widest net available -- every type in every compiled program, rather
/// than the ones a test remembered to name.
void Codegen::Impl::checkSizeAgreement(const Type& T, llvm::Type* Built) {
    // Inside a schema body the two are meant to differ: the denoters carry
    // the last instantiation's annotation while the storage being laid out is
    // this one's.  The check above llvmTypeOfNodeChecked skips it for the same
    // reason.
    // SchemaBindings marks a record that *is* an instantiation: it resolves
    // to a plain Record, but its field denoters still carry whichever
    // instantiation was resolved last.
    if (!schemaCtx.empty() || !T.SchemaBindings.empty()) return;
    const auto FromSema = Sema::byteSizeOf(T);
    if (!FromSema || !Built || !Built->isSized()) return;
    const uint64_t FromLayout =
        mod->getDataLayout().getTypeAllocSize(Built).getFixedValue();
    if (*FromSema != FromLayout)
        codegenICE("type '" + T.Name + "' is "
                   + llvm::Twine(*FromSema) + " bytes to Sema and "
                   + llvm::Twine(FromLayout) + " bytes as it was laid out");
    checkFieldOffsetAgreement(T, Built);
}

// R4.  A record can be exactly the right size with every field in the wrong
// place, and until now only the total was ever compared -- Sema's walk and
// codegen's layout are two implementations of one algorithm, and Sema's own
// comment says it mirrors codegen's.  This asks the question that would notice.
void Codegen::Impl::checkFieldOffsetAgreement(const Type& T, llvm::Type* Built) {
    auto* st = llvm::dyn_cast_or_null<llvm::StructType>(Built);
    if (!st || T.Kind != TypeKind::Record || !T.RecordDecl) return;
    if (st->isOpaque() || !st->isSized()) return;

    Sema::FieldOffsets Want;
    if (!Sema::byteSizeOf(T, &Want)) return;

    const auto* L = layoutOfRecord(T);
    if (!L) return;
    const auto* SL = mod->getDataLayout().getStructLayout(st);

    for (const auto& [Name, Offset] : Want) {
        auto It = L->Fields.find(toLower(Name));
        if (It == L->Fields.end()) continue;
        const auto& P = It->second;
        // A field inside a variant lives at an offset within the shared blob,
        // which is a different question and not one Sema answers yet.
        if (P.InVariant || P.Index >= st->getNumElements()) continue;
        const uint64_t Got = SL->getElementOffset(P.Index);
        if (Got != Offset)
            codegenICE("field '" + Name + "' of type '" + T.Name + "' is at "
                       + llvm::Twine(Offset) + " to Sema and at "
                       + llvm::Twine(Got) + " as it was laid out");
    }
}

llvm::Type* Codegen::Impl::llvmTypeOfSemaType(const Type& T) {
    llvm::Type* Built = llvmTypeOfSemaTypeImpl(T);
    checkSizeAgreement(T, Built);
    return Built;
}

llvm::Type* Codegen::Impl::llvmTypeOfSemaTypeImpl(const Type& T) {
    switch (T.Kind) {
        case TypeKind::Integer:
        case TypeKind::Subrange:
        case TypeKind::Enum:
            // The width travels with the type; see Type::Width.  ISO 7185 and
            // Extended Pascal stamp 64 on all three, so this is the i64 they
            // have always emitted.
            return llvm::Type::getIntNTy(ctx, T.Width);
        case TypeKind::Set:
            return setTy();
        case TypeKind::Real:
            return dblTy;
        case TypeKind::Complex:
            return complexTy(); // EP §6.4.2.2: { double, double }
        case TypeKind::Boolean:
            return i1Ty;
        case TypeKind::Char:
            return i8Ty;
        case TypeKind::String:
        case TypeKind::Pointer:
        case TypeKind::Nil:
            return ptrTy;
        case TypeKind::VarString:
            return strStructType(T.StrCapacity > 0 ? T.StrCapacity : 255);
        case TypeKind::File:
            return fileStructType();
        case TypeKind::Array: {
            if (!T.IndexType || !T.ElemType) return ptrTy;
            int64_t lo  = T.IndexType->SubLo;
            int64_t hi  = T.IndexType->SubHi;
            int64_t cnt = (hi - lo + 1 > 0) ? (hi - lo + 1) : 0;
            return llvm::ArrayType::get(llvmTypeOfSemaType(*T.ElemType),
                                        static_cast<uint64_t>(cnt));
        }
        case TypeKind::Record: {
            // The declaration is the only place the variant tree survives, and
            // laying the record out from the flattened field list instead would
            // give every alternative its own storage — a different, larger
            // struct than the one a variable of the same type is allocated.
            if (T.RecordDecl) {
                SchemaBindingScope bind(*this, T);
                return structTypeFor(*T.RecordDecl);
            }
            // Build struct from record fields, keyed by LLVM type pointer sequence.
            std::vector<llvm::Type*> fieldTypes;
            std::string key;
            for (const auto& F : T.RecordFields) {
                if (!F.Ty) continue;
                auto* ft = llvmTypeOfSemaType(*F.Ty);
                fieldTypes.push_back(ft);
                key += std::to_string(std::bit_cast<uintptr_t>(ft)) + ",";
            }
            auto it = structTypes.find(key);
            if (it != structTypes.end()) return it->second;
            auto* st = llvm::StructType::get(ctx, fieldTypes);
            structTypes[key] = st;
            return st;
        }
        case TypeKind::SchemaInstance:
            if (T.SchemaBody) return llvmTypeOfSemaType(*T.SchemaBody);
            codegenICE("schema instance '" + T.Name + "' has no resolved body");
        case TypeKind::ConformantArray:
            // Passed by reference; the bounds travel alongside as separate args.
            return ptrTy;
        default:
            codegenICE("no LLVM type for semantic type '" + T.Name + "'");
    }
}

// ====================================================================
// Alloca helpers
// ====================================================================

llvm::AllocaInst* Codegen::Impl::createEntryAlloca(llvm::Type* ty, const std::string& name) {
    auto ip = builder.saveIP();
    auto& entry = curFunc->getEntryBlock();
    builder.SetInsertPoint(&entry, entry.begin());
    auto* alloca = builder.CreateAlloca(ty, nullptr, name);
    builder.restoreIP(ip);
    return alloca;
}
