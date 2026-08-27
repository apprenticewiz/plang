// SchemaAccess.h — EP §6.4.7 schema value/access-path resolution.
//
// A schema is a family of types indexed by a tuple of discriminants.  When
// the discriminants are written out (`vec(4)`) the type is ordinary and
// lowers like any other.  This unit covers the case where they are not: a
// formal parameter declared `v: vec`, and `p^` where `p: ^vec`.  In both,
// the discriminants are only known at run time, so they travel with the
// value -- this is where a run-time SchemaRef/SchemaPath is recovered and
// walked. The EP §6.4.7 run-time LAYOUT walk itself (the differential-oracle
// counterpart to CGTypes' static layout) lives in SchemaLayoutEngine,
// reached here through the SchemaLayout& below, never duplicated.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

#include "CGSymbolTable.h"
#include "CGTypes.h"
#include "RangeCheckGuards.h"
#include "RuntimeFunctionCache.h"
#include "SchemaLayoutEngine.h"
#include "SchemaTypeRegistry.h"
#include "StringRuntime.h"
#include "VarEntry.h"

namespace llvm {
class Module;
class Value;
}

namespace plang {
struct ArrayTypeNode;
struct ExprNode;
struct FieldExpr;
struct RecordTypeNode;
struct TypeNode;
struct VariantPart;
struct Type;
}

class SchemaAccess {
public:
    SchemaAccess(
        llvm::LLVMContext& Ctx, llvm::Module& Mod, llvm::IRBuilder<>& B,
        SchemaTypeRegistry& SchemaTypes, SchemaLayoutEngine& SchemaLayout,
        CGTypes& Types, RuntimeFunctionCache& RtFns, StringRuntime& Strings,
        RangeCheckGuards& RangeGuards, CGSymbolTable& SymTab,
        std::vector<std::unordered_map<std::string, VarEntry>>& Scopes,
        llvm::IntegerType* I64Ty, llvm::IntegerType* I8Ty, llvm::PointerType* PtrTy,
        std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr,
        std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue,
        std::function<llvm::Value*(const plang::ExprNode&)> EmitStrAddr,
        std::function<llvm::Value*(llvm::Value*)> ToI64,
        std::function<bool(const plang::ExprNode&)> ExprIsVarStr,
        std::function<bool(const plang::ExprNode&)> ExprIsCharStr,
        std::function<int64_t(const plang::ExprNode&)> ExprStrCap,
        std::function<int64_t(const plang::ExprNode&)> ExprCharStrLen,
        std::function<unsigned(const std::string&, size_t)> SchemaArgDiscCountOf)
        : Ctx(Ctx), Mod(Mod), B(B), SchemaTypes(SchemaTypes),
          SchemaLayout(SchemaLayout), Types(Types), RtFns(RtFns),
          Strings(Strings), RangeGuards(RangeGuards), SymTab(SymTab),
          Scopes(Scopes), I64Ty(I64Ty), I8Ty(I8Ty), PtrTy(PtrTy),
          EmitExpr(std::move(EmitExpr)), EmitLValue(std::move(EmitLValue)),
          EmitStrAddr(std::move(EmitStrAddr)), ToI64(std::move(ToI64)),
          ExprIsVarStr(std::move(ExprIsVarStr)),
          ExprIsCharStr(std::move(ExprIsCharStr)),
          ExprStrCap(std::move(ExprStrCap)),
          ExprCharStrLen(std::move(ExprCharStrLen)),
          SchemaArgDiscCountOf(std::move(SchemaArgDiscCountOf)) {}

    /// The run-time view of a schema-typed expression: its semantic type,
    /// the address its body starts at, and its discriminants -- recovered
    /// from a formal parameter's own arguments, or from the header
    /// emitNewSchema wrote in front of p^.
    struct SchemaRef {
        const plang::Type*        semaTy{nullptr}; // TypeKind::Schema
        llvm::Value*              data{nullptr};   // start of the body storage
        std::vector<llvm::Value*> discs;           // one i64 per discriminant
    };

    /// A resolved access path into a run-time-laid-out object: the
    /// enclosing schema whose header carries the discriminants, the
    /// component's address, and the denoter its extents are written in.
    struct SchemaPath {
        SchemaRef               root;
        llvm::Value*            addr{nullptr};
        const plang::TypeNode*  decl{nullptr};
    };

    std::optional<SchemaRef> schemaRefOf(const plang::ExprNode& e);
    std::pair<llvm::Value*, std::vector<llvm::Value*>>
    schemaActual(const plang::ExprNode& arg, unsigned discCount);
    unsigned schemaArgDiscs(const std::string& mangledName, size_t astArgIdx) const;
    void pushSchemaArgs(std::vector<llvm::Value*>& args, const plang::ExprNode& arg,
                        unsigned discCount);
    void emitSchemaDiscMatch(const SchemaRef& dst, const SchemaRef& src);
    std::pair<llvm::Value*, llvm::Value*> schemaArrayBounds(const SchemaRef& ref);
    llvm::Type* schemaStorageType(const SchemaRef& ref);
    llvm::Value* schemaBodySize(const plang::Type& schema,
                                const std::vector<llvm::Value*>& discs);
    void emitNewSchema(const plang::ExprNode& ptrArg, const plang::Type& schema,
                       std::span<const std::unique_ptr<plang::ExprNode>> discArgs);
    llvm::Value* exprStrCapV(const plang::ExprNode& e);
    /// R5: the address AND the capacity of a string from ONE walk of its
    /// access path.
    std::pair<llvm::Value*, llvm::Value*> strAddrAndCap(const plang::ExprNode& e);
    llvm::Value* strCapFromPath(const SchemaPath& path);
    const plang::ArrayTypeNode* varyingArrayFieldOf(const plang::FieldExpr& fe);
    std::pair<SchemaRef, const plang::TypeNode*>
    descendIntoInstantiation(const SchemaRef& root, llvm::Value* addr,
                             const plang::TypeNode* decl);
    std::optional<SchemaPath> schemaPathOf(const plang::ExprNode& e);
    const plang::TypeNode* fieldDenoterOf(const plang::RecordTypeNode& rt,
                                          const std::string& field);
    const plang::TypeNode* variantFieldDenoterOf(const plang::VariantPart& vp,
                                                 const std::string& field);
    void setVarStrCap(const std::string& name, llvm::Value* cap);
    void setVarSchemaPath(const std::string& name, const SchemaRef& root,
                          const plang::TypeNode* decl);

private:
    llvm::LLVMContext& Ctx;
    llvm::Module& Mod;
    llvm::IRBuilder<>& B;
    SchemaTypeRegistry& SchemaTypes;
    SchemaLayoutEngine& SchemaLayout;
    CGTypes& Types;
    RuntimeFunctionCache& RtFns;
    StringRuntime& Strings;
    RangeCheckGuards& RangeGuards;
    CGSymbolTable& SymTab;
    std::vector<std::unordered_map<std::string, VarEntry>>& Scopes;
    llvm::IntegerType* I64Ty;
    llvm::IntegerType* I8Ty;
    llvm::PointerType* PtrTy;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitStrAddr;
    std::function<llvm::Value*(llvm::Value*)> ToI64;
    /// Stateless string-shape predicates -- static Impl methods
    /// (CodeGenImpl.h) used far outside this unit too, so they stay put
    /// rather than moving; reached here through a closure rather than a
    /// qualified call, which would need this file to see all of Impl.
    std::function<bool(const plang::ExprNode&)> ExprIsVarStr;
    std::function<bool(const plang::ExprNode&)> ExprIsCharStr;
    std::function<int64_t(const plang::ExprNode&)> ExprStrCap;
    std::function<int64_t(const plang::ExprNode&)> ExprCharStrLen;
    /// schemaArgDiscs' whole three-line paramMeta_ lookup -- narrower to
    /// bridge this one derived query than to give Impl::ParamMeta (used far
    /// outside this unit) a free-standing header as a side effect of this
    /// extraction.
    std::function<unsigned(const std::string&, size_t)> SchemaArgDiscCountOf;

    /// R6: substr/trim's result is typed as its ARGUMENT's Type object (see
    /// the CallExpr branch of exprStrCapV), so strAddrAndCap on a substr/trim
    /// call is a second question about that same argument on top of the one
    /// call-evaluation already answers while marshalling it -- and asking it
    /// by walking the argument fresh repeated whatever side effect an index
    /// in its path has: `substr(q^.a[next].s, 1, 2)` called `next` once for
    /// the call's own argument and once more for the result's capacity.
    /// strAddrAndCap primes these across that one call-evaluation, so the
    /// lookup its own argument marshalling makes of the very same node (by
    /// pointer identity -- a node is only ever this call's own argument)
    /// answers from here instead of walking again.  Set only for the
    /// duration of that one nested call and cleared unconditionally after,
    /// whether or not it was read.
    const plang::ExprNode* pendingArgExpr_{nullptr};
    std::pair<llvm::Value*, llvm::Value*> pendingArgVal_{};

    llvm::Constant* i64c(int64_t v) const;
};
