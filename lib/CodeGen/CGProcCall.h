// CGProcCall.h — the required-procedure dispatch chain (ISO §6.6.5/EP
// §6.7.5) and user-declared procedure call statements.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

#include "BuiltinIO.h"
#include "CGLinkage.h"
#include "CGPackUnpack.h"
#include "CGSymbolTable.h"
#include "CGTypes.h"
#include "ClosureAndCallABI.h"
#include "FileVarHelpers.h"
#include "RuntimeFunctionCache.h"
#include "SchemaAccess.h"
#include "SetOps.h"
#include "StringCallMarshalling.h"

namespace llvm { class Module; class Value; }
namespace plang {
struct CallStmt; struct ExprNode; struct TypeNode;
struct ProcedureTypeNode;
}

class CGProcCall {
public:
    CGProcCall(llvm::LLVMContext& Ctx, llvm::Module& Mod, llvm::IRBuilder<>& B,
               FileVarHelpers& FileVars, RuntimeFunctionCache& RtFns,
               BuiltinIO& Builtins, ClosureAndCallABI& ClosureAbi,
               SchemaAccess& Schema, CGTypes& Types, CGSymbolTable& SymTab,
               CGLinkage& Linkage, SetOps& Sets, StringCallMarshalling& StrCall,
               CGPackUnpack& PackUnpack,
               llvm::IntegerType* I8Ty, llvm::IntegerType* I64Ty, llvm::PointerType* PtrTy,
               std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr,
               std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue,
               std::function<llvm::Value*(llvm::Value*)> ToI64,
               std::function<const plang::TypeNode*(const plang::TypeNode*)> InitialStateShapeOf,
               std::function<bool(const plang::TypeNode*)> HasInitialState,
               std::function<void(llvm::Value*, llvm::Type*, const plang::TypeNode*)> EmitInitialState,
               std::function<void(llvm::Value*, const plang::Type&,
                                  const std::vector<llvm::Value*>&)> EmitSchemaInitialState,
               std::function<llvm::Value*(const std::string&)> BuildStaticLinkFrame,
               std::function<const plang::ProcedureTypeNode*(const std::string&, size_t)> ProcParamArg,
               std::function<bool(const std::string&, size_t)> ParamIsByRef,
               std::function<size_t(const std::string&, size_t)> ConformantDimsOf,
               std::function<std::optional<int64_t>(const std::string&, size_t)> ParamSetBaseOf)
        : Ctx(Ctx), Mod(Mod), B(B), FileVars(FileVars), RtFns(RtFns),
          Builtins(Builtins), ClosureAbi(ClosureAbi), Schema(Schema), Types(Types),
          SymTab(SymTab), Linkage(Linkage), Sets(Sets), StrCall(StrCall),
          PackUnpack(PackUnpack),
          I8Ty(I8Ty), I64Ty(I64Ty), PtrTy(PtrTy),
          EmitExpr(std::move(EmitExpr)), EmitLValue(std::move(EmitLValue)),
          ToI64(std::move(ToI64)),
          InitialStateShapeOf(std::move(InitialStateShapeOf)),
          HasInitialState(std::move(HasInitialState)),
          EmitInitialState(std::move(EmitInitialState)),
          EmitSchemaInitialState(std::move(EmitSchemaInitialState)),
          BuildStaticLinkFrame(std::move(BuildStaticLinkFrame)),
          ProcParamArg(std::move(ProcParamArg)), ParamIsByRef(std::move(ParamIsByRef)),
          ConformantDimsOf(std::move(ConformantDimsOf)),
          ParamSetBaseOf(std::move(ParamSetBaseOf)) {}

    void emitCallStmt(const plang::CallStmt& s);
    void emitUserProcCall(const plang::CallStmt& s);

private:
    llvm::LLVMContext& Ctx;
    llvm::Module& Mod;
    llvm::IRBuilder<>& B;
    FileVarHelpers& FileVars;
    RuntimeFunctionCache& RtFns;
    BuiltinIO& Builtins;
    ClosureAndCallABI& ClosureAbi;
    SchemaAccess& Schema;
    CGTypes& Types;
    CGSymbolTable& SymTab;
    CGLinkage& Linkage;
    SetOps& Sets;
    StringCallMarshalling& StrCall;
    CGPackUnpack& PackUnpack;
    llvm::IntegerType* I8Ty;
    llvm::IntegerType* I64Ty;
    llvm::PointerType* PtrTy;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue;
    std::function<llvm::Value*(llvm::Value*)> ToI64;
    std::function<const plang::TypeNode*(const plang::TypeNode*)> InitialStateShapeOf;
    std::function<bool(const plang::TypeNode*)> HasInitialState;
    std::function<void(llvm::Value*, llvm::Type*, const plang::TypeNode*)> EmitInitialState;
    /// EP §6.6 with §6.4.7: the same idea as EmitInitialState, for a schema
    /// instance's body -- which, new()'s discriminants being run-time values
    /// in general, is laid out at run time and so cannot share that one's
    /// static llvm::Type* / GEP-by-index walk.  See Codegen::Impl::
    /// emitSchemaInitialState.
    std::function<void(llvm::Value*, const plang::Type&,
                       const std::vector<llvm::Value*>&)> EmitSchemaInitialState;
    std::function<llvm::Value*(const std::string&)> BuildStaticLinkFrame;
    std::function<const plang::ProcedureTypeNode*(const std::string&, size_t)> ProcParamArg;
    std::function<bool(const std::string&, size_t)> ParamIsByRef;
    std::function<size_t(const std::string&, size_t)> ConformantDimsOf;
    std::function<std::optional<int64_t>(const std::string&, size_t)> ParamSetBaseOf;
};
