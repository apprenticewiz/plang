#include "CGFieldAccess.h"

#include "llvm/IR/Constants.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Sema/Type.h"

#include "CodegenICE.h"

using namespace plang;

namespace {

const Type* recordTypeOf(const ExprNode& recExpr) {
    const Type* T = recExpr.ResolvedType.get();
    if (!T) return nullptr;
    T = schemaUnderlying(T);
    return T->Kind == TypeKind::Record ? T : nullptr;
}

/// Resolve the LLVM struct element index for a named field, using the
/// Sema-annotated record type on the expression.  Returns nothing when the
/// field cannot be matched; callers must not fall back to index 0, which
/// would read or write the wrong field.
///
/// This is the fallback for a record type with no declaration to lay out from,
/// where the struct was built by walking the same flattened field list.
/// Turbo Tier 5, Cluster A item 7: 'A.Field'/'P^.Field' for an OBJECT type,
/// outside a method body (Sema::checkField's own comment, SemaExpr.cpp, has
/// the Kind-restriction half of this same gap).  Mirrors
/// Codegen::Impl::selfFieldPtr (CodeGenProcs.cpp) exactly -- same recursive
/// walk through CGTypes::layoutOfObject's own nested-ancestor struct shape,
/// GEPing into element 0 (the ancestor sub-object, unshifted -- a struct's
/// first element always starts at its own byte offset 0) when \p fieldName
/// is not this level's own -- duplicated rather than shared because
/// selfFieldPtr is a private member of a different class (Codegen::Impl)
/// that CGFieldAccess has no access to, not because the logic differs.
llvm::Value* objectFieldPtr(llvm::IRBuilder<>& B, CGTypes& Types,
                             llvm::IntegerType* I32Ty, llvm::IntegerType* I8Ty,
                             llvm::Value* base, const Type& T,
                             const std::string& fieldName, llvm::Type*& outTy) {
    const CGTypes::RecordLayout* L = Types.layoutOfObject(T);
    if (!L) return nullptr;
    auto* zero = llvm::ConstantInt::get(I32Ty, 0);
    if (auto it = L->Fields.find(toLower(fieldName)); it != L->Fields.end()) {
        const auto& P = it->second;
        auto* fp = B.CreateGEP(L->Ty, base,
                       {zero, llvm::ConstantInt::get(I32Ty, P.Index)},
                       "obj." + fieldName);
        if (P.InVariant && P.Offset != 0)
            fp = B.CreateConstGEP1_64(I8Ty, fp, P.Offset, "obj." + fieldName);
        outTy = P.Ty;
        return fp;
    }
    if (!T.Parent) return nullptr;
    auto* parentPtr = B.CreateGEP(L->Ty, base, {zero, zero}, "obj.parent");
    return objectFieldPtr(B, Types, I32Ty, I8Ty, parentPtr, *T.Parent, fieldName, outTy);
}

/// Type-only sibling of objectFieldPtr, just below: which LLVM type does
/// \p fieldName have in object type \p T (walking the same Parent chain),
/// with no builder/address involved at all -- what fieldLlvmType needs, since
/// unlike emitFieldGEP it is asked for a TYPE alone and must not insert any
/// IR (a GEP off a throwaway base pointer would leave dead instructions in
/// whatever block happens to be open at the time it is called).
llvm::Type* objectFieldType(CGTypes& Types, const Type& T,
                             const std::string& fieldName) {
    const CGTypes::RecordLayout* L = Types.layoutOfObject(T);
    if (!L) return nullptr;
    if (auto it = L->Fields.find(toLower(fieldName)); it != L->Fields.end())
        return it->second.Ty;
    if (!T.Parent) return nullptr;
    return objectFieldType(Types, *T.Parent, fieldName);
}

std::optional<unsigned> fieldStructIndex(const ExprNode& recExpr,
                                          const std::string& fieldName,
                                          llvm::StructType* st) {
    const Type* RecTy = recordTypeOf(recExpr);
    if (!RecTy || !st) return std::nullopt;

    unsigned Idx = 0;
    for (const auto& F : RecTy->RecordFields) {
        if (Idx >= st->getNumElements()) break;
        if (eqCI(F.Name, fieldName)) return Idx;
        ++Idx;
    }
    return std::nullopt;
}

} // namespace

/// Resolve the LLVM struct type for the record in a field expression.
/// Handles both  r.field  (r is a record variable)  and
///              p^.field  (p is a pointer to a record).
llvm::StructType* CGFieldAccess::resolveRecordStructType(const FieldExpr& e) {
    // EP §6.4.7: a schematic record body has a fixed layout (Sema rejects the
    // varying non-array case), so the struct comes straight from the body type.
    // The body may itself be another schema instantiation, so the underlying
    // record is the one to lower, not the immediate body.
    if (e.Record->ResolvedType
            && e.Record->ResolvedType->Kind == TypeKind::Schema
            && e.Record->ResolvedType->SchemaBody)
        return llvm::dyn_cast<llvm::StructType>(
                   Types.llvmTypeOfSemaType(*schemaUnderlying(e.Record->ResolvedType->SchemaBody)));

    // Case 1: r.field — r is a direct record variable.
    if (auto* id = llvm::dyn_cast<IdentExpr>(e.Record.get()))
        if (auto* ve = SymTab.findVar(id->Name))
            if (auto* st = llvm::dyn_cast<llvm::StructType>(ve->type))
                return st;

    // Case 2: p^.field — p is a pointer variable; get the pointee struct type.
    // The pointer typeNode may be PointerTypeNode directly, or a NamedTypeNode
    // that is a type alias resolving to a PointerTypeNode (e.g. recptr = ^rec).
    if (auto* deref = llvm::dyn_cast<DerefExpr>(e.Record.get())) {
        // The pointee's record, from Sema.  Resolving the domain type by NAME
        // reads whichever declaration of that name is innermost at codegen
        // time, not the one the pointer was declared against: a nested
        // procedure declaring its own `rec` re-aimed every `p^.f` in its body
        // at the inner layout.  The field *index* still came from the right
        // record, so it was a GEP to an offset in a struct that had nothing to
        // do with the pointer -- `p^.b` read 1585267068834414592 for 22, with
        // no diagnostic.
        if (e.Record->ResolvedType
                && e.Record->ResolvedType->Kind == TypeKind::Record)
            if (auto* st = llvm::dyn_cast_or_null<llvm::StructType>(
                    Types.llvmTypeOfSemaType(*e.Record->ResolvedType)))
                return st;
        if (auto* innerID = llvm::dyn_cast<IdentExpr>(deref->Pointer.get())) {
            if (auto* ve = SymTab.findVar(innerID->Name)) {
                // Direct: var p: ^Rec
                if (auto* ptn = llvm::dyn_cast_or_null<PointerTypeNode>(ve->typeNode))
                    return llvm::dyn_cast<llvm::StructType>(Types.llvmTypeOfNode(*ptn->Base));
                // Via alias: type PtrRec = ^Rec;  var p: PtrRec
                if (auto* ntn = llvm::dyn_cast_or_null<NamedTypeNode>(ve->typeNode)) {
                    // Denotes first, same as llvmTypeOfNode's own NamedTypeNode
                    // case: it is what Sema resolved this name to where it was
                    // WRITTEN, not a flat table rebuilt per procedure that can
                    // only agree by coincidence.  typeAliases is a fallback for
                    // the node it cannot reach, not the first answer to ask.
                    const TypeNode* target = ntn->Denotes;
                    if (!target) {
                        auto it = TypeAliases.find(toLower(ntn->Name));
                        if (it != TypeAliases.end()) target = it->second;
                    }
                    if (auto* ptn2 = llvm::dyn_cast_or_null<PointerTypeNode>(target))
                        return llvm::dyn_cast<llvm::StructType>(
                            Types.llvmTypeOfNode(*ptn2->Base));
                }
            }
        }
    }

    // Case 3: from the Sema type, which knows the declaration it came from.
    //
    // This used to look the type's NAME up in typeAliases first, and that table
    // is rebuilt per procedure and holds the innermost declaration of a
    // spelling -- so a nested procedure declaring its own type of that name
    // re-aimed the access at the inner layout.  The p^.field branch above was
    // fixed for that; this is the path it does not cover, which is a field of
    // an array element, of a nested field, or of a function result.
    //
    // llvmTypeOfSemaType reaches the struct through Type::RecordDecl, so two
    // accesses to one declaration still share one struct -- which is what the
    // name lookup was for -- and no other declaration can be reached by
    // spelling the name again.
    // Asking for Kind == Record here is the same mistake one step further in:
    // `small = buf(8)` names a schema applied to actual discriminants, which
    // EP §6.4.9 makes an ordinary fixed-size type -- an array component, a
    // field of another record, a pointer domain.  Sema kinds it SchemaInstance
    // and hangs the record off SchemaBody, so the test failed and every such
    // record reached as anything but a directly-declared variable ICEd.
    // recordTypeOf is the look-through the field-index and layout lookups
    // below already use, so all three agree on which record is selected from.
    if (const Type* RecTy = recordTypeOf(*e.Record))
        return llvm::dyn_cast<llvm::StructType>(Types.llvmTypeOfSemaType(*RecTy));
    return nullptr;
}

llvm::Type* CGFieldAccess::fieldLlvmType(const FieldExpr& e) {
    // Turbo Tier 5, Cluster A item 7: object field, address-independent half.
    if (e.Record->ResolvedType && e.Record->ResolvedType->Kind == TypeKind::Object) {
        if (auto* ty = objectFieldType(Types, *e.Record->ResolvedType, e.Field))
            return ty;
        codegenICE("object field '" + e.Field + "' has no type in its own layout");
    }
    // A field of a variant is one of several sharing a single struct element,
    // so the element's type is the storage they share and not the field's own.
    if (const Type* RecTy = recordTypeOf(*e.Record)) {
        if (const auto* L = Types.layoutOfRecord(*RecTy)) {
            auto It = L->Fields.find(toLower(e.Field));
            if (It != L->Fields.end() && It->second.Ty) return It->second.Ty;
        }
    }
    if (auto* st = resolveRecordStructType(e)) {
        auto Idx = fieldStructIndex(*e.Record, e.Field, st);
        if (Idx && *Idx < st->getNumElements()) return st->getElementType(*Idx);
    }
    // The address of this field was worked out by the same two routes, so
    // reaching here means the load is about to read storage of a shape nobody
    // could name.  Reading it as an integer would take the first eight bytes of
    // whatever is there and call the answer a number.
    codegenICE("field '" + e.Field + "' has no type that either its record's "
               "declaration or Sema can give");
}

llvm::Value* CGFieldAccess::emitFieldGEP(const FieldExpr& e) {
    // EP §6.4.7: for p^ the body starts past the discriminant header, so the
    // record pointer has to come from the schematic view rather than emitLValue.
    llvm::Value* recPtr = nullptr;
    std::optional<SchemaAccess::SchemaRef> sref;
    if (e.Record->ResolvedType
            && e.Record->ResolvedType->Kind == TypeKind::Schema) {
        sref = Schema.schemaRefOf(*e.Record);
        if (sref) recPtr = sref->data;
    }
    if (!recPtr) recPtr = EmitLValue(*e.Record);
    if (!recPtr) return nullptr;

    // Turbo Tier 5, Cluster A item 7: 'A.Field'/'P^.Field' for an object
    // type, outside a method body -- see objectFieldPtr's own comment above.
    if (e.Record->ResolvedType && e.Record->ResolvedType->Kind == TypeKind::Object) {
        llvm::Type* outTy = nullptr;
        auto* fp = objectFieldPtr(B, Types, I32Ty, I8Ty, recPtr,
                                   *e.Record->ResolvedType, e.Field, outTy);
        if (!fp) codegenICE("object has no field named '" + e.Field + "'");
        return fp;
    }

    // EP §6.4.7: a body whose extent a discriminant fixes has no one struct --
    // layoutOf specialises per discriminant tuple and there is no tuple until
    // run time -- so the address is worked out from the declaration instead.
    // This has to come before resolveRecordStructType, because the struct it
    // would hand back is the probe's and is exactly what must not be indexed.
    //
    // Asked of the whole access PATH, not of this one field: `q^.inner.k` and
    // `q^.a[i].s` reach a run-time-laid-out component through an operand that
    // is itself not a p^, and matching on that shape is what let them fall
    // through to the probe struct.
    if (const Type* RecTy = recordTypeOf(*e.Record);
            RecTy && RecTy->ExtentVaries && RecTy->RecordDecl)
        if (auto path = Schema.schemaPathOf(e)) return path->addr;

    // Returning recPtr unchanged here would silently alias the whole record,
    // so both failures below are hard errors.
    auto* st = resolveRecordStructType(e);
    if (!st) codegenICE("cannot resolve the record type of field '" + e.Field + "'");
    auto* zero = llvm::ConstantInt::get(I32Ty, 0);

    // The declaration knows which fields share storage; the flattened field
    // list below does not, so it is only the fallback for a record that has no
    // declaration to consult.
    if (const Type* RecTy = recordTypeOf(*e.Record)) {
        if (const auto* L = Types.layoutOfRecord(*RecTy)) {
            auto It = L->Fields.find(toLower(e.Field));
            if (It == L->Fields.end())
                codegenICE("record has no field named '" + e.Field + "'");
            const auto& P = It->second;
            auto* ep = B.CreateGEP(
                st, recPtr, {zero, llvm::ConstantInt::get(I32Ty, P.Index)},
                P.InVariant ? "variant.ptr" : "field.ptr");
            if (!P.InVariant || P.Offset == 0) return ep;
            return B.CreateConstGEP1_64(I8Ty, ep, P.Offset, "field.ptr");
        }
    }

    auto FieldIdx = fieldStructIndex(*e.Record, e.Field, st);
    if (!FieldIdx) codegenICE("record has no field named '" + e.Field + "'");
    auto* fidx = llvm::ConstantInt::get(I32Ty, *FieldIdx);
    return B.CreateGEP(st, recPtr, {zero, fidx}, "field.ptr");
}

/// The alignment a load or store through \p e may honestly claim, or nullopt
/// to let IRBuilder use the value type's ABI alignment.
///
/// ISO §6.4.3.1: a packed record is stored as economically as the
/// implementation can manage, and plang packs it -- layoutOf builds an LLVM
/// struct with packed=true, whose fields sit at byte offsets that need not
/// satisfy their own types' alignment.  IRBuilder attaches the ABI alignment of
/// the VALUE TYPE when none is given, so `g.cs := ['a'..'z']` on a
/// `packed record c: char; cs: set of char; ...` emitted
///
///     store i256 %set, ptr %field.ptr, align 16
///
/// about an address at offset 1.  At -O0 the backend happened to use scalar
/// moves and the program ran; from -O1 it emits `movaps` and the program dies.
/// The project's own acceptance program crashes this way at -O2 -- it is only
/// ever run at -O0.
///
/// Same class as the variant blob R4 fixed: a promise to LLVM about an address
/// that nothing has made true.  The blob was fixed by making the promise true;
/// here the promise must simply not be made, because `packed` means the field
/// really is at an odd offset.
///
/// Issue #192: the checks below answer from the ADDRESS an expression
/// reaches, not from whether that expression happens to spell "r.field".
/// `with r do f := ...` binds a packed record's field to a bare name
/// (IdentExpr) with no FieldExpr left to ask, and `r.a[i] := ...` reaches the
/// same packed field through an IndexExpr wrapped around the FieldExpr --
/// both used to fall straight through the dyn_cast below and keep the value
/// type's default ABI alignment, so the exact -O1/-O2 crash already fixed for
/// `r.field` still happened through either of those two other shapes.
std::optional<llvm::Align> CGFieldAccess::packedAccessAlign(const ExprNode& e) {
    if (auto* fe = llvm::dyn_cast<FieldExpr>(&e)) {
        const Type* RecTy = recordTypeOf(*fe->Record);
        if (!RecTy) return std::nullopt;
        const bool packed = RecTy->Packed
                         || (RecTy->RecordDecl && RecTy->RecordDecl->Packed);
        return packed ? std::optional{llvm::Align(1)} : std::nullopt;
    }
    // A with-bound name: CGWith records the same fact on the binding itself
    // (VarEntry::packedWithField) when it creates it, since by the time an
    // IdentExpr reads or writes it there is no FieldExpr left to inspect.
    if (auto* ie = llvm::dyn_cast<IdentExpr>(&e)) {
        const VarEntry* ve = SymTab.findVar(ie->Name);
        return (ve && ve->packedWithField) ? std::optional{llvm::Align(1)}
                                            : std::nullopt;
    }
    // a[i] can claim no more than `a` itself can: an array field of a packed
    // record sits at a byte offset its element type's alignment does not
    // fix, and indexing into it does not change that, so every element
    // inherits the same ceiling as the access that reached the array.
    if (auto* xe = llvm::dyn_cast<IndexExpr>(&e))
        return packedAccessAlign(*xe->Array);
    return std::nullopt;
}

llvm::Value* CGFieldAccess::emitFieldLoad(const FieldExpr& e) {
    // EP §6.8.4: a schema-discriminant, constant for a discriminated instance
    // and carried with the value for an undiscriminated one.
    if (e.Record->ResolvedType
            && e.Record->ResolvedType->Kind == TypeKind::SchemaInstance) {
        for (const auto& D : e.Record->ResolvedType->SchemaDiscs) {
            if (eqCI(D.Name, e.Field)) {
                llvm::Type* want = D.Ty && !D.Ty->isError()
                                       ? Types.llvmTypeOfSemaType(*D.Ty) : I64Ty;
                // Issue #408: D.Value is a Sema-time placeholder (typically
                // the probe pass's stand-in, e.g. the declared subrange's low
                // bound) whenever this instantiation's own discriminant is a
                // FORM over an ENCLOSING schema's discriminant --
                // `outer(n) = record y: inner(n) end`, reading `q^.y.m` --
                // rather than a genuine compile-time constant.  `inner`'s
                // body here does not itself vary with m, so Sema still kinds
                // `q^.y` SchemaInstance (it needs no run-time header of its
                // own), but the discriminant's TRUE value is only known at
                // run time, from the enclosing object's own n.
                //
                // descendIntoInstantiation (below in this file's sibling,
                // SchemaAccess.cpp) already recomputes exactly this value --
                // through emitExtentForm over the SchemaTypeNode's
                // ActualForms -- for sizing/offset purposes; schemaPathOf
                // reaches that same computation for e.Record's own
                // instantiation, since e.Record IS the nested instance being
                // read.  It resolves to nothing for a genuinely-constant
                // instance -- one reached through neither a live schema
                // object (schemaRefOf's IdentExpr/DerefExpr arms) nor a
                // `with`-bound path -- such as #210's own `var x: t('a')`,
                // so consulting it first cannot regress that fix: D.Value is
                // still what is used whenever there is no live path to ask
                // instead.
                if (auto path = Schema.schemaPathOf(*e.Record); path && path->root.semaTy) {
                    const auto& liveDiscs = path->root.semaTy->SchemaDiscs;
                    for (size_t i = 0; i < liveDiscs.size() && i < path->root.discs.size(); ++i) {
                        if (!eqCI(liveDiscs[i].Name, e.Field)) continue;
                        llvm::Value* live = path->root.discs[i];
                        return want->isIntegerTy() && want != I64Ty
                                   ? B.CreateTrunc(live, want, "sch.disc.n")
                                   : live;
                    }
                }
                // D.Value is stored as int64_t; narrow to the declared ordinal
                // type the same way the Schema arm below narrows its
                // runtime-carried i64, so that a char or enum discriminant
                // hands back an i8/i1/... rather than a bare i64.  Without
                // this, `x.c` for `t(c: char) = ...` produced an i64 97 where
                // callers that key off the raw LLVM value (e.g. string
                // concatenation) need an i8 -- an LLVM IR verifier failure,
                // not caught by callers that key off the Sema type instead
                // (writeln, comparisons).
                llvm::Value* full = llvm::ConstantInt::get(I64Ty,
                                         static_cast<uint64_t>(D.Value), /*isSigned=*/true);
                return want->isIntegerTy() && want != I64Ty
                           ? B.CreateTrunc(full, want, "sch.disc.n")
                           : full;
            }
        }
        // Not a discriminant — fall through to normal field access on the body.
    }
    if (e.Record->ResolvedType
            && e.Record->ResolvedType->Kind == TypeKind::Schema) {
        if (auto ref = Schema.schemaRefOf(*e.Record)) {
            const auto& discs = ref->semaTy->SchemaDiscs;
            for (size_t i = 0; i < discs.size() && i < ref->discs.size(); ++i) {
                if (!eqCI(discs[i].Name, e.Field)) continue;
                // Discriminants travel as i64; narrow to the declared ordinal
                // type so that char and enum discriminants print correctly.
                llvm::Type* want = discs[i].Ty && !discs[i].Ty->isError()
                                       ? Types.llvmTypeOfSemaType(*discs[i].Ty) : I64Ty;
                return want->isIntegerTy() && want != I64Ty
                           ? B.CreateTrunc(ref->discs[i], want, "sch.disc.n")
                           : ref->discs[i];
            }
        }
    }

    auto* ptr = emitFieldGEP(e);
    if (!ptr) codegenICE("field access '" + e.Field + "' on a non-record operand");

    auto* ld = B.CreateLoad(fieldLlvmType(e), ptr, "field");
    if (auto A = packedAccessAlign(e)) ld->setAlignment(*A);
    return ld;
}

llvm::Value* CGFieldAccess::emitDerefLoad(const DerefExpr& e) {
    // ISO §6.5.5: reading f^ reads the file's buffer variable, which the
    // runtime fills from the component at the current position.
    llvm::Value* ptrVal = FileVars.isFileVar(*e.Pointer) ? FileVars.fileBufferPtr(*e.Pointer)
                                                : EmitExpr(*e.Pointer);
    if (!ptrVal) codegenICE("dereference of an unlowerable pointer expression");
    if (ptrVal->getType()->isPointerTy() && !FileVars.isFileVar(*e.Pointer))
        RangeGuards.emitNilCheck(ptrVal);

    // Determine the pointee type from the Sema annotation on the deref
    // expression.  A record written through a name has a struct already built
    // from its declaration, and reusing it keeps p^.f and q.f agreeing on
    // field order; everything else follows from the type alone.
    // The same name lookup was here for a whole-record p^, and it was the same
    // mistake: inside a procedure that declares its own type of the pointee's
    // name, p^ was loaded as the inner record.  `t^ = 11 0 0` where the record
    // holds 11 22 33, because a { i8 } was loaded from a three-integer record
    // and stored back over it.
    llvm::Type* loadTy = I64Ty;
    if (e.ResolvedType) loadTy = Types.llvmTypeOfSemaType(*e.ResolvedType);
    return B.CreateLoad(loadTy, ptrVal, "deref");
}
