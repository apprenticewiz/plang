// CGTypes.h — static type-lowering/layout: syntax and semantic type
// denoters to llvm::Type*, and the record/variant layout machinery both
// route through.
//
// The differential-oracle counterpart to SchemaLayoutEngine's run-time
// layout walk: this is the STATIC half of the "three independent
// computations of one fact" invariant (this file, SchemaLayoutEngine's
// walk, Sema::byteSizeOf) -- they cross-check each other explicitly and
// must never be merged into one implementation. checkSizeAgreement/
// checkSchemaFieldOffsetAgreement/checkFieldOffsetAgreement are exactly
// those cross-checks; they call into SchemaLayout (held by reference,
// already its own class since Wave 2) through its public rt* surface only,
// never its private state.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"

#include "ComplexOps.h"
#include "SchemaLayoutEngine.h"
#include "SetOps.h"

#include "plang/Sema/Type.h"

namespace llvm {
class Module;
class Value;
}

namespace plang {
struct ArrayTypeNode;
struct FieldDecl;
struct RecordTypeNode;
struct TypeNode;
struct VariantCase;
struct VariantPart;
struct LangOptions;
}

class CGTypes {
public:
    CGTypes(llvm::LLVMContext& Ctx, llvm::Module& Mod,
            const plang::LangOptions& Opts, SchemaLayoutEngine& SchemaLayout,
            ComplexOps& Complex, SetOps& Sets,
            std::unordered_map<std::string, const plang::TypeNode*>& TypeAliases,
            std::unordered_map<std::string, llvm::Value*>& Consts,
            llvm::IntegerType* I1Ty, llvm::IntegerType* I8Ty,
            llvm::IntegerType* I32Ty, llvm::IntegerType* I64Ty,
            llvm::Type* DblTy, llvm::Type* FltTy, llvm::PointerType* PtrTy)
        : Ctx(Ctx), Mod(Mod), Opts(Opts), SchemaLayout(SchemaLayout),
          Complex(Complex), Sets(Sets), TypeAliases(TypeAliases),
          Consts(Consts), i1Ty(I1Ty), i8Ty(I8Ty), i32Ty(I32Ty), i64Ty(I64Ty),
          dblTy(DblTy), fltTy(FltTy), ptrTy(PtrTy) {}

    /// Where one field of a record lives.  A field of the fixed part is an
    /// element of the struct.  ISO §6.4.3.3 has at most one variant active
    /// at a time, so the alternatives share a single run of storage: their
    /// fields are all placed in the one element standing for the variant
    /// part, each at its own byte offset within it.
    struct FieldPlace {
        unsigned    Index{0};        ///< element index in the struct
        llvm::Type* Ty{nullptr};     ///< the field's own type
        bool        InVariant{false};
        uint64_t    Offset{0};       ///< byte offset into the variant element
    };
    struct RecordLayout {
        llvm::StructType* Ty{nullptr};
        /// Field name, folded to lower case, to where it lives.
        std::map<std::string, FieldPlace> Fields;
    };

    /// Binds a schema instance's discriminants into Consts/schemaCtx_ for
    /// the scope's lifetime, restoring both on destruction -- so a denoter
    /// read while a schema-typed value is in scope resolves its
    /// discriminant names, and the memo key in layoutOf sees which
    /// instantiation is in force.
    class SchemaBindingScope {
    public:
        SchemaBindingScope(CGTypes& Owner, const plang::Type& T);
        ~SchemaBindingScope();
        SchemaBindingScope(const SchemaBindingScope&)            = delete;
        SchemaBindingScope& operator=(const SchemaBindingScope&) = delete;
    private:
        CGTypes& Owner;
        std::string SavedCtx;
        std::vector<std::pair<std::string, std::optional<llvm::Value*>>> Saved;
    };

    llvm::StructType* strStructType(int64_t cap);
    llvm::Type*        llvmTypeOfName(const std::string& name);
    llvm::Type*        variantBlobType(uint64_t size, uint64_t align);
    llvm::Type*        semaFieldType(const plang::Type* semaRec, const std::string& nm);
    uint64_t           layoutVariantCase(const plang::VariantCase& vc, RecordLayout& L,
                                          bool packed, unsigned blobIdx, uint64_t base,
                                          const plang::Type* semaRec);
    void               layoutVariantPart(const plang::VariantPart& vp, RecordLayout& L,
                                          bool packed, std::vector<llvm::Type*>& elems,
                                          const plang::Type* semaRec);
    const RecordLayout* layoutOfRecord(const plang::Type& T);
    const RecordLayout& layoutOf(const plang::RecordTypeNode& rt,
                                  const plang::Type* semaRec = nullptr);
    llvm::StructType*   structTypeFor(const plang::RecordTypeNode& rt);
    llvm::Type*         llvmTypeOfNode(const plang::TypeNode& node);
    llvm::Type*         llvmTypeOfNodeViaSema(const plang::TypeNode& node, const std::string& what);
    std::optional<std::pair<int64_t, int64_t>> arrayIndexRange(const plang::ArrayTypeNode& n) const;
    static bool         canLowerSemaType(const plang::Type& T);
    llvm::Type*         llvmTypeOf(const plang::TypeNode* denoter, const plang::Type* resolved);
    [[nodiscard]] llvm::Type* ordinalTyOf(const plang::TypeNode& node);
    void                checkSizeAgreement(const plang::Type& T, llvm::Type* Built);
    void                checkSchemaFieldOffsetAgreement(const plang::Type& T, llvm::Type* Built);
    void                checkFieldOffsetAgreement(const plang::Type& T, llvm::Type* Built);
    llvm::Type*         llvmTypeOfSemaType(const plang::Type& T);
    llvm::Type*         llvmTypeOfSemaTypeImpl(const plang::Type& T);

    /// ISO §6.5: storage laid out to match the runtime's PascalFile
    /// (plang/Basic/PascalFileLayout.h), checked field-by-field the first
    /// time the type is wanted.
    llvm::StructType* fileStructType();
    /// EP §6.4.3.4, checked against PlangTimeStamp (RequiredRecordLayouts.h).
    llvm::StructType* timestampStructType();
    /// EP §6.4.3.4, checked against PlangBindingType (RequiredRecordLayouts.h).
    llvm::StructType* bindingStructType();

private:
    llvm::LLVMContext& Ctx;
    llvm::Module& Mod;
    const plang::LangOptions& Opts;
    SchemaLayoutEngine& SchemaLayout;
    ComplexOps& Complex;
    SetOps& Sets;
    std::unordered_map<std::string, const plang::TypeNode*>& TypeAliases;
    std::unordered_map<std::string, llvm::Value*>& Consts;
    // Lowercase, unlike this class's other members: fileStructType/
    // timestampStructType/bindingStructType expand PLANG_FILE_FIELDS/
    // PLANG_TIMESTAMP_FIELDS/PLANG_BINDINGTYPE_FIELDS
    // (plang/Basic/PascalFileLayout.h, RequiredRecordLayouts.h), macros
    // shared with the runtime's own build and written expecting exactly
    // these names in scope.
    llvm::IntegerType* i1Ty;
    llvm::IntegerType* i8Ty;
    llvm::IntegerType* i32Ty;
    llvm::IntegerType* i64Ty;
    llvm::Type*        dblTy;
    /// Turbo `Single`'s LLVM lowering (llvm::Type::getFloatTy); see
    /// llvmTypeOfSemaTypeImpl's Real case.
    llvm::Type*        fltTy;
    llvm::PointerType* ptrTy;

    std::map<std::string, llvm::StructType*> structTypes_;
    /// Keyed by declaration, not by struct type -- see the definition's own
    /// comment (moved verbatim from Codegen::Impl) for why the discriminant
    /// context and the Sema record both belong in the key.
    std::map<std::tuple<const plang::RecordTypeNode*, std::string, const plang::Type*>,
             RecordLayout>
        recordLayouts_;
    /// The discriminants a layout is currently being worked out under,
    /// written as `name=value` pairs.  Empty everywhere outside a schema
    /// body.
    std::string schemaCtx_;
    std::map<int64_t, llvm::StructType*> strStructTypes_;
    llvm::StructType* fileStructTy_{nullptr};
    llvm::StructType* timestampTy_{nullptr};
    /// Name (lowercased) -> field, memoized per Sema record.  semaFieldType
    /// used to re-scan the whole RecordFields list for every call, and it is
    /// called once per field name while laying a record out -- an O(n) scan
    /// times n fields made the whole pass O(n^2).  This index turns each
    /// lookup after the first into O(1), without changing which field (or
    /// none) is found: it is filled by the same linear walk semaFieldType
    /// used to do inline, just done once and cached instead of once per call.
    std::unordered_map<const plang::Type*,
                        std::unordered_map<std::string, const plang::Type::Field*>>
        semaFieldIndex_;
    const std::unordered_map<std::string, const plang::Type::Field*>&
        semaFieldIndexFor(const plang::Type* semaRec);
};
