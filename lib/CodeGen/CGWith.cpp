#include "CGWith.h"

#include <optional>

#include "llvm/IR/Constants.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Sema/Type.h"

#include "CodegenICE.h"

using namespace plang;

void CGWith::emitWith(const WithStmt& s) {
    // with r1, r2 do stmt — open each record's fields as local variables,
    // innermost record taking priority (last opened = first consulted).
    SymTab.pushScope();
    for (const auto& rec : s.Records) {
        if (!rec->ResolvedType) continue;

        // EP §6.4.7: an undiscriminated schema has no struct to GEP into, so
        // each field is bound to the address the run-time layout gives it, and
        // each discriminant to the value the object carries.
        //
        // Asked of the ACCESS PATH and not of the type's kind.  `with q^ do` is
        // a Schema; `with q^.inner do` is an ordinary Record that merely lives
        // inside one, and keying on the kind sent it to the static branch
        // below -- where the nested `string(n)` was bound at the probe's
        // capacity, so reading a field worked and assigning to one raised
        // "string of length 7 assigned to a string(1)" on legal code.
        // Whether there is a struct to GEP into is a question about the
        // storage, which is what the path knows and the kind does not.
        // A SchemaInstance was treated as having a static layout and sent
        // straight to the static branch.  That holds for one DECLARED as such
        // -- `var v: ent(5)` -- and not for a COMPONENT of a run-time-laid-out
        // object: `p^.e` for `t(n) = record e: ent(n) ... end` is an
        // instantiation whose discriminants are values the object carries, and
        // binding its fields statically bound them at the probe's offsets.
        // `with p^.e do id := 12345` then wrote into the middle of the
        // neighbouring string, silently, exit 0.
        //
        // The rule this file already states applies: whether there is a struct
        // to GEP into is a question about the STORAGE, which the path knows and
        // the kind does not.  So ask the path in every case and let it decide;
        // a genuine `var v: ent(5)` has no schema ref, yields no path, and
        // reaches the static branch exactly as before.
        // schemaPathOf EMITS the access path -- every subscript in it -- so its
        // result is kept whatever happens next.  Discarding it and letting the
        // static branch call emitLValue below emitted the path a SECOND time:
        // `with q^.a[idx] do` called idx twice, bound the element the second
        // call chose, and left a live range check on the first.  ISO §6.8.3.10
        // says the record-variable is evaluated once.
        std::optional<SchemaAccess::SchemaPath> path = Schema.schemaPathOf(*rec);
        {
            const RecordTypeNode* rt =
                path ? llvm::dyn_cast_or_null<RecordTypeNode>(
                           PeelPackedNode(path->decl))
                     : nullptr;
            if (path && rt) {
                // The discriminants belong to the schematic variable, not to a
                // record nested inside it, so they are exposed only where the
                // body IS the schema's.
                const bool isBody = rec->ResolvedType->Kind == TypeKind::Schema;
                if (isBody) {
                    const auto& discs = rec->ResolvedType->SchemaDiscs;
                    for (size_t i = 0;
                         i < discs.size() && i < path->root.discs.size(); ++i) {
                        auto* slot = CreateEntryAlloca(I64Ty,
                                                       "disc." + discs[i].Name);
                        B.CreateStore(path->root.discs[i], slot);
                        SymTab.defVar(discs[i].Name, slot, I64Ty);
                    }
                }
                {
                    // Whatever the type really is: a Schema keeps its fields
                    // on its body, a SchemaInstance likewise, and a plain
                    // record on itself.  Reading RecordFields off an INSTANCE
                    // found an empty list, so `with p^.e do` bound nothing and
                    // every name in the body became an undefined global.
                    const plang::Type* fieldsFrom =
                        schemaUnderlying(rec->ResolvedType.get());
                    const auto& fields = fieldsFrom->RecordFields;
                    for (const auto& F : fields) {
                        auto* off = [&] {
                            SchemaLayoutEngine::RtDiscScope disc(SchemaLayout, path->root.discs);
                            return SchemaLayout.rtFieldOffset(*rt, F.Name);
                        }();
                        auto* fp = B.CreateGEP(I8Ty, path->addr, {off},
                                                     "with.fld");
                        SymTab.defVar(F.Name, fp,
                               F.Ty ? Types.llvmTypeOfSemaType(*F.Ty) : I64Ty);
                        // Keep the path, not just the address: an array field
                        // bound here is still indexed, and a nested record is
                        // still selected from.
                        Schema.setVarSchemaPath(F.Name, path->root,
                                         Schema.fieldDenoterOf(*rt, F.Name));
                        // A varying string field: record what its capacity
                        // really is, since the bound name loses the path.
                        if (F.Ty && F.Ty->Kind == TypeKind::VarString
                                && F.Ty->ExtentVaries)
                            if (auto* st = llvm::dyn_cast_or_null<StringTypeNode>(
                                    Schema.fieldDenoterOf(*rt, F.Name))) {
                                // R3: the form, not the declaration's
                                // expression re-emitted in the with-statement's
                                // scope.
                                if (!st->ExtentLow)
                                    codegenICE("a with over a schema string "
                                               "field with no capacity form");
                                auto* cap = SchemaLayout.emitExtentForm(*st->ExtentLow,
                                                           path->root.discs);
                                Schema.setVarStrCap(F.Name, cap);
                            }
                    }
                }
                continue;
            }
        }

        // EP §6.4.7: schema instance — expose discriminants as constant vars
        // and body record fields as normal GEP-derived vars.
        if (rec->ResolvedType->Kind == TypeKind::SchemaInstance) {
            // Expose discriminants as alloca'd integer constants.
            for (const auto& D : rec->ResolvedType->SchemaDiscs) {
                auto* alloca = CreateEntryAlloca(I64Ty, "disc." + D.Name);
                B.CreateStore(
                    llvm::ConstantInt::get(I64Ty,
                        static_cast<uint64_t>(D.Value), /*isSigned=*/true),
                    alloca);
                SymTab.defVar(D.Name, alloca, I64Ty);
            }
            // If body is a record, expose its fields via GEP.  The body may
            // itself be another schema instantiation (EP §6.4.7), so this
            // asks schemaUnderlying, not the immediate SchemaBody.
            const plang::Type* body = rec->ResolvedType->SchemaBody
                                     ? schemaUnderlying(rec->ResolvedType->SchemaBody.get())
                                     : nullptr;
            if (body && body->Kind == TypeKind::Record) {
                auto* recPtr = EmitLValue(*rec);
                if (!recPtr) codegenICE("'with' on a schema instance without an address");
                llvm::StructType* st = nullptr;
                if (auto* id = llvm::dyn_cast<IdentExpr>(rec.get()))
                    if (auto* ve = SymTab.findVar(id->Name))
                        st = llvm::dyn_cast<llvm::StructType>(ve->type);
                if (!st)
                    st = llvm::dyn_cast<llvm::StructType>(Types.llvmTypeOfSemaType(*body));
                if (st) {
                    // Through the layout, exactly as the record case below
                    // does: Sema's field list is flattened and the struct has
                    // one blob for all the variants, so pairing them by
                    // position binds the first variant field to the blob and
                    // never binds the rest.  A schema body may have a variant
                    // part like any other record, and this is that same walk.
                    auto* zero = llvm::ConstantInt::get(I32Ty, 0);
                    const CGTypes::RecordLayout* L = Types.layoutOfRecord(*body);
                    unsigned ElemIdx = 0;
                    for (const auto& F : body->RecordFields) {
                        llvm::Value* fldPtr = nullptr;
                        llvm::Type*  fldTy  = nullptr;
                        if (L) {
                            const auto It = L->Fields.find(toLower(F.Name));
                            if (It == L->Fields.end()) continue;
                            const auto& P = It->second;
                            fldPtr = B.CreateGEP(
                                st, recPtr,
                                {zero, llvm::ConstantInt::get(I32Ty, P.Index)},
                                "with." + F.Name);
                            if (P.InVariant && P.Offset != 0)
                                fldPtr = B.CreateConstGEP1_64(
                                    I8Ty, fldPtr, P.Offset, "with." + F.Name);
                            fldTy = P.Ty;
                        } else {
                            if (ElemIdx >= st->getNumElements()) break;
                            fldPtr = B.CreateGEP(
                                st, recPtr,
                                {zero, llvm::ConstantInt::get(I32Ty, ElemIdx)},
                                "with." + F.Name);
                            fldTy = st->getElementType(ElemIdx);
                        }
                        SymTab.defVar(F.Name, fldPtr, fldTy);
                        ++ElemIdx;
                    }
                }
            }
            continue;
        }

        // Reuse the address schemaPathOf already emitted, when it produced one.
        // Calling emitLValue here regardless is what evaluated the record
        // variable a second time.
        auto* recPtr = path ? path->addr : EmitLValue(*rec);
        if (!recPtr) codegenICE("'with' on a record that has no address");
        if (rec->ResolvedType->Kind != TypeKind::Record)
            codegenICE("'with' on a non-record operand");

        // Get the LLVM struct type from the variable entry (needed for GEP).
        llvm::StructType* st = nullptr;
        if (auto* id = llvm::dyn_cast<IdentExpr>(rec.get()))
            if (auto* ve = SymTab.findVar(id->Name))
                st = llvm::dyn_cast<llvm::StructType>(ve->type);
        // Records reached through an index or a dereference have no variable
        // entry of their own, so fall back to the type Sema resolved.
        if (!st)
            st = llvm::dyn_cast<llvm::StructType>(
                     Types.llvmTypeOfSemaType(*rec->ResolvedType));
        if (!st) codegenICE("cannot resolve the struct type for a 'with' record");

        // Expose each field as a named variable pointing into the record
        // struct, through the SAME layout that r.f goes through.
        //
        // This used to pair Sema's field list positionally with the struct's
        // elements, and the two are not the same list.  §6.4.3.3 lets a variant
        // field be selected by name like any other, so Sema's list is flattened
        // -- fixed fields, the tag, then every alternative's fields -- while the
        // struct holds the fixed fields, the tag, and ONE blob shared by all the
        // alternatives.  So the first variant field was bound to the blob and
        // every later one ran off the end of the struct and was not bound at
        // all: `with r do c := 4` stored an integer bit pattern into a real, and
        // `with r do b := 22` referred to a `pasg_b` that no one defined and
        // failed at link time.
        auto* zero = llvm::ConstantInt::get(I32Ty, 0);
        const CGTypes::RecordLayout* L = Types.layoutOfRecord(*rec->ResolvedType);
        for (const auto& F : rec->ResolvedType->RecordFields) {
            llvm::Value* fldPtr = nullptr;
            llvm::Type*  fldTy  = nullptr;
            if (L) {
                const auto It = L->Fields.find(toLower(F.Name));
                if (It == L->Fields.end()) continue;
                const auto& P = It->second;
                fldPtr = B.CreateGEP(
                    st, recPtr, {zero, llvm::ConstantInt::get(I32Ty, P.Index)},
                    "with." + F.Name);
                if (P.InVariant && P.Offset != 0)
                    fldPtr = B.CreateConstGEP1_64(I8Ty, fldPtr, P.Offset,
                                                        "with." + F.Name);
                fldTy = P.Ty;
            } else {
                // A record with no declaration to lay out -- one that came in
                // through an interface file.  Positional is all there is, and
                // it is right for a record with no variant part.
                const unsigned Idx =
                    static_cast<unsigned>(&F - rec->ResolvedType->RecordFields.data());
                if (Idx >= st->getNumElements()) break;
                fldPtr = B.CreateGEP(st, recPtr,
                             {zero, llvm::ConstantInt::get(I32Ty, Idx)},
                             "with." + F.Name);
                fldTy = st->getElementType(Idx);
            }
            SymTab.defVar(F.Name, fldPtr, fldTy);
        }
    }
    EmitStmt(s.Body.get());
    SymTab.popScope();
}
