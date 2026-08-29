#include "CGTypes.h"

#include <bit>
#include <cstdio>
#include <cstdlib>

#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/Arith.h"
#include "plang/Basic/LangOptions.h"
#include "plang/Basic/PascalFileLayout.h"
#include "plang/Basic/RequiredRecordLayouts.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Sema/Sema.h"
#include "plang/Sema/Type.h"

#include "CodegenICE.h"
#include "ConstFold.h"

using namespace plang;

llvm::StructType* CGTypes::strStructType(int64_t cap) {
    auto it = strStructTypes_.find(cap);
    if (it != strStructTypes_.end()) return it->second;
    auto* arr = llvm::ArrayType::get(i8Ty, static_cast<uint64_t>(cap));
    auto* st  = llvm::StructType::get(Ctx, {i64Ty, arr});
    strStructTypes_[cap] = st;
    return st;
}

llvm::Type* CGTypes::llvmTypeOfName(const std::string& name) {
    std::string lo = toLower(name);
    // As wide as the dialect makes it; see Type::Width.  Answering i64
    // regardless is what made the two readings of `integer` disagree under
    // -std=turbo, where Sema had already resolved it to sixteen bits.
    if (lo == "integer")  return llvm::Type::getIntNTy(Ctx, Opts.defaultIntWidth());
    if (lo == "real")     return dblTy;
    if (lo == "complex")  return Complex.complexTy(); // EP §6.4.2.2: { double, double }
    if (lo == "boolean")  return i1Ty;
    if (lo == "char")     return i8Ty;
    if (lo == "string")   return ptrTy;
    if (lo == "text")      return fileStructType(); // PascalFile { FILE*, int } = 16 bytes
    if (lo == "timestamp")   return timestampStructType();  // EP §6.4.3.4
    if (lo == "bindingtype") return bindingStructType();    // EP §6.4.3.4
    // User-defined type alias — resolve through the typedef table.
    auto it = TypeAliases.find(lo);
    if (it != TypeAliases.end()) return llvmTypeOfNode(*it->second);
    // Unknown here is not an error yet: the caller retries via the Sema type.
    return nullptr;
}

llvm::Type* CGTypes::variantBlobType(uint64_t size, uint64_t align) {
    llvm::Type* cell = i8Ty;
    uint64_t    unit = 1;
    // The cell used to stop at i64, so a part holding anything that wants more
    // got a blob that wanted 8 -- and the blob is what LLVM positions, so the
    // member sat at an offset its own type does not permit.  `set of char` is
    // i256, which this data layout aligns to 16, and codegen emits its loads
    // and stores with the alignment of the TYPE: a record whose variant part
    // held one produced `store i256 ..., align 16` into a blob at offset 8 of
    // an 8-aligned global.  That is a promise to LLVM that the address is
    // 16-aligned when nothing has made it so, and an aligned vector store is
    // within its rights to fault on it.
    if      (align >= 16) { cell = llvm::Type::getInt128Ty(Ctx); unit = 16; }
    else if (align >= 8)  { cell = i64Ty;                        unit = 8; }
    else if (align >= 4)  { cell = i32Ty;                        unit = 4; }
    else if (align >= 2)  { cell = llvm::Type::getInt16Ty(Ctx);  unit = 2; }
    return llvm::ArrayType::get(cell, (size + unit - 1) / unit);
}

/// The type Sema resolved for \p nm in THIS record, or null.
///
/// R4 gave the fixed fields this and stopped there.  A variant alternative's
/// fields kept reading their own denoter, and one declaration node serves every
/// instantiation, so a record whose alternative holds a nested schema
/// instantiation was laid out for whichever instantiation Sema resolved LAST:
/// `outer(6)` got `outer(2)`'s field offsets, and its own field aliased another.
///
/// Sema's RecordFields is flattened -- §6.4.3.3 lets a variant field be
/// selected by name like any other, so every alternative's fields are in that
/// list -- which is why one lookup serves both parts of the record.
/// Name (lowercased) -> the first field of \p semaRec that semaFieldType
/// would have returned for that name, built once and cached by record
/// pointer.  This is the same linear walk semaFieldType used to do inline
/// on every call; doing it once per record instead of once per field turns
/// the whole record layout from O(fields^2) into O(fields).
const std::unordered_map<std::string, const Type::Field*>&
CGTypes::semaFieldIndexFor(const Type* semaRec) {
    auto it = semaFieldIndex_.find(semaRec);
    if (it != semaFieldIndex_.end()) return it->second;
    auto& index = semaFieldIndex_[semaRec];
    if (semaRec) {
        for (const auto& F : semaRec->RecordFields) {
            const std::string key = toLower(F.Name);
            // A name whose first occurrence fails the type test keeps
            // looking for a later one with the same name, matching
            // semaFieldType's original scan exactly.
            if (index.contains(key)) continue;
            if (F.Ty && !F.Ty->isError() && canLowerSemaType(*F.Ty))
                index.emplace(key, &F);
        }
    }
    return index;
}

llvm::Type* CGTypes::semaFieldType(const Type* semaRec, const std::string& nm) {
    if (!semaRec) return nullptr;
    const auto& index = semaFieldIndexFor(semaRec);
    auto it = index.find(toLower(nm));
    if (it == index.end()) return nullptr;
    return llvmTypeOfSemaType(*it->second->Ty);
}

uint64_t CGTypes::layoutVariantCase(const VariantCase& vc, RecordLayout& L,
                                     bool packed, unsigned blobIdx,
                                     uint64_t base, const Type* semaRec) {
    const auto& dl = Mod.getDataLayout();
    uint64_t at = base;

    auto place = [&](const std::string& name, llvm::Type* ft) {
        // ISO §6.4.3.1: a packed component is stored as economically as the
        // implementation can manage, which here means no padding in front of it.
        const uint64_t fa = packed ? 1 : dl.getABITypeAlign(ft).value();
        at = (at + fa - 1) / fa * fa;
        // A name used in two alternatives keeps its first placement; Sema has
        // already reported it, and inventing a second one would only confuse
        // the diagnostics that follow.
        L.Fields.emplace(toLower(name), FieldPlace{blobIdx, ft, true, at});
        at += dl.getTypeAllocSize(ft);
    };

    for (const auto& fd : vc.Fields) {
        llvm::Type* fromNode = nullptr;
        for (const auto& nm : fd.Names) {
            llvm::Type* ft = semaFieldType(semaRec, nm);
            if (!ft) {
                if (!fromNode) fromNode = llvmTypeOf(fd.Type.get(), nullptr);
                ft = fromNode;
            }
            place(nm, ft);
        }
    }

    // A nested variant follows this alternative's own fields, and its
    // alternatives in turn share the storage after them.
    if (vc.NestedVariant) {
        const auto& nv = *vc.NestedVariant;
        if (!nv.TagField.empty() && nv.TagType) {
            llvm::Type* tt = semaFieldType(semaRec, nv.TagField);
            place(nv.TagField, tt ? tt : llvmTypeOfNode(*nv.TagType));
        }
        uint64_t end = at;
        for (const auto& inner : nv.Cases)
            end = std::max(end,
                           layoutVariantCase(inner, L, packed, blobIdx, at,
                                             semaRec));
        at = end;
    }
    return at;
}

void CGTypes::layoutVariantPart(const VariantPart& vp, RecordLayout& L,
                                 bool packed, std::vector<llvm::Type*>& elems,
                                 const Type* semaRec) {
    if (!vp.TagField.empty() && vp.TagType) {
        llvm::Type* tt = semaFieldType(semaRec, vp.TagField);
        if (!tt) tt = llvmTypeOfNode(*vp.TagType);
        L.Fields[toLower(vp.TagField)] =
            FieldPlace{static_cast<unsigned>(elems.size()), tt, false, 0};
        elems.push_back(tt);
    }

    const auto blobIdx = static_cast<unsigned>(elems.size());
    // R4: one answer to "what does the shared run need aligning to".  This used
    // to be accumulated here, field by field, while the run-time walk worked
    // the same number out separately -- and a cap on it ended up written down
    // in three places, none of which knew about the others.
    uint64_t size = 0;
    const uint64_t align = SchemaLayout.rtVariantRunAlign(vp);
    for (const auto& vc : vp.Cases)
        size = std::max(size,
                        layoutVariantCase(vc, L, packed, blobIdx, 0, semaRec));
    // Every alternative may be empty — `case b: boolean of true: (); false: ()`
    // is a record with a tag and nothing else — and then there is nothing to
    // reserve and no field that would have referred to it.
    if (size > 0) elems.push_back(variantBlobType(size, packed ? 1 : align));
}

CGTypes::SchemaBindingScope::SchemaBindingScope(CGTypes& Owner, const Type& T)
        : Owner(Owner), SavedCtx(Owner.schemaCtx_) {
    for (const auto& [name, value] : T.SchemaBindings) {
        const std::string key = toLower(name);
        auto it = Owner.Consts.find(key);
        Saved.emplace_back(key, it != Owner.Consts.end()
                                    ? std::optional{it->second}
                                    : std::nullopt);
        Owner.Consts[key] = llvm::ConstantInt::get(Owner.i64Ty,
                            static_cast<uint64_t>(value), /*isSigned=*/true);
        Owner.schemaCtx_ += "|" + key + "=" + std::to_string(value);
    }
}

CGTypes::SchemaBindingScope::~SchemaBindingScope() {
    for (const auto& [key, prior] : Saved) {
        if (prior) Owner.Consts[key] = *prior;
        else       Owner.Consts.erase(key);
    }
    Owner.schemaCtx_ = SavedCtx;
}

const CGTypes::RecordLayout* CGTypes::layoutOfRecord(const Type& T) {
    if (T.Kind != TypeKind::Record || !T.RecordDecl) return nullptr;
    SchemaBindingScope bind(*this, T);
    return &layoutOf(*T.RecordDecl, &T);
}

const CGTypes::RecordLayout& CGTypes::layoutOf(const RecordTypeNode& rt,
                                                const Type* semaRec) {
    const auto key0 = std::tuple{&rt, schemaCtx_, semaRec};
    if (auto it = recordLayouts_.find(key0); it != recordLayouts_.end())
        return it->second;

    RecordLayout L;
    std::vector<llvm::Type*> elems;
    // ISO §6.4.3.1 leaves what `packed` does to the implementation, and plang
    // used to do nothing with it.  It packs now, in every dialect: Turbo needs
    // it for {$PACKRECORDS 1} and for a record image a real Turbo program can
    // read, and a `packed` that packs nothing is a word the language has that
    // means nothing.
    const bool packed = rt.Packed;
    // R4: a field's type comes from what SEMA resolved for THIS record, not
    // from re-reading the field's denoter.  One declaration node serves every
    // instantiation and carries whichever was resolved last, so a record
    // holding `x: inner(n)` in a program that also mentions the schema
    // undiscriminated was laid out with x at the PROBE's size -- the static
    // struct came out { [4 x i64], [1 x i64], i64 } for t(4), and every field
    // behind x sat at an offset the run-time walk disagreed with.
    //
    // The Sema-against-codegen offset check was green through it, because both
    // sides were reading the same stale annotation.  Two answers agreeing is
    // not the same as either being right.
    const auto semaFieldTy = [&](const std::string& nm) -> llvm::Type* {
        return semaFieldType(semaRec, nm);
    };
    for (const auto& fd : rt.Fields) {
        llvm::Type* fromNode = nullptr;
        for (const auto& nm : fd.Names) {
            llvm::Type* ft = semaFieldTy(nm);
            if (!ft) {
                if (!fromNode) fromNode = llvmTypeOf(fd.Type.get(), nullptr);
                ft = fromNode;
            }
            L.Fields[toLower(nm)] =
                FieldPlace{static_cast<unsigned>(elems.size()), ft, false, 0};
            elems.push_back(ft);
        }
    }
    if (rt.Variant) layoutVariantPart(*rt.Variant, L, packed, elems, semaRec);

    // Two records laid out the same way share one struct type.  The names are
    // not part of the key: they are what the layout is for, and the struct only
    // has to give a GEP the right shape.
    // Packedness is part of the key.  Two records with the same field types
    // and different packing are different layouts, and sharing one struct
    // between them would give the packed one the padded one's offsets.
    std::string key = packed ? "P:" : "U:";
    for (auto* t : elems) key += std::to_string(std::bit_cast<uintptr_t>(t)) + ",";
    auto it = structTypes_.find(key);
    L.Ty = (it != structTypes_.end()) ? it->second
                                      : llvm::StructType::get(Ctx, elems, packed);
    structTypes_[key] = L.Ty;
    return recordLayouts_.emplace(key0, std::move(L)).first->second;
}

llvm::StructType* CGTypes::structTypeFor(const RecordTypeNode& rt) {
    return layoutOf(rt).Ty;
}

// See NumTypeKinds in AstBase.h.
static_assert(NumTypeKinds == 14,
              "a new type denoter needs a case in llvmTypeOfNode");

// Lowers a type denoter.  The syntax is used where it carries information the
// semantic type does not — array bounds, for instance, which codegen can fold
// in cases Sema leaves symbolic.  Anything the syntax cannot answer falls back
// to the type Sema resolved for the node, and a denoter neither can lower is an
// internal error rather than a silent integer.
llvm::Type* CGTypes::llvmTypeOfNode(const TypeNode& node) {
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
        if (schemaCtx_.empty() && node.ResolvedType && !node.ResolvedType->isError()
                && canLowerSemaType(*node.ResolvedType))
            return llvmTypeOfSemaType(*node.ResolvedType);
        // Which declaration this name denotes, from Sema, recorded in the scope
        // the name was WRITTEN in.  It is asked here and not only above because
        // the branch above is skipped inside a schema instantiation -- there the
        // node's resolved TYPE is the probe's and cannot be used, but the
        // question "which declaration is this name" still has a correct answer
        // and Sema still has it.  What stood here was llvmTypeOfName, which
        // falls through to `typeAliases`: flat, keyed by spelling, populated at
        // the USE site.  Measured at 0 disagreements across the suite once the
        // Sema-side scope fixes landed -- so this is not a bug fix, it is
        // removing the way one could arrive without anything saying so.
        if (n->Denotes) return llvmTypeOfNode(*n->Denotes);
        if (auto* t = llvmTypeOfName(n->Name)) return t;
        return llvmTypeOfNodeViaSema(node, "unknown type name '" + n->Name + "'");
    }

    if (auto* n = llvm::dyn_cast<ArrayTypeNode>(&node)) {
        // A bound this cannot fold used to become zero, and an array whose
        // extent came out zero or negative became a [0 x T] that every index
        // then ran off the end of.  Sema resolved the same type and rejects a
        // bound that is not constant, so defer to it rather than guess.
        //
        // The same "defer to Sema" answer also covers a bound pair that DID
        // fold but whose count does not fit a uint64_t (array[low(int64)..
        // high(int64)] and its kin, issue #215): ordinalRangeCount reports
        // that as nullopt rather than silently wrapping "hi - lo + 1" into a
        // plausible-looking small count, the same way "did not fold" is
        // handled just below -- Sema's own byteSizeOf makes the identical
        // check, and for anything reaching this from a global variable's
        // declared type has already turned it into a proper diagnostic
        // before codegen ever runs (issue #214).
        auto range = arrayIndexRange(*n);
        const auto cnt = range ? ordinalRangeCount(range->first, range->second)
                               : std::optional<uint64_t>{0};
        if (!cnt || *cnt == 0)
            return llvmTypeOfNodeViaSema(node, "array bounds did not fold");
        return llvm::ArrayType::get(llvmTypeOfNode(*n->Element), *cnt);
    }
    if (auto* n = llvm::dyn_cast<RecordTypeNode>(&node))
        return structTypeFor(*n);

    if (llvm::dyn_cast<PointerTypeNode>(&node))   return ptrTy;
    // Turbo procedural VALUES: this arm is reached only for a procedural
    // TYPE'S own denoter -- a variable, a record field, an array element,
    // and so on -- never for a procedural PARAMETER's ProcedureTypeNode,
    // which CodeGenProcs.cpp's parameter loop matches and lowers to its own
    // {entry point, frame} ABI shape BEFORE ever calling llvmTypeOfNode on
    // it (see the `if (auto* pt = llvm::dyn_cast<ProcedureTypeNode>(...))`
    // arm there, which `continue`s past the rest of that loop).  A
    // procedural VARIABLE has no frame slot to carry, so it lowers as one
    // flat pointer, same as PointerTypeNode just above.
    if (llvm::dyn_cast<ProcedureTypeNode>(&node)) return ptrTy;
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
        // storage, and CodeGenSchema lays the real one out at run time.  Every
        // other route now has an answer or is an internal error, rather than a
        // number that describes nothing.
        if (schemaCtx_.empty() && node.ResolvedType
                && node.ResolvedType->Kind == TypeKind::VarString)
            return strStructType(node.ResolvedType->StrCapacity);
        if (auto Cap = tryEvalConstInt(*n->Capacity, &Consts))
            return strStructType(*Cap);
        if (schemaCtx_.empty())
            codegenICE("a string capacity that is neither resolved nor "
                       "constant-foldable");
        return strStructType(PlangMaxStringCapacity);
    }
    if (llvm::dyn_cast<EnumTypeNode>(&node))      return ordinalTyOf(node);
    if (llvm::dyn_cast<SetTypeNode>(&node))       return Sets.setTy();
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

llvm::Type* CGTypes::llvmTypeOfNodeViaSema(const TypeNode& node,
                                            const std::string& what) {
    if (node.ResolvedType && !node.ResolvedType->isError())
        return llvmTypeOfSemaType(*node.ResolvedType);
    codegenICE(what + " and Sema left it unresolved");
}

// R1.  Sema folded these bounds in the scope they were WRITTEN in; folding
// them here folds them where the denoter is being LOWERED, against a
// constant table that holds whatever is innermost at that moment.  A record
// whose field is `array[1..n]` and whose layout is first computed inside a
// procedure declaring its own `n` came out sized for the stranger's n --
// caught, on a legal ISO 7185 program, as "takes 16 bytes as it is written
// and 80 bytes as Sema resolved it".
std::optional<std::pair<int64_t, int64_t>>
CGTypes::arrayIndexRange(const ArrayTypeNode& n) const {
    // Inside a schema instantiation the syntax is still the only answer: a
    // bound over a discriminant is a constant per instance and not in the
    // syntax, and Sema's annotation there is the probe's.  Same exemption the
    // size-agreement guard makes.
    if (schemaCtx_.empty() && n.ResolvedType
            && n.ResolvedType->Kind == TypeKind::Array && n.ResolvedType->IndexType)
        if (auto R = ordinalRange(*n.ResolvedType->IndexType)) return R;
    if (n.Low && n.High) {
        const auto lo = tryEvalConstInt(*n.Low,  &Consts);
        const auto hi = tryEvalConstInt(*n.High, &Consts);
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

// See NumSemaTypeKinds in Sema/Type.h.  A kind this file has not been taught
// falls into the default of canLowerSemaType, which reports it as not
// lowerable, and a variable of it is then refused for a reason that names
// nothing -- or reaches llvmTypeOfSemaType and is lowered as an i64 -- or
// reaches debugTypeOfSemaType and is silently given no DIType at all,
// which is the *correct*, deliberate answer for a composite kind but a
// silent gap for a new scalar-like kind that should have gotten one.
static_assert(NumSemaTypeKinds == 21,
              "a new semantic type kind needs a case in canLowerSemaType, "
              "llvmTypeOfSemaType, and (if it is scalar-like) "
              "debugTypeOfSemaType");

/// Whether llvmTypeOfSemaType has a lowering for \p T.  An undiscriminated
/// schema has none — its extent is not known until it is passed or allocated —
/// and neither has the error type.
bool CGTypes::canLowerSemaType(const Type& T) {
    switch (T.Kind) {
    case TypeKind::Integer:  case TypeKind::Subrange: case TypeKind::Enum:
    case TypeKind::Set:      case TypeKind::Real:     case TypeKind::Complex:
    case TypeKind::Boolean:  case TypeKind::Char:     case TypeKind::String:
    case TypeKind::Pointer:  case TypeKind::Nil:      case TypeKind::VarString:
    case TypeKind::File:     case TypeKind::Array:    case TypeKind::Record:
    case TypeKind::ConformantArray:
    // Turbo procedural VALUES: see llvmTypeOfSemaTypeImpl's identical case.
    case TypeKind::Procedure: case TypeKind::Function:
        return true;
    case TypeKind::SchemaInstance:
        return T.SchemaBody && canLowerSemaType(*T.SchemaBody);
    default:
        return false;
    }
}

llvm::Type* CGTypes::llvmTypeOf(const TypeNode* denoter, const Type* resolved) {
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
    // An extent a discriminant fixes makes both answers meaningless here: Sema
    // holds the probe's and the syntax cannot fold the bound at all, so they
    // differ by construction.  Neither is the storage -- CodeGenSchema lays it
    // out at run time -- so there is nothing for the two to agree about.
    if (schemaCtx_.empty() && resolved && !resolved->isError()
            && !resolved->ExtentVaries && canLowerSemaType(*resolved)) {
        auto* fromSema = llvmTypeOfSemaType(*resolved);
        const auto& dl  = Mod.getDataLayout();
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

/// The integer type an ordinal denoter lowers to, taken from what Sema
/// resolved it to.  Falls back to the dialect's own integer width, for a
/// denoter inside a schema body that no instantiation has annotated yet.
llvm::Type* CGTypes::ordinalTyOf(const TypeNode& node) {
    const Type* T = node.ResolvedType.get();
    return llvm::Type::getIntNTy(Ctx, T ? T->Width : Opts.defaultIntWidth());
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
void CGTypes::checkSizeAgreement(const Type& T, llvm::Type* Built) {
    // Inside a schema body the two are meant to differ: the denoters carry
    // the last instantiation's annotation while the storage being laid out is
    // this one's.  The check above llvmTypeOfNodeChecked skips it for the same
    // reason.  Nor is there a fixed instance to check yet where the extent
    // still varies -- both are genuinely unanswerable, not merely skipped.
    if (!schemaCtx_.empty() || T.ExtentVaries) {
        if (const char* Log = ::getenv("PLANG_SKIPGATE_LOG")) {
            const auto FS = Sema::byteSizeOf(T);
            if (FS && Built && Built->isSized()) {
                const uint64_t FL =
                    Mod.getDataLayout().getTypeAllocSize(Built).getFixedValue();
                if (FILE* F = ::fopen(Log, "a")) {
                    ::fprintf(F, "%s|%s|ctx=%s|bind=%d|vary=%d|sema=%llu|layout=%llu\n",
                              (*FS != FL ? "DISAGREE" : "agree"),
                              T.Name.c_str(), schemaCtx_.c_str(),
                              (int)!T.SchemaBindings.empty(), (int)T.ExtentVaries,
                              (unsigned long long)*FS, (unsigned long long)FL);
                    ::fclose(F);
                }
            }
        }
        return;
    }
    // SchemaBindings marks a record that *is* an instantiation: it resolves
    // to a plain Record, but its field denoters still carry whichever
    // instantiation was resolved last -- which is exactly what the run-time
    // walk (built to evaluate against BOUND discriminants, unlike the
    // denoters) does not depend on.  Sema::byteSizeOf structurally cannot
    // answer here -- it has no per-instance discriminant values -- so this
    // was the one layout question left checked zero ways instead of two.
    // T.SchemaBindings is stamped in declaration order (see the ordering
    // comment where it is built, SemaType.cpp), which is exactly the index
    // convention ExtentForm::Op::Disc uses.
    if (!T.SchemaBindings.empty()) {
        if (!T.RecordDecl || !Built || !Built->isSized()) return;
        std::vector<llvm::Value*> Discs;
        Discs.reserve(T.SchemaBindings.size());
        for (const auto& [Name, Value] : T.SchemaBindings)
            Discs.push_back(llvm::ConstantInt::get(i64Ty, static_cast<uint64_t>(Value), true));
        SchemaLayoutEngine::RtDiscScope Scope(SchemaLayout, Discs);
        auto* RtSize = llvm::dyn_cast_or_null<llvm::ConstantInt>(
            SchemaLayout.rtSizeOfTypeNode(T.RecordDecl));
        if (!RtSize)
            codegenICE("type '" + T.Name + "' has no constant size in the "
                       "run-time walk, though its extent does not vary");
        const uint64_t FromLayout =
            Mod.getDataLayout().getTypeAllocSize(Built).getFixedValue();
        if (RtSize->getZExtValue() != FromLayout)
            codegenICE("type '" + T.Name + "' is "
                       + llvm::Twine(FromLayout) + " bytes as it was laid "
                       "out and " + llvm::Twine(RtSize->getZExtValue())
                       + " to the run-time walk");
        checkSchemaFieldOffsetAgreement(T, Built);
        return;
    }
    const auto FromSema = Sema::byteSizeOf(T);
    if (!FromSema || !Built || !Built->isSized()) return;
    const uint64_t FromLayout =
        Mod.getDataLayout().getTypeAllocSize(Built).getFixedValue();
    if (*FromSema != FromLayout)
        codegenICE("type '" + T.Name + "' is "
                   + llvm::Twine(*FromSema) + " bytes to Sema and "
                   + llvm::Twine(FromLayout) + " bytes as it was laid out");
    checkFieldOffsetAgreement(T, Built);
}

// checkFieldOffsetAgreement's sibling for a bound schema instance.  Sema's
// byteSizeOf cannot supply a Want map here (no per-instance discriminant
// values), so this compares the same two independent answers
// checkFieldOffsetAgreement's own R4 addition compares -- the static
// layout and the run-time walk -- without the three-way Sema leg.  Called
// under the RtDiscScope checkSizeAgreement already established from
// T.SchemaBindings, so rtFieldOffset evaluates against this instance's
// actual discriminants rather than a probe's.
void CGTypes::checkSchemaFieldOffsetAgreement(const Type& T, llvm::Type* Built) {
    auto* st = llvm::dyn_cast_or_null<llvm::StructType>(Built);
    if (!st || !T.RecordDecl || st->isOpaque() || !st->isSized()) return;

    const auto* L = layoutOfRecord(T);
    if (!L) return;
    const auto* SL = Mod.getDataLayout().getStructLayout(st);

    // One run-time walk of the whole record, not one restarted per field --
    // see rtAllFieldOffsets.  A name repeated across variant alternatives
    // keeps its FIRST walk-order offset, matching what rtFieldOffset itself
    // would have returned for it.
    std::unordered_map<std::string, llvm::Value*> RtOffsets;
    for (auto& [Name, Off] : SchemaLayout.rtAllFieldOffsets(*T.RecordDecl))
        RtOffsets.try_emplace(toLower(Name), Off);

    for (const auto& F : T.RecordFields) {
        auto It = L->Fields.find(toLower(F.Name));
        if (It == L->Fields.end()) continue;
        const auto& P = It->second;
        if (P.Index >= st->getNumElements()) continue;
        const uint64_t FromLayout = SL->getElementOffset(P.Index)
                                  + (P.InVariant ? P.Offset : 0);

        auto RtIt = RtOffsets.find(toLower(F.Name));
        auto* RtC = RtIt != RtOffsets.end()
            ? llvm::dyn_cast_or_null<llvm::ConstantInt>(RtIt->second) : nullptr;
        if (!RtC)
            codegenICE("field '" + F.Name + "' of type '" + T.Name + "' has "
                       "no constant offset in the run-time walk, though the "
                       "type does not vary");
        if (RtC->getZExtValue() != FromLayout)
            codegenICE("field '" + F.Name + "' of type '" + T.Name + "' is "
                       "at " + llvm::Twine(FromLayout) + " as it was laid "
                       "out and at " + llvm::Twine(RtC->getZExtValue())
                       + " to the run-time walk");
    }
}

// R4.  A record can be exactly the right size with every field in the wrong
// place, and until now only the total was ever compared -- Sema's walk and
// codegen's layout are two implementations of one algorithm, and Sema's own
// comment says it mirrors codegen's.  This asks the question that would notice.
void CGTypes::checkFieldOffsetAgreement(const Type& T, llvm::Type* Built) {
    auto* st = llvm::dyn_cast_or_null<llvm::StructType>(Built);
    if (!st || T.Kind != TypeKind::Record || !T.RecordDecl) return;
    if (st->isOpaque() || !st->isSized()) return;

    Sema::FieldOffsets Want;
    if (!Sema::byteSizeOf(T, &Want)) return;

    const auto* L = layoutOfRecord(T);
    if (!L) return;
    const auto* SL = Mod.getDataLayout().getStructLayout(st);

    // One run-time walk of the whole record instead of one restarted per
    // field -- see rtAllFieldOffsets.  Below used to call rtFieldOffset
    // (itself an O(fields) walk from the top) once per entry in Want, an
    // O(fields) loop, making this whole check O(fields^2).
    std::unordered_map<std::string, llvm::Value*> RtOffsets;
    for (auto& [Name, Off] : SchemaLayout.rtAllFieldOffsets(*T.RecordDecl))
        RtOffsets.try_emplace(toLower(Name), Off);

    for (const auto& [Name, Offset] : Want) {
        auto It = L->Fields.find(toLower(Name));
        if (It == L->Fields.end()) continue;
        const auto& P = It->second;
        if (P.Index >= st->getNumElements()) continue;
        // A field inside a variant is placed at an offset within the shared
        // run, so its absolute position is where the run starts plus that.
        // Sema now reports these too, which matters: the one layout
        // disagreement anybody has actually found -- over whether a TAGLESS
        // selector reserves storage for a tag that does not exist -- was in a
        // variant part, and comparing only the fixed fields would have been
        // green through it.
        const uint64_t Got = SL->getElementOffset(P.Index)
                           + (P.InVariant ? P.Offset : 0);
        if (Got != Offset)
            codegenICE("field '" + Name + "' of type '" + T.Name + "' is at "
                       + llvm::Twine(Offset) + " to Sema and at "
                       + llvm::Twine(Got) + " as it was laid out");

        // R4: and the THIRD implementation.  Sema's walk and the static layout
        // were compared above; the run-time walk in CodeGenSchema is a separate
        // traversal of the same declaration, and until now nothing compared it
        // to either.  What covered it was the differential harness -- a
        // behavioural test over the schema programs somebody thought to write.
        //
        // On a record with nothing varying, the walk's arithmetic is all
        // constants and folds to a number without emitting an instruction, so
        // the comparison is available on EVERY record the compiler lays out,
        // not only on the ones a test exercises.  A result that does not fold
        // is itself the finding: it means this walk thinks something varies
        // that the static layout was certain did not.
        auto RtIt = RtOffsets.find(toLower(Name));
        auto* RtC = RtIt != RtOffsets.end()
            ? llvm::dyn_cast_or_null<llvm::ConstantInt>(RtIt->second) : nullptr;
        if (!RtC)
            codegenICE("field '" + Name + "' of type '" + T.Name + "' has no "
                       "constant offset in the run-time walk, though the type "
                       "does not vary");
        if (RtC->getZExtValue() != Got)
            codegenICE("field '" + Name + "' of type '" + T.Name + "' is at "
                       + llvm::Twine(Got) + " as it was laid out and at "
                       + llvm::Twine(RtC->getZExtValue())
                       + " to the run-time walk");
    }
}

llvm::Type* CGTypes::llvmTypeOfSemaType(const Type& T) {
    llvm::Type* Built = llvmTypeOfSemaTypeImpl(T);
    checkSizeAgreement(T, Built);
    return Built;
}

llvm::Type* CGTypes::llvmTypeOfSemaTypeImpl(const Type& T) {
    switch (T.Kind) {
        case TypeKind::Integer:
        case TypeKind::Subrange:
        case TypeKind::Enum:
            // The width travels with the type; see Type::Width.  ISO 7185 and
            // Extended Pascal stamp 64 on all three, so this is the i64 they
            // have always emitted.
            return llvm::Type::getIntNTy(Ctx, T.Width);
        case TypeKind::Set:
            return Sets.setTy();
        case TypeKind::Real:
            return dblTy;
        case TypeKind::Complex:
            return Complex.complexTy(); // EP §6.4.2.2: { double, double }
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
            // See the identical guard in llvmTypeOfNode's ArrayTypeNode arm
            // and Sema::byteSizeOf's Array case: "hi - lo + 1" done directly
            // in int64_t is signed-overflow UB once the bounds are far
            // enough apart, and used to silently become a plausible-looking
            // small (or zero) count instead of the astronomically large one
            // the declaration actually asks for (issue #215).  Unlike that
            // sibling there is no TypeNode here to defer to Sema through --
            // T already IS Sema's own answer -- so an extent that does not
            // fit a uint64_t is a codegen internal error: reaching here with
            // one means it came from somewhere Sema's byteSizeOf gate does
            // not cover (a local variable, say -- that gate is global-scope
            // only), and building the array type anyway would silently emit
            // a wrong, crash-prone program instead of failing to compile it.
            const auto cnt = ordinalRangeCount(T.IndexType->SubLo, T.IndexType->SubHi);
            if (!cnt)
                codegenICE("an array type has more elements than plang can "
                           "represent");
            return llvm::ArrayType::get(llvmTypeOfSemaType(*T.ElemType), *cnt);
        }
        case TypeKind::Record: {
            // The declaration is the only place the variant tree survives, and
            // laying the record out from the flattened field list instead would
            // give every alternative its own storage — a different, larger
            // struct than the one a variable of the same type is allocated.
            if (T.RecordDecl) {
                SchemaBindingScope bind(*this, T);
                // R4: T goes with the node.  Dropping it here is what left the
                // layout re-reading each field's DENOTER, and one declaration
                // node carries whichever instantiation was resolved last -- so
                // a program that mentions a schema undiscriminated ANYWHERE
                // laid out every discriminated instance of it with the probe's
                // field sizes.  Merely declaring `procedure b(var v: t)`
                // changed the layout of an unrelated `var a: t(4)`.
                return layoutOf(*T.RecordDecl, &T).Ty;
            }
            // EP §6.4.3.4: TimeStamp and BindingType have no backing syntax --
            // they are built directly onto RecordFields by
            // Sema::registerBuiltins(), the only two Record-kind types with
            // no RecordDecl at all -- so a record actually written in a
            // program (even one that shadows either name) always takes the
            // branch above instead.  Routed to the checked builders in
            // RequiredRecordLayouts.h rather than the field-walk below so
            // that a program's own storage and the runtime's own struct are
            // the same question asked once, not built twice and hoped to
            // still agree.
            if (T.Name == "TimeStamp")   return timestampStructType();
            if (T.Name == "BindingType") return bindingStructType();
            // Build struct from record fields, keyed by LLVM type pointer sequence.
            std::vector<llvm::Type*> fieldTypes;
            std::string key;
            for (const auto& F : T.RecordFields) {
                if (!F.Ty) continue;
                auto* ft = llvmTypeOfSemaType(*F.Ty);
                fieldTypes.push_back(ft);
                key += std::to_string(std::bit_cast<uintptr_t>(ft)) + ",";
            }
            auto it = structTypes_.find(key);
            if (it != structTypes_.end()) return it->second;
            auto* st = llvm::StructType::get(Ctx, fieldTypes);
            structTypes_[key] = st;
            return st;
        }
        case TypeKind::SchemaInstance:
            if (T.SchemaBody) return llvmTypeOfSemaType(*T.SchemaBody);
            codegenICE("schema instance '" + T.Name + "' has no resolved body");
        case TypeKind::ConformantArray:
            // Passed by reference; the bounds travel alongside as separate args.
            return ptrTy;
        // Turbo procedural VALUES: a flat function pointer, never the
        // {entry point, frame} pair a PARAMETER of this same denoter gets
        // (see llvmTypeOfNode's identical ProcedureTypeNode arm, and
        // ClosureAndCallABI's own top comment for why the two ABIs differ).
        case TypeKind::Procedure:
        case TypeKind::Function:
            return ptrTy;
        default:
            codegenICE("no LLVM type for semantic type '" + T.Name + "'");
    }
}

// ISO §6.5: a file variable's storage laid out to match the runtime's
// PascalFile, which is declared once in plang/Basic/PascalFileLayout.h and
// read by both sides.
//
// Building it from a list is only half the job — the list has to be the
// same one the runtime compiled.  So every field's offset, and the whole
// size, are checked against that struct here, the first time the type is
// wanted.  A field added, widened or reordered on either side alone stops
// the compiler rather than leaving generated code reading a field at an
// offset nothing wrote it to.
llvm::StructType* CGTypes::fileStructType() {
    // Cached on this Codegen and not in a static.  A static outlives the
    // LLVMContext that owns the type, so a second compilation in one
    // process -- which the binary never does and anything embedding the
    // front end does immediately -- got a type belonging to a context that
    // had been destroyed, and took a segmentation fault building a null
    // value of it.
    if (fileStructTy_) return fileStructTy_;
    llvm::StructType*& FST = fileStructTy_;

    FST = llvm::StructType::get(
        Ctx, {
#define PLANG_FILE_FIELD_TYPE(Member, LLVMTy) LLVMTy,
            PLANG_FILE_FIELDS(PLANG_FILE_FIELD_TYPE)
#undef PLANG_FILE_FIELD_TYPE
        });

    const auto& dl     = Mod.getDataLayout();
    const auto* layout = dl.getStructLayout(FST);
    if (FST->getNumElements() != PlangFileFieldCount)
        codegenICE("the file record has " + std::to_string(PlangFileFieldCount)
                   + " fields and codegen built "
                   + std::to_string(FST->getNumElements()));
    if (dl.getTypeAllocSize(FST) != sizeof(PascalFile))
        codegenICE("a file variable takes "
                   + std::to_string(dl.getTypeAllocSize(FST).getFixedValue())
                   + " bytes as codegen lays it out and "
                   + std::to_string(sizeof(PascalFile))
                   + " as the runtime declares it");
    unsigned idx = 0;
#define PLANG_FILE_FIELD_OFFSET(Member, LLVMTy)                                \
    if (layout->getElementOffset(idx) != offsetof(PascalFile, Member))     \
        codegenICE("the file record's '" #Member "' is at offset "         \
                   + std::to_string(layout->getElementOffset(idx))         \
                   + " as codegen lays it out and "                        \
                   + std::to_string(offsetof(PascalFile, Member))          \
                   + " as the runtime declares it");                       \
    ++idx;
    PLANG_FILE_FIELDS(PLANG_FILE_FIELD_OFFSET)
#undef PLANG_FILE_FIELD_OFFSET
    return FST;
}

// EP §6.4.3.4: DateValid, year, month, day, TimeValid, hour, minute,
// second.  Built from and checked against PlangTimeStamp, declared once
// in plang/Basic/RequiredRecordLayouts.h and read by the runtime too;
// see fileStructType's own comment for why this checks rather than
// asserts alone.
llvm::StructType* CGTypes::timestampStructType() {
    // Cached on this Codegen and not in a static; see fileStructType.
    if (timestampTy_) return timestampTy_;
    llvm::StructType*& TST = timestampTy_;

    TST = llvm::StructType::get(
        Ctx, {
#define PLANG_TIMESTAMP_FIELD_TYPE(Member, LLVMTy) LLVMTy,
            PLANG_TIMESTAMP_FIELDS(PLANG_TIMESTAMP_FIELD_TYPE)
#undef PLANG_TIMESTAMP_FIELD_TYPE
        });

    const auto& dl     = Mod.getDataLayout();
    const auto* layout = dl.getStructLayout(TST);
    if (TST->getNumElements() != PlangTimeStampFieldCount)
        codegenICE("TimeStamp has " + std::to_string(PlangTimeStampFieldCount)
                   + " fields and codegen built "
                   + std::to_string(TST->getNumElements()));
    if (dl.getTypeAllocSize(TST) != sizeof(PlangTimeStamp))
        codegenICE("a TimeStamp variable takes "
                   + std::to_string(dl.getTypeAllocSize(TST).getFixedValue())
                   + " bytes as codegen lays it out and "
                   + std::to_string(sizeof(PlangTimeStamp))
                   + " as the runtime declares it");
    unsigned idx = 0;
#define PLANG_TIMESTAMP_FIELD_OFFSET(Member, LLVMTy)                          \
    if (layout->getElementOffset(idx) != offsetof(PlangTimeStamp, Member)) \
        codegenICE("TimeStamp's '" #Member "' is at offset "              \
                   + std::to_string(layout->getElementOffset(idx))        \
                   + " as codegen lays it out and "                       \
                   + std::to_string(offsetof(PlangTimeStamp, Member))     \
                   + " as the runtime declares it");                      \
    ++idx;
    PLANG_TIMESTAMP_FIELDS(PLANG_TIMESTAMP_FIELD_OFFSET)
#undef PLANG_TIMESTAMP_FIELD_OFFSET
    return TST;
}

// EP §6.4.3.4: 'name' (a string(PlangMaxBindingName)) and 'bound', both
// required.  Built from and checked against PlangBindingType; see
// timestampStructType's own comment for why.
llvm::StructType* CGTypes::bindingStructType() {
    // Use the structTypes_ cache keyed by a unique string so the type
    // is stable across multiple llvmTypeOfName() calls within one module.
    auto it = structTypes_.find("__binding__");
    if (it != structTypes_.end()) return it->second;

    // 'bound' is i1 rather than i8 so that writing it selects the Boolean
    // formatter; an i1 still occupies one byte, matching the C struct.
    auto* BST = llvm::StructType::get(
        Ctx, {
#define PLANG_BINDINGTYPE_FIELD_TYPE(Member, LLVMTy) LLVMTy,
            PLANG_BINDINGTYPE_FIELDS(PLANG_BINDINGTYPE_FIELD_TYPE)
#undef PLANG_BINDINGTYPE_FIELD_TYPE
        });
    structTypes_["__binding__"] = BST;

    const auto& dl     = Mod.getDataLayout();
    const auto* layout = dl.getStructLayout(BST);
    if (BST->getNumElements() != PlangBindingTypeFieldCount)
        codegenICE("BindingType has "
                   + std::to_string(PlangBindingTypeFieldCount)
                   + " fields and codegen built "
                   + std::to_string(BST->getNumElements()));
    if (dl.getTypeAllocSize(BST) != sizeof(PlangBindingType))
        codegenICE("a BindingType variable takes "
                   + std::to_string(dl.getTypeAllocSize(BST).getFixedValue())
                   + " bytes as codegen lays it out and "
                   + std::to_string(sizeof(PlangBindingType))
                   + " as the runtime declares it");
    unsigned idx = 0;
#define PLANG_BINDINGTYPE_FIELD_OFFSET(Member, LLVMTy)                         \
    if (layout->getElementOffset(idx) != offsetof(PlangBindingType, Member)) \
        codegenICE("BindingType's '" #Member "' is at offset "             \
                   + std::to_string(layout->getElementOffset(idx))         \
                   + " as codegen lays it out and "                        \
                   + std::to_string(offsetof(PlangBindingType, Member))    \
                   + " as the runtime declares it");                       \
    ++idx;
    PLANG_BINDINGTYPE_FIELDS(PLANG_BINDINGTYPE_FIELD_OFFSET)
#undef PLANG_BINDINGTYPE_FIELD_OFFSET
    return BST;
}
