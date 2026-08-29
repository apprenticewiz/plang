#include "CGDebugInfo.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>

#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/SourceManager.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Basic/Version.h"
#include "plang/Sema/Type.h"

#include "CGTypes.h"
#include "SchemaTypeRegistry.h"

using namespace plang;

namespace {
/// The Sema::Type::Field \p T's flattened field list holds for \p LowerName
/// (already folded, matching CGTypes::RecordLayout::Fields's own key), or
/// null.  Record/schema field names are matched case-insensitively
/// everywhere else in codegen (see CGTypes::semaFieldType); this is the
/// same lookup, used here to recover a field's ORIGINAL declared spelling
/// and its own Sema type (CGTypes::FieldPlace only keeps the LLVM type,
/// which has no field-name-worthy encoding of its own).
const plang::Type::Field* findSemaField(const plang::Type& T, std::string_view LowerName) {
    for (const auto& F : T.RecordFields)
        if (plang::eqCI(F.Name, LowerName)) return &F;
    return nullptr;
}
} // namespace

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

/// Zero-padded 16-hex-digit rendering of a 64-bit token -- used both for
/// SchemaBuildId_ (embedded in DW_AT_producer and the sidecar's "buildId")
/// and for a schema's structural fingerprint (the sidecar's per-variant
/// "fp"), so both sides of issue #140/#141's fix speak the same format.
std::string toHex16(uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    return std::string(buf);
}

/// FNV-1a, 64-bit -- a small, dependency-free, stable-across-runs hash;
/// nothing here needs cryptographic strength, only that two structurally
/// different inputs are exceedingly unlikely to collide and that plang and
/// plang_schema_printers.py (an independent Python implementation) compute
/// the identical value from the identical canonical string.
uint64_t fnv1a64(std::string_view s) {
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}
} // namespace

CGDebugInfo::CGDebugInfo(llvm::Module& Mod, llvm::LLVMContext& Ctx, llvm::IRBuilder<>& B,
                          const LangOptions& Opts, const SourceManager* SrcMgr,
                          FileID MainFileID, const std::string& ProgName)
    : Mod(Mod), Ctx(Ctx), B(B), Opts(Opts), SrcMgr(SrcMgr), MainFileID(MainFileID) {
    if (!Opts.Debug) return;
    DBuilder = std::make_unique<llvm::DIBuilder>(Mod);
    std::string Filename = ProgName, Directory;
    if (SrcMgr)
        std::tie(Filename, Directory) = splitDebugFilePath(SrcMgr->getBufferName(MainFileID));
    DebugFile = DBuilder->createFile(Filename, Directory);

    // Issue #141: a random 64-bit token, minted fresh every single time a
    // CGDebugInfo is constructed (i.e. once per compile that has -g on),
    // regardless of whether this particular compile ever ends up writing a
    // sidecar. Embedded in DW_AT_producer below (readable back from the
    // live binary's own DWARF, independent of any file on disk) and again
    // in the sidecar's own "buildId" field by writeSchemaDebugScript --
    // plang_schema_printers.py compares the two before trusting the
    // sidecar, so an older binary loading a newer/different sidecar (or
    // vice versa) is a detectable mismatch instead of silently wrong data.
    std::random_device rd;
    SchemaBuildId_ = (static_cast<uint64_t>(rd()) << 32) | static_cast<uint64_t>(rd());

    DebugCU = DBuilder->createCompileUnit(
        llvm::DISourceLanguageName(llvm::dwarf::DW_LANG_Pascal83),
        DebugFile, "plang " PLANG_VERSION_STRING " schemabuildid:" + toHex16(SchemaBuildId_),
        /*isOptimized=*/Opts.OptLevel > 0, /*Flags=*/"", /*RV=*/0);
    // DWARF cannot be read back without a producer that states which
    // version of the metadata schema it wrote; DIBuilder's own nodes
    // say nothing about this on their own.
    Mod.addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                       llvm::DEBUG_METADATA_VERSION);

    // Issue #141, defense in depth beyond the buildId check above: truncate
    // any pre-existing sidecar from an EARLIER compile of this same source
    // right now, rather than only ever overwriting it at the very end in
    // writeSchemaDebugScript. Narrows the window in which a stale sidecar
    // on disk can be read by a gdb session against some OTHER, not-yet-
    // relinked binary while this compile is still running (or if it never
    // reaches finalize() at all, e.g. it crashes or the schema pass never
    // runs because Debug got no schema types this time) -- the file is
    // simply gone rather than confidently describing a different program.
    if (auto path = schemaSidecarPath(); !path.empty()) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}

std::filesystem::path CGDebugInfo::schemaSidecarPath() const {
    if (!SrcMgr) return {};
    std::filesystem::path SourcePath(SrcMgr->getBufferName(MainFileID));
    SourcePath += ".plang-schemas.json";
    return SourcePath;
}

// -g.  Every TypeKind that reaches codegen gets a real DIType now (Record/
// Array/Set/Complex/String/VarString/Procedure/Function/Schema/
// SchemaInstance below, alongside the seven scalar/pointer kinds this
// switch always had).  File and ConformantArray still fall through to
// null -- a file variable's own storage is an implementation-defined
// runtime handle (ISO §6.5) nothing describes usefully, and a conformant
// array parameter has no one fixed shape (passed by reference, its bounds
// threaded as separate hidden arguments; see CGTypes::llvmTypeOfSemaTypeImpl's
// own ConformantArray case) -- both documented gaps, not oversights.  A
// pointer to a kind that DOES get a DIType now gets that DIType for its
// pointee; a pointer to one that still doesn't gets no DIType for its
// pointee rather than a placeholder invented for the occasion.  A null
// pointee is a documented, ordinary createPointerType input (a C `void*`'s
// own DIType is built the same way), not a special case this function has
// to guard.
llvm::DIType* CGDebugInfo::debugTypeOfSemaType(const Type& T) {
    if (!DBuilder) return nullptr;
    if (auto it = debugTypes_.find(&T); it != debugTypes_.end()) return it->second;

    llvm::DIType* DT = nullptr;
    switch (T.Kind) {
        case TypeKind::Integer:
            // The width and signedness travel with the type; see Type::Width's
            // own comment.  ISO 7185 and Extended Pascal stamp 64/signed on
            // every Integer, so this reproduces the old hardcoded 64/signed
            // output for them exactly; Turbo's Integer (16-bit signed) and any
            // other width TypeContext::getInt mints now get DWARF that matches
            // the llvm::Type CGTypes::llvmTypeOfSemaTypeImpl actually builds
            // for the same T (getIntNTy(Ctx, T.Width)), instead of always
            // claiming a 64-bit value regardless of the real one.
            DT = DBuilder->createBasicType(
                "integer", T.Width,
                T.IsSigned ? llvm::dwarf::DW_ATE_signed : llvm::dwarf::DW_ATE_unsigned);
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
            DT = DBuilder->createPointerType(PointeeDT, ptrBits());
            break;
        }
        case TypeKind::Array:
            DT = buildArrayDIType(T);
            break;
        case TypeKind::Set:
            DT = buildSetDIType(T);
            break;
        case TypeKind::Complex:
            DT = buildComplexDIType(T);
            break;
        case TypeKind::VarString:
            DT = buildStringDIType(T.StrCapacity);
            break;
        case TypeKind::String:
            // ISO 7185 unbounded string: a pointer at the LLVM level (see
            // CGTypes::llvmTypeOfSemaTypeImpl's own String case) to
            // storage shaped like VarString's -- but with no declared
            // capacity to size it by, buildStringDIType's
            // PlangMaxStringCapacity fallback is only ever a
            // REPRESENTATIVE shape here, not this particular instance's
            // real allocation size.
            DT = DBuilder->createPointerType(buildStringDIType(T.StrCapacity), ptrBits());
            break;
        case TypeKind::Record:
            DT = buildRecordDIType(T);
            break;
        case TypeKind::Procedure:
        case TypeKind::Function:
            DT = buildSubroutineDIType(T);
            break;
        case TypeKind::Schema:
        case TypeKind::SchemaInstance:
            // EP §6.4.7.  SchemaBody is the body resolved against either the
            // real discriminants (a SchemaInstance whose extent is fixed:
            // codegen stores it as a plain value with no header -- see
            // CGTypes::llvmTypeOfSemaTypeImpl's own SchemaInstance case --
            // so recursing straight into the body's DIType is exact) or a
            // PROBE binding (T.ExtentVaries: an undiscriminated Schema, or
            // one whose discriminant only arrives at run time).  The latter
            // is where SchemaLayoutEngine::schemaHeaderBytes and
            // SchemaAccess::emitNewSchema/schemaRefOf put a leading
            // discriminant-word header in front of the body at run time --
            // buildSchemaDIType accounts for it; see its own comment for why
            // recursing into the probe body as if it had none (the previous
            // behavior, issue #122) put every FIXED field, not just the
            // varying one's own extent, at the wrong DWARF offset.
            DT = buildSchemaDIType(T);
            break;
        default:
            break;
    }
    debugTypes_[&T] = DT;
    return DT;
}

llvm::DIType* CGDebugInfo::fieldOrFallbackDIType(const Type* SemaTy, llvm::Type* LLTy,
                                                  const std::string& DisplayName) {
    if (SemaTy)
        if (auto* DT = debugTypeOfSemaType(*SemaTy)) return DT;
    return DBuilder->createBasicType(DisplayName, Mod.getDataLayout().getTypeSizeInBits(LLTy),
                                      llvm::dwarf::DW_ATE_unsigned);
}

llvm::DIType* CGDebugInfo::buildArrayDIType(const Type& T) {
    if (!Types || !T.IndexType || !T.ElemType) return nullptr;
    llvm::DIType* ElemDT = debugTypeOfSemaType(*T.ElemType);
    if (!ElemDT) return nullptr;
    llvm::Type* ArrTy = Types->llvmTypeOfSemaType(T);
    const auto& DL = Mod.getDataLayout();
    const int64_t lo    = T.IndexType->SubLo;
    const int64_t hi    = T.IndexType->SubHi;
    const int64_t count = (hi - lo + 1 > 0) ? (hi - lo + 1) : 0;
    auto* Sub = DBuilder->getOrCreateSubrange(lo, count);
    return DBuilder->createArrayType(
        DL.getTypeSizeInBits(ArrTy), DL.getABITypeAlign(ArrTy).value() * 8,
        ElemDT, DBuilder->getOrCreateArray({Sub}));
}

llvm::DIType* CGDebugInfo::buildSetDIType(const Type&) {
    // ISO §6.7.2.4: every set, regardless of base type, is the SAME flat
    // PlangMaxSetElements-bit bitmask (SetOps::setTy) -- one bit per
    // ordinal, bit 0 standing for the base type's own origin.  DWARF has
    // no native "Pascal set" primitive, so this shows the real runtime
    // storage honestly: a fixed-size array of bytes, which a debugger
    // prints as a raw hex/byte dump rather than a decoded {lo..hi} view.
    // That is still infinitely better than the variable being invisible,
    // and is exactly the representation this project's own review of this
    // gap called for.
    const uint64_t bytes = static_cast<uint64_t>(PlangMaxSetElements) / 8;
    auto* ByteTy = DBuilder->createBasicType("byte", 8, llvm::dwarf::DW_ATE_unsigned_char);
    auto* Sub = DBuilder->getOrCreateSubrange(0, static_cast<int64_t>(bytes));
    return DBuilder->createArrayType(bytes * 8, 8, ByteTy, DBuilder->getOrCreateArray({Sub}));
}

llvm::DIType* CGDebugInfo::buildComplexDIType(const Type&) {
    // EP §6.4.2.2: { double re, double im }, matching ComplexOps::complexTy()
    // exactly (two adjacent doubles, no padding) -- built directly here
    // rather than through CGTypes/Types since the shape never varies with
    // T and needs no field-offset computation to get right.
    auto* Dbl = DBuilder->createBasicType("real", 64, llvm::dwarf::DW_ATE_float);
    std::vector<llvm::Metadata*> Elems{
        DBuilder->createMemberType(DebugFile, "re", DebugFile, 0, 64, 64, 0,
                                    llvm::DINode::FlagZero, Dbl),
        DBuilder->createMemberType(DebugFile, "im", DebugFile, 0, 64, 64, 64,
                                    llvm::DINode::FlagZero, Dbl),
    };
    return DBuilder->createStructType(DebugFile, "complex", DebugFile, 0, 128, 64,
                                       llvm::DINode::FlagZero, nullptr,
                                       DBuilder->getOrCreateArray(Elems));
}

llvm::DIType* CGDebugInfo::buildStringDIType(int64_t cap) {
    // StringRuntime/CGTypes::strStructType's own shape: { i64 length;
    // [cap x i8] data }, no padding -- built directly (as buildComplexDIType
    // is) rather than through Types, since the shape is a fixed formula in
    // cap alone.  cap<=0 (a bare, capacity-less `string`, or a VarString
    // whose capacity did not resolve) falls back to PlangMaxStringCapacity,
    // same as CGTypes::llvmTypeOfNode's own probe-type fallback -- an
    // honestly-labelled REPRESENTATIVE shape, not this particular
    // instance's real allocation.
    if (cap <= 0) cap = PlangMaxStringCapacity;
    const uint64_t capBits = static_cast<uint64_t>(cap) * 8;
    auto* CharTy = DBuilder->createBasicType("char", 8, llvm::dwarf::DW_ATE_unsigned_char);
    auto* LenTy  = DBuilder->createBasicType("integer", 64, llvm::dwarf::DW_ATE_signed);
    auto* Sub    = DBuilder->getOrCreateSubrange(0, cap);
    auto* DataTy = DBuilder->createArrayType(capBits, 8, CharTy, DBuilder->getOrCreateArray({Sub}));
    std::vector<llvm::Metadata*> Elems{
        DBuilder->createMemberType(DebugFile, "length", DebugFile, 0, 64, 64, 0,
                                    llvm::DINode::FlagZero, LenTy),
        DBuilder->createMemberType(DebugFile, "data", DebugFile, 0, capBits, 8, 64,
                                    llvm::DINode::FlagZero, DataTy),
    };
    return DBuilder->createStructType(DebugFile, "string", DebugFile, 0, 64 + capBits, 64,
                                       llvm::DINode::FlagZero, nullptr,
                                       DBuilder->getOrCreateArray(Elems));
}

llvm::DISubroutineType* CGDebugInfo::buildSubroutineDIType(const Type& T) {
    // ISO §6.6.3.1 procedural/functional parameter type.  Element 0 is the
    // return type (null for a Procedure), matching createSubroutineType's
    // own convention -- the same one emitFunctionStart already relies on
    // for every real DISubprogram.
    std::vector<llvm::Metadata*> Params;
    Params.push_back(T.RetType ? debugTypeOfSemaType(*T.RetType) : nullptr);
    for (const auto& P : T.Params) {
        llvm::DIType* PDT = P.Ty ? debugTypeOfSemaType(*P.Ty) : nullptr;
        // A var parameter is passed by reference at the LLVM level (an
        // address, not a value); wrap in a pointer so the DWARF signature
        // reflects the real calling convention, the same treatment every
        // by-reference parameter elsewhere in this pass gets.
        if (PDT && P.IsVar) PDT = DBuilder->createPointerType(PDT, ptrBits());
        Params.push_back(PDT);
    }
    return DBuilder->createSubroutineType(DBuilder->getOrCreateTypeArray(Params));
}

llvm::DIType* CGDebugInfo::buildRecordDIType(const Type& T) {
    if (!Types) return nullptr;
    const auto& DL = Mod.getDataLayout();

    // EP §6.4.3.4's two runtime-only records (TimeStamp, BindingType) carry
    // no RecordDecl -- see CGTypes::llvmTypeOfSemaTypeImpl's own Record
    // case -- so layoutOfRecord (which walks a RecordTypeNode) has nothing
    // to walk for them; every other record goes through it, since it alone
    // knows which fields share storage under a variant part.
    const CGTypes::RecordLayout* RL = Types->layoutOfRecord(T);

    auto* Fwd = DBuilder->createReplaceableCompositeType(
        llvm::dwarf::DW_TAG_structure_type, T.Name, DebugFile, DebugFile, 0);
    // Cached BEFORE any field is resolved: an ordinary Pascal linked-list
    // shape (`PNode = ^Node; Node = record next: PNode end`) has a field
    // whose type is a pointer back to this same record, and resolving
    // that field recurses into debugTypeOfSemaType(Node) again from
    // inside this very call -- this makes that recursive call see the
    // (temporarily incomplete) forward declaration instead of looping
    // forever.  A DWARF pointer only ever needs SOME DIType for its
    // pointee, not a complete one, so a forward declaration is a
    // legitimate, ordinary answer here, not a workaround.
    debugTypes_[&T] = Fwd;

    std::vector<llvm::Metadata*> Elements;
    uint64_t sizeBits  = 0;
    uint32_t alignBits = 8;

    if (RL) {
        std::vector<std::pair<std::string, CGTypes::FieldPlace>> Fixed, Variant;
        for (const auto& kv : RL->Fields) (kv.second.InVariant ? Variant : Fixed).push_back(kv);
        std::sort(Fixed.begin(), Fixed.end(),
                  [](const auto& a, const auto& b) { return a.second.Index < b.second.Index; });
        std::sort(Variant.begin(), Variant.end(),
                  [](const auto& a, const auto& b) { return a.second.Offset < b.second.Offset; });

        const auto* SL = DL.getStructLayout(RL->Ty);
        for (const auto& [lname, fp] : Fixed) {
            const Type::Field* SF = findSemaField(T, lname);
            const std::string disp = SF ? SF->Name : lname;
            llvm::DIType* MDT = fieldOrFallbackDIType(SF ? SF->Ty.get() : nullptr, fp.Ty, disp);
            Elements.push_back(DBuilder->createMemberType(
                Fwd, disp, DebugFile, 0, DL.getTypeSizeInBits(fp.Ty),
                DL.getABITypeAlign(fp.Ty).value() * 8, SL->getElementOffsetInBits(fp.Index),
                llvm::DINode::FlagZero, MDT));
        }
        if (!Variant.empty()) {
            const unsigned blobIdx = Variant.front().second.Index;
            llvm::Type* blobTy     = RL->Ty->getElementType(blobIdx);
            std::vector<llvm::Metadata*> UElems;
            for (const auto& [lname, fp] : Variant) {
                const Type::Field* SF = findSemaField(T, lname);
                const std::string disp = SF ? SF->Name : lname;
                llvm::DIType* MDT = fieldOrFallbackDIType(SF ? SF->Ty.get() : nullptr, fp.Ty, disp);
                UElems.push_back(DBuilder->createMemberType(
                    Fwd, disp, DebugFile, 0, DL.getTypeSizeInBits(fp.Ty),
                    DL.getABITypeAlign(fp.Ty).value() * 8, fp.Offset * 8,
                    llvm::DINode::FlagZero, MDT));
            }
            // ISO §6.4.3.3: every alternative of a variant part shares this
            // one run of storage.  DWARF has no native discriminated-union
            // primitive; a DW_TAG_union_type nested at the blob's own
            // offset -- one member per field of every alternative,
            // flattened, each at its own byte offset within the blob -- is
            // the standard approximation (the same shape LLVM's own C
            // frontend gives a C union).  A debugger can read
            // `record.$variant.whichever`, but nothing here tells it which
            // alternative is actually live -- that answer lives in the
            // tag field, an ordinary member right alongside this one.
            auto* UnionTy = DBuilder->createUnionType(
                Fwd, T.Name + ".$variant", DebugFile, 0, DL.getTypeSizeInBits(blobTy),
                DL.getABITypeAlign(blobTy).value() * 8, llvm::DINode::FlagZero,
                DBuilder->getOrCreateArray(UElems));
            Elements.push_back(DBuilder->createMemberType(
                Fwd, "$variant", DebugFile, 0, DL.getTypeSizeInBits(blobTy),
                DL.getABITypeAlign(blobTy).value() * 8, SL->getElementOffsetInBits(blobIdx),
                llvm::DINode::FlagZero, UnionTy));
        }
        sizeBits  = DL.getTypeSizeInBits(RL->Ty);
        alignBits = DL.getABITypeAlign(RL->Ty).value() * 8;
    } else {
        // TimeStamp/BindingType: built straight off the flattened field
        // list, one struct element per field in declaration order -- see
        // CGTypes::llvmTypeOfSemaTypeImpl's own fallback, mirrored exactly
        // (same "skip a null Ty" rule) so the indices line up.
        auto* ST = llvm::dyn_cast_or_null<llvm::StructType>(Types->llvmTypeOfSemaType(T));
        if (ST) {
            const auto* SL = DL.getStructLayout(ST);
            unsigned idx = 0;
            for (const auto& F : T.RecordFields) {
                if (!F.Ty) continue;
                llvm::Type* ft = ST->getElementType(idx);
                llvm::DIType* MDT = fieldOrFallbackDIType(F.Ty.get(), ft, F.Name);
                Elements.push_back(DBuilder->createMemberType(
                    Fwd, F.Name, DebugFile, 0, DL.getTypeSizeInBits(ft),
                    DL.getABITypeAlign(ft).value() * 8, SL->getElementOffsetInBits(idx),
                    llvm::DINode::FlagZero, MDT));
                ++idx;
            }
            sizeBits  = DL.getTypeSizeInBits(ST);
            alignBits = DL.getABITypeAlign(ST).value() * 8;
        }
    }

    auto* Full = DBuilder->createStructType(
        DebugFile, T.Name, DebugFile, 0, sizeBits, alignBits, llvm::DINode::FlagZero,
        nullptr, DBuilder->getOrCreateArray(Elements));
    DBuilder->replaceTemporary(llvm::TempDIType(Fwd), Full);
    debugTypes_[&T] = Full;
    return Full;
}

llvm::DIType* CGDebugInfo::buildSchemaDIType(const Type& T) {
    if (!T.SchemaBody) return nullptr;
    llvm::DIType* BodyDT = debugTypeOfSemaType(*T.SchemaBody);
    if (!BodyDT || !Types) return BodyDT;

    // A fixed-extent instance (T.ExtentVaries false) is stored as a plain
    // value with no header at all -- see CGTypes::llvmTypeOfSemaTypeImpl's
    // own SchemaInstance case, which just lowers SchemaBody directly -- so
    // BodyDT is already exact and there is nothing to wrap.
    if (!T.ExtentVaries) return BodyDT;

    llvm::Type* BodyLLTy = Types->llvmTypeOfSemaType(*T.SchemaBody);
    if (!BodyLLTy) return BodyDT;

    // Matches SchemaLayoutEngine::schemaHeaderBytes exactly: one 8-byte
    // word per discriminant, aligned up to the body's own alignment (never
    // below 8).  The body's ABI alignment does not depend on how many
    // elements a varying array field really has (only its element type
    // does), so computing it off the PROBE body here agrees with the real,
    // per-instance header every run-time object actually carries.
    const auto& DL = Mod.getDataLayout();
    const uint64_t bodyAlign = std::max<uint64_t>(8, DL.getABITypeAlign(BodyLLTy).value());
    const uint64_t rawHdr    = T.SchemaDiscs.size() * 8;
    const uint64_t hdrBytes  = (rawHdr + bodyAlign - 1) / bodyAlign * bodyAlign;

    auto* Fwd = DBuilder->createReplaceableCompositeType(
        llvm::dwarf::DW_TAG_structure_type, T.Name, DebugFile, DebugFile, 0);
    debugTypes_[&T] = Fwd;

    // One named member per discriminant, in the header -- readable by name
    // (`print q^.n`) exactly like an ordinary field, since that is what the
    // header really holds -- followed by the body itself as one unnamed
    // member just past the header.  DW_TAG_member with no DW_AT_name is the
    // ordinary DWARF "anonymous struct/union member" shape (the same one a
    // C `struct { struct { int x; }; }` anonymous nested struct gets):
    // gdb/lldb read straight through it, so `q^.a`/`q^.k` still resolve to
    // the body's own fields without one more `.` in the way -- only the
    // OFFSET they are read at changed, not the access spelling.
    std::vector<llvm::Metadata*> Elements;
    auto* DiscDT = DBuilder->createBasicType("integer", 64, llvm::dwarf::DW_ATE_signed);
    for (size_t i = 0; i < T.SchemaDiscs.size(); ++i) {
        Elements.push_back(DBuilder->createMemberType(
            Fwd, T.SchemaDiscs[i].Name, DebugFile, 0, 64, 64,
            i * 64, llvm::DINode::FlagZero, DiscDT));
    }
    // BodyDT's own field offsets are still the PROBE's (issue #122's other
    // half, left as-is here): a varying-extent field's real size can only
    // be known from the discriminant this object carries at run time, which
    // a DIType -- one static description shared by every instance of T, not
    // rebuilt per allocation -- has no way to read.  Wrapping the header
    // around it fixes the header itself and every field AT OR BEFORE the
    // varying one exactly (nothing past this point in the body depends on
    // any discriminant); a FIXED field written AFTER a varying one in the
    // same record still inherits the varying field's own probe-approximated
    // extent for its own offset, same as the varying field's own extent
    // already did before this fix. issue #130: this cannot be closed with a
    // genuine per-object DWARF location expression the way this comment
    // used to say -- LLVM's DWARF emitter has no implementation of a
    // computed member ADDRESS at all (confirmed directly from
    // DwarfUnit.cpp's constructMemberDIE: an expression-typed offset always
    // becomes DW_AT_data_bit_offset, a bitfield-only attribute; confirmed
    // empirically too, gdb 17.2 crashes trying to print a member built that
    // way). recordSchemaLayoutForScript below is the real fix for this gap
    // instead -- a sidecar a gdb pretty-printer reads separately, computing
    // the correct value from live memory, bypassing DWARF's limitation
    // entirely rather than fighting it.
    Elements.push_back(DBuilder->createMemberType(
        Fwd, "", DebugFile, 0, DL.getTypeSizeInBits(BodyLLTy),
        DL.getABITypeAlign(BodyLLTy).value() * 8, hdrBytes * 8,
        llvm::DINode::FlagZero, BodyDT));

    const uint64_t sizeBits = hdrBytes * 8 + DL.getTypeSizeInBits(BodyLLTy);
    auto* Full = DBuilder->createStructType(
        DebugFile, T.Name, DebugFile, 0, sizeBits, bodyAlign * 8,
        llvm::DINode::FlagZero, nullptr, DBuilder->getOrCreateArray(Elements));
    DBuilder->replaceTemporary(llvm::TempDIType(Fwd), Full);
    debugTypes_[&T] = Full;

    if (const TypeNode* bodyNode = SchemaTypes ? SchemaTypes->schemaBodyNodeOf(T) : nullptr) {
        if (const auto* rt = llvm::dyn_cast<RecordTypeNode>(bodyNode))
            recordSchemaLayoutForScript(T, *rt, hdrBytes);
    }
    return Full;
}

void CGDebugInfo::jsonEncodeExtentForm(const ExtentForm& F, std::string& Out) {
    // A flat JSON array, op name first: ["const",1], ["disc",0],
    // ["add",<left>,<right>], ["neg",<arg>] -- plang_schema_printers.py's
    // eval_form walks this directly, no separate schema needed on that side
    // beyond "first element is the op name".
    switch (F.Kind) {
        case ExtentForm::Op::Const: Out += "[\"const\"," + std::to_string(F.Value) + "]"; return;
        case ExtentForm::Op::Disc:  Out += "[\"disc\","  + std::to_string(F.Value) + "]"; return;
        case ExtentForm::Op::Neg:
            Out += "[\"neg\",";
            if (!F.Args.empty()) jsonEncodeExtentForm(F.Args[0], Out); else Out += "[\"const\",0]";
            Out += "]";
            return;
        default: {
            const char* op = F.Kind == ExtentForm::Op::Add ? "add"
                            : F.Kind == ExtentForm::Op::Sub ? "sub"
                            : F.Kind == ExtentForm::Op::Mul ? "mul"
                            : F.Kind == ExtentForm::Op::Div ? "div"
                            : F.Kind == ExtentForm::Op::Mod ? "mod"
                            : F.Kind == ExtentForm::Op::Pow ? "pow"
                            : "const";
            Out += std::string("[\"") + op + "\",";
            if (F.Args.size() == 2) {
                jsonEncodeExtentForm(F.Args[0], Out);
                Out += ",";
                jsonEncodeExtentForm(F.Args[1], Out);
            } else {
                Out += "[\"const\",0],[\"const\",0]";
            }
            Out += "]";
            return;
        }
    }
}

std::optional<uint64_t> CGDebugInfo::computeSchemaFingerprint(const RecordTypeNode& rt,
                                                                size_t numDiscs) {
    if (!Types) return std::nullopt;
    if (rt.Variant) return std::nullopt;
    const auto& DL = Mod.getDataLayout();
    // Canonical string: discriminant count, then every body field's own
    // (name, kind, byte size) in DWARF declaration order -- deliberately
    // the SAME quantities buildRecordDIType feeds a member's own DIType
    // (DL.getTypeSizeInBits(fp.Ty)/8 is exactly the byte size that becomes
    // that member's DW_AT_byte_size), so plang_schema_printers.py can
    // reproduce this identical hash purely from a live gdb.Type's own
    // member list -- no need to also read the sidecar just to find out
    // which sidecar entry to trust.
    std::string canon = "D" + std::to_string(numDiscs) + ";";
    for (const auto& fd : rt.Fields) {
        auto* at = llvm::dyn_cast<ArrayTypeNode>(fd.Type.get());
        if (at && (!at->ExtentLow || !at->ExtentHigh)) return std::nullopt;
        if (!at && (fd.Type->ExtentLow || fd.Type->ExtentHigh)) return std::nullopt;
        llvm::Type* FieldLLTy = Types->llvmTypeOfNode(*fd.Type);
        if (!FieldLLTy) return std::nullopt;
        const uint64_t sizeBytes = DL.getTypeSizeInBits(FieldLLTy) / 8;
        const char* kind = at ? "array" : "scalar";
        for (const auto& name : fd.Names) {
            canon += name;
            canon += ':';
            canon += kind;
            canon += ':';
            canon += std::to_string(sizeBytes);
            canon += ';';
        }
    }
    return fnv1a64(canon);
}

namespace {
/// issue #144: plang_schema_printers.py used to read every non-array field
/// as a raw signed integer regardless of its real plang type -- a real
/// field showed its IEEE-754 bit pattern, a set field an arbitrary integer,
/// and a 16-byte field (complex) crashed the printer outright.  This tags
/// each scalar field with the shape the Python side needs to format it
/// correctly; a kind this compiler-side switch does not know how to name
/// specially just gets "integer", which is the same raw-integer read the
/// script already did for it and stays correct for anything ordinal-shaped
/// (Integer, Subrange, Enum).
const char* schemaScriptTypeKind(const Type* T) {
    if (!T) return "integer";
    switch (T->Kind) {
        case TypeKind::Real:    return "real";
        case TypeKind::Complex: return "complex";
        case TypeKind::Set:     return "set";
        case TypeKind::Boolean: return "boolean";
        case TypeKind::Char:    return "char";
        case TypeKind::Pointer: return "pointer";
        default:                return "integer";
    }
}

/// Issue #416: a field's own type -- or an array field's element type --
/// might not itself be a SchemaTypeNode (the shape issue #143's guards
/// already catch, right where this is called) but might instead be an
/// ORDINARY (non-schema) record that recursively contains a member whose
/// extent was probed against the ENCLOSING schema's own discriminant.
/// Sema's ProbeDiscNames_ propagates into every nested resolveTypeImpl call
/// reached while resolving the enclosing schema body (see SemaType.cpp,
/// guarded on !ProbeDiscNames_.empty()), so a StringTypeNode/ArrayTypeNode/
/// SubrangeTypeNode buried inside a nested record's own fields gets its
/// ExtentLow/ExtentHigh set exactly like a top-level varying field would --
/// but recordSchemaLayoutForScript's loop only ever looks at fd.Type (or,
/// for an array field, at->Element) ONE level deep, so a nested record hides
/// that from both of its guards and falls through to the generic scalar
/// branch, recording the nested record's own compile-time-PROBE size (every
/// discriminant pinned to 1 -- see resolveNamed's own comment on the probe
/// binding) as if it were that field's real, run-time-constant size.
///
/// Walks a nested record's own fields (and, transitively, any record nested
/// further inside those) for exactly the shapes the caller already bails on
/// at the top level: a schema-typed member, a variant part, or a member
/// whose own type-node carries a probe-derived ExtentLow/ExtentHigh.
/// Present on a top-level array field is normal and required -- the sidecar
/// has a dedicated slot for its low/high forms -- but nothing here gives a
/// NESTED member's own extent form anywhere to go, so its presence at any
/// depth below the top means this whole subtree's size is not the
/// compile-time constant the generic scalar branch would otherwise assume,
/// and the containing schema's recording must bail the same way it already
/// does for a direct schema-typed field.
bool recordTypeHasOutOfScopeMember(const RecordTypeNode& rt) {
    if (rt.Variant) return true;
    for (const auto& fd : rt.Fields) {
        auto* at = llvm::dyn_cast<ArrayTypeNode>(fd.Type.get());
        if (llvm::isa<SchemaTypeNode>(fd.Type.get())) return true;
        if (at && llvm::isa<SchemaTypeNode>(at->Element.get())) return true;
        if (fd.Type->ExtentLow || fd.Type->ExtentHigh) return true;
        const TypeNode* elemOrField = at ? at->Element.get() : fd.Type.get();
        if (auto* nestedRt = llvm::dyn_cast<RecordTypeNode>(elemOrField))
            if (recordTypeHasOutOfScopeMember(*nestedRt)) return true;
    }
    return false;
}
} // namespace

void CGDebugInfo::recordSchemaLayoutForScript(const Type& T, const RecordTypeNode& rt,
                                               uint64_t hdrBytes) {
    if (!Types) return;
    // Nothing here handles a variant part or an array whose bound isn't a
    // closed form (shouldn't happen for a field reached through a schema
    // body -- see TypeNode's own ExtentLow/ExtentHigh comment -- but this
    // stays defensive rather than emitting a partial, misleading entry):
    // skip recording, plang_schema_printers.py's own fallback (print the
    // ordinary DWARF-derived value) is exactly as good as not having run
    // this pass at all.
    if (rt.Variant) return;

    // Issue #140: keyed on (T.Name, structural fingerprint), not on the
    // bare name alone -- two schemas that share a name AND a fingerprint
    // (the common case: the same schema type recorded from several probe
    // instantiation sites) collapse into one entry, exactly as the old
    // name-only key did; two that share a name but NOT a fingerprint (a
    // genuine collision -- two distinct schema types that happen to be
    // spelled the same) get a second, distinct entry instead of the first
    // silently winning forever.
    const std::optional<uint64_t> fpOpt = computeSchemaFingerprint(rt, T.SchemaDiscs.size());
    if (!fpOpt) return;
    const uint64_t fp = *fpOpt;

    // Looked up without inserting: any of this function's several bail-out
    // returns below (a nested-schema field, a variant part reached late,
    // an unlowerable field type, ...) must NOT leave a stray empty
    // "name":[] entry behind for a schema that's never actually recorded --
    // plang_schema_printers.py's own SIDECAR-NOT-style tests treat the
    // bare presence of a name as "this schema WAS recorded", so an empty
    // placeholder is as wrong as a real-but-incorrect one. Only the final,
    // successful emplace_back below touches schemaScriptEntries_ itself.
    auto existingIt = schemaScriptEntries_.find(T.Name);
    const bool hadPriorVariants =
        existingIt != schemaScriptEntries_.end() && !existingIt->second.empty();
    if (existingIt != schemaScriptEntries_.end())
        for (const auto& variant : existingIt->second)
            if (variant.first == fp) return; // already recorded -- harmless merge
    if (hadPriorVariants && warnedSchemaNames_.insert(T.Name).second) {
        std::cerr << "plang: warning: schema type '" << T.Name
                   << "' is declared more than once with incompatible field "
                      "layouts (same name, different body) -- the -g debug "
                      "sidecar now records each layout under its own "
                      "structural fingerprint so the gdb pretty-printer can "
                      "tell them apart, but this ambiguity is still worth "
                      "fixing in the source.\n";
    }
    const auto& DL = Mod.getDataLayout();

    std::string J = "{\"fp\":\"" + toHex16(fp) + "\",";
    J += "\"discs\":[";
    for (size_t i = 0; i < T.SchemaDiscs.size(); ++i) {
        if (i) J += ",";
        J += "\"" + T.SchemaDiscs[i].Name + "\"";
    }
    J += "],\"hdrBytes\":" + std::to_string(hdrBytes) + ",\"fields\":[";

    bool firstField = true;
    for (const auto& fd : rt.Fields) {
        auto* at = llvm::dyn_cast<ArrayTypeNode>(fd.Type.get());
        // A field whose type is itself another schema instantiation (or an
        // array of one) never carries ExtentLow/ExtentHigh -- those belong
        // only to a string capacity/subrange/array-bound denoter (see
        // TypeNode::ExtentLow's own comment) -- so neither guard below would
        // fire for it, and it would otherwise fall through to the generic
        // scalar branch and record that field's compile-time-probe size as
        // if it were the field's real, run-time-varying size.  Bail the
        // WHOLE containing schema's recording instead, same as the variant-
        // part and varying-non-array-field cases just below: a partial,
        // silently-wrong entry is worse than none (plang_schema_printers.py's
        // own fallback -- plain DWARF -- is exactly as good as not having
        // run this pass at all).
        if (llvm::isa<SchemaTypeNode>(fd.Type.get())) return;
        if (at && llvm::isa<SchemaTypeNode>(at->Element.get())) return;
        // Issue #416: neither guard above fires for an ORDINARY (non-schema)
        // record field/element that itself, recursively, contains a member
        // probed against this schema's own discriminant -- see
        // recordTypeHasOutOfScopeMember's own comment for exactly why that
        // shape is just as out of scope as the two direct checks above it.
        if (auto* nestedRt = llvm::dyn_cast<RecordTypeNode>(at ? at->Element.get() : fd.Type.get()))
            if (recordTypeHasOutOfScopeMember(*nestedRt)) return;
        if (at && (!at->ExtentLow || !at->ExtentHigh)) return; // see comment above
        if (!at && (fd.Type->ExtentLow || fd.Type->ExtentHigh)) return; // varying non-array field, out of scope

        llvm::Type* FieldLLTy = Types->llvmTypeOfNode(*fd.Type);
        if (!FieldLLTy) return;
        const uint64_t align = DL.getABITypeAlign(FieldLLTy).value();

        std::string fieldJson = "{\"names\":[";
        for (size_t i = 0; i < fd.Names.size(); ++i) {
            if (i) fieldJson += ",";
            fieldJson += "\"" + fd.Names[i] + "\"";
        }
        fieldJson += "],";
        if (at) {
            llvm::Type* ElemLLTy = Types->llvmTypeOfNode(*at->Element);
            if (!ElemLLTy) return;
            const uint64_t elemSize  = DL.getTypeAllocSize(ElemLLTy);
            const uint64_t elemAlign = DL.getABITypeAlign(ElemLLTy).value();
            fieldJson += "\"kind\":\"array\",\"elemSizeBytes\":" + std::to_string(elemSize)
                       + ",\"elemAlignBytes\":" + std::to_string(elemAlign)
                       + ",\"alignBytes\":" + std::to_string(align) + ",\"low\":";
            jsonEncodeExtentForm(*at->ExtentLow, fieldJson);
            fieldJson += ",\"high\":";
            jsonEncodeExtentForm(*at->ExtentHigh, fieldJson);
            fieldJson += "}";
        } else {
            const uint64_t sizeBytes = DL.getTypeAllocSize(FieldLLTy);
            const Type* FieldTy = fd.Type->ResolvedType.get();
            const char* typeKind = schemaScriptTypeKind(FieldTy);
            fieldJson += "\"kind\":\"scalar\",\"sizeBytes\":" + std::to_string(sizeBytes)
                       + ",\"alignBytes\":" + std::to_string(align)
                       + ",\"typeKind\":\"" + typeKind + "\"";
            // A set's members are a bitmask over its base type's ordinals, bit
            // 0 standing for the base type's own origin rather than for
            // ordinal 0 (see SetOps::setBitIndex / setBaseOffset) -- the
            // script needs that same offset to decode the bits back into the
            // ordinals the program actually put in, not ordinals shifted by
            // whatever the base type's lower bound happens to be.
            if (FieldTy && FieldTy->Kind == TypeKind::Set) {
                const int64_t setBase = FieldTy->ElemType ? setBaseOffset(*FieldTy->ElemType) : 0;
                fieldJson += ",\"setBase\":" + std::to_string(setBase);
            }
            fieldJson += "}";
        }

        if (!firstField) J += ",";
        firstField = false;
        J += fieldJson;
    }
    J += "]}";
    schemaScriptEntries_[T.Name].emplace_back(fp, std::move(J));
}

bool CGDebugInfo::writeSchemaDebugScript() {
    if (schemaScriptEntries_.empty() || !SrcMgr) return true;
    const std::filesystem::path SidecarPath = schemaSidecarPath();
    if (SidecarPath.empty()) return true;

    // Issue #140: each name now maps to a JSON ARRAY of variant bodies (one
    // per distinct structural fingerprint seen under that name), not a
    // single object -- see schemaScriptEntries_'s own comment. The common,
    // non-colliding case is a one-element array; plang_schema_printers.py
    // only needs to disambiguate at all once it sees more than one.
    //
    // Issue #141: "buildId" ties this specific sidecar to this specific
    // compile -- see SchemaBuildId_'s own comment for how the printer uses
    // it (compared against the matching token embedded in DW_AT_producer).
    std::string J = "{\"buildId\":\"" + toHex16(SchemaBuildId_) + "\",\"schemas\":{";
    bool first = true;
    for (const auto& [name, variants] : schemaScriptEntries_) {
        if (!first) J += ",";
        first = false;
        J += "\"" + name + "\":[";
        bool firstVariant = true;
        for (const auto& [fp, body] : variants) {
            if (!firstVariant) J += ",";
            firstVariant = false;
            J += body;
        }
        J += "]";
    }
    J += "}}";

    // Issue #396: this used to be "if (Out) Out << J;" -- an open failure
    // (a full disk, a permissions error) was silently ignored, and a write
    // failure after a successful open was never checked at all, so a
    // failure here always reported the same clean compile as success while
    // a later gdb session, loading plang_schema_printers.py, either fell
    // back to raw values or misbehaved with no way to tell the sidecar
    // write is what actually failed.  Same "fail loud, don't vanish
    // silently" contract issues #246 (Frontend.cpp's own -o output writer)
    // and #397 (its .pmi writer) already gave the compiler's other output
    // writers.
    std::ofstream Out(SidecarPath);
    if (!Out) {
        std::cerr << "plang: cannot open -g schema sidecar file '"
                   << SidecarPath.string() << "'\n";
        return false;
    }
    Out << J;
    // A write can fail only after the open already succeeded (ENOSPC is the
    // reproducible case) -- std::ofstream surfaces that solely through its
    // own failbit, which a write under the internal buffer size does not
    // set until the stream is actually flushed.  close() first, then check,
    // exactly like reportIfWriteFailed's own comment in Frontend.cpp
    // (issue #246) documents this same gotcha for.
    Out.close();
    if (!Out) {
        std::cerr << "plang: error writing -g schema sidecar file '"
                   << SidecarPath.string() << "'\n";
        return false;
    }
    return true;
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
                                llvm::Value* ptr, llvm::Value* debugIndirectPtr,
                                bool suppress) {
    // Issue #142: the caller is about to (or already has) built this
    // variable's real debug declaration a different way -- see
    // declareSchemaParamRef's own comment for why a schema var/value
    // parameter is the one case that needs to skip the ordinary path below
    // rather than go through it.
    if (suppress) return;
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

void CGDebugInfo::declareProcParam(const std::string& name, const ProcedureTypeNode* PT,
                                    llvm::Value* ptr) {
    if (!DBuilder || !PT || !PT->ResolvedType || !CurScope || !B.GetInsertBlock()) return;
    llvm::DISubroutineType* SubTy = buildSubroutineDIType(*PT->ResolvedType);
    const unsigned PB = ptrBits(); // the pair's own two fields are both pointers
    auto* CodeTy  = DBuilder->createPointerType(SubTy, PB);
    // The frame's own pointee varies per call site (whatever static link the
    // procedure passed happens to need) and nothing here knows its shape --
    // an untyped pointer, the same honest answer a C `void*` frame would
    // get, exactly like a null pointee elsewhere in this file.
    auto* FrameTy = DBuilder->createPointerType(nullptr, PB);
    std::vector<llvm::Metadata*> Elems{
        DBuilder->createMemberType(DebugFile, "code", DebugFile, 0, PB, PB, 0,
                                    llvm::DINode::FlagZero, CodeTy),
        DBuilder->createMemberType(DebugFile, "frame", DebugFile, 0, PB, PB, PB,
                                    llvm::DINode::FlagZero, FrameTy),
    };
    auto* PairTy = DBuilder->createStructType(
        DebugFile, "procparam", DebugFile, 0, 2 * PB, PB, llvm::DINode::FlagZero,
        nullptr, DBuilder->getOrCreateArray(Elems));
    const unsigned line = SrcMgr ? SrcMgr->getPresumedLoc(PT->Loc).Line : 0;
    auto* DV = DBuilder->createAutoVariable(CurScope, name, DebugFile, line, PairTy);
    DBuilder->insertDeclare(ptr, DV, DBuilder->createExpression(),
                             llvm::DILocation::get(Ctx, line, 0, CurScope), B.GetInsertBlock());
}

void CGDebugInfo::declareSchemaParamRef(const std::string& name, const TypeNode* typeNode,
                                         const std::vector<llvm::Value*>& discs,
                                         llvm::Value* bodyPtr) {
    if (!DBuilder || !typeNode || !typeNode->ResolvedType || !CurScope
            || !B.GetInsertBlock() || !Types)
        return;
    const Type& T = *typeNode->ResolvedType;
    if (!T.SchemaBody) return;
    llvm::DIType* BodyDT = debugTypeOfSemaType(*T.SchemaBody);
    llvm::Type*   BodyLLTy = Types->llvmTypeOfSemaType(*T.SchemaBody);
    if (!BodyDT || !BodyLLTy) return;

    // Same formula as buildSchemaDIType's own hdrBytes -- the SIZE of the
    // header this schema's own discriminants take when they DO sit in real
    // memory (SchemaAccess::emitNewSchema's own allocation).  Here it only
    // sizes the debug-only shadow block below; nothing about a parameter's
    // real storage is aligned by it.
    const auto& DL = Mod.getDataLayout();
    const uint64_t bodyAlign = std::max<uint64_t>(8, DL.getABITypeAlign(BodyLLTy).value());
    const uint64_t rawHdr    = T.SchemaDiscs.size() * 8;
    const uint64_t hdrBytes  = (rawHdr + bodyAlign - 1) / bodyAlign * bodyAlign;

    llvm::Type* I8Ty  = llvm::Type::getInt8Ty(Ctx);
    llvm::Type* I64Ty = llvm::Type::getInt64Ty(Ctx);

    // The shadow block: [hdrBytes worth of discriminant words][one pointer
    // to the real body].  Built fresh every time this is reached (once per
    // activation of a procedure with a schema var/value parameter) --
    // exactly the cost declareProcParam's own closure-pair storage already
    // pays for the same reason, and not on any hot path RangeGuards or the
    // like would care about.
    const unsigned ptrBytes = ptrBits() / 8;
    auto* shadow = B.CreateAlloca(I8Ty,
        llvm::ConstantInt::get(I64Ty, hdrBytes + ptrBytes), name + ".dbgref");
    for (size_t i = 0; i < discs.size(); ++i) {
        auto* slot = B.CreateGEP(I8Ty, shadow,
            {llvm::ConstantInt::get(I64Ty, static_cast<uint64_t>(i) * 8)});
        B.CreateStore(discs[i], slot);
    }
    auto* bodySlot = B.CreateGEP(I8Ty, shadow,
        {llvm::ConstantInt::get(I64Ty, hdrBytes)});
    B.CreateStore(bodyPtr, bodySlot);

    // The DIType: discriminant members exactly like buildSchemaDIType's own
    // header, followed by a POINTER-typed member (not BodyDT inline) --
    // this is the one structural difference from a direct object's wrapped
    // struct, and it is exactly what makes the shadow block's layout
    // correct: the body is somewhere else, reached through one indirection,
    // never assumed to sit right after the header the way a real
    // in-memory-adjacent object's does. Tagged "<name>.ref", not T.Name --
    // plang_schema_printers.py tells the two shapes apart by that suffix
    // alone, no extra sidecar field needed (see its own comment).
    std::vector<llvm::Metadata*> Elements;
    auto* DiscDT = DBuilder->createBasicType("integer", 64, llvm::dwarf::DW_ATE_signed);
    for (size_t i = 0; i < T.SchemaDiscs.size(); ++i) {
        Elements.push_back(DBuilder->createMemberType(
            DebugFile, T.SchemaDiscs[i].Name, DebugFile, 0, 64, 64,
            i * 64, llvm::DINode::FlagZero, DiscDT));
    }
    auto* BodyPtrTy = DBuilder->createPointerType(BodyDT, ptrBits());
    Elements.push_back(DBuilder->createMemberType(
        DebugFile, "", DebugFile, 0, ptrBits(), ptrBits(), hdrBytes * 8,
        llvm::DINode::FlagZero, BodyPtrTy));

    const uint64_t sizeBits = hdrBytes * 8 + ptrBits();
    auto* RefTy = DBuilder->createStructType(
        DebugFile, T.Name + ".ref", DebugFile, 0, sizeBits, 64,
        llvm::DINode::FlagZero, nullptr, DBuilder->getOrCreateArray(Elements));

    // Recorded under T.Name itself (not the ".ref"-suffixed tag above) --
    // this is the SAME field-layout data a direct object of this schema
    // would record, since the body's own shape does not depend on how its
    // caller reached it; a program whose only use of this schema name is
    // through a var parameter would otherwise never populate the sidecar
    // at all, since buildSchemaDIType (the only other recorder) is never
    // reached for it.
    if (const TypeNode* bodyNode = SchemaTypes ? SchemaTypes->schemaBodyNodeOf(T) : nullptr) {
        if (const auto* rt = llvm::dyn_cast<RecordTypeNode>(bodyNode))
            recordSchemaLayoutForScript(T, *rt, hdrBytes);
    }

    const unsigned line = SrcMgr ? SrcMgr->getPresumedLoc(typeNode->Loc).Line : 0;
    auto* DV = DBuilder->createAutoVariable(CurScope, name, DebugFile, line, RefTy);
    DBuilder->insertDeclare(shadow, DV, DBuilder->createExpression(),
                             llvm::DILocation::get(Ctx, line, 0, CurScope), B.GetInsertBlock());
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
