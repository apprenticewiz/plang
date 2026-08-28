// CGProcCall.h — the required-procedure dispatch chain (ISO §6.6.5/EP
// §6.7.5) and user-declared procedure call statements.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

#include "BuiltinIO.h"
#include "CGAssign.h"
#include "CGLinkage.h"
#include "CGPackUnpack.h"
#include "CGSymbolTable.h"
#include "CGTypes.h"
#include "ClosureAndCallABI.h"
#include "FileVarHelpers.h"
#include "RangeCheckGuards.h"
#include "RuntimeFunctionCache.h"
#include "SchemaAccess.h"
#include "SetOps.h"
#include "StringCallMarshalling.h"

namespace llvm { class BasicBlock; class Module; class Value; }
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
               CGPackUnpack& PackUnpack, RangeCheckGuards& RangeGuards,
               CGAssign& Assign,
               llvm::IntegerType* I8Ty, llvm::IntegerType* I64Ty, llvm::PointerType* PtrTy,
               std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr,
               std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue,
               std::function<llvm::Value*(llvm::Value*)> ToI64,
               std::function<llvm::Value*(llvm::Value*)> EnsureI1,
               std::function<const plang::TypeNode*(const plang::TypeNode*)> InitialStateShapeOf,
               std::function<bool(const plang::TypeNode*)> HasInitialState,
               std::function<void(llvm::Value*, llvm::Type*, const plang::TypeNode*)> EmitInitialState,
               std::function<void(llvm::Value*, const plang::Type&,
                                  const std::vector<llvm::Value*>&)> EmitSchemaInitialState,
               std::function<llvm::Value*(const std::string&)> BuildStaticLinkFrame,
               std::function<const plang::ProcedureTypeNode*(const std::string&, size_t)> ProcParamArg,
               std::function<bool(const std::string&, size_t)> ParamIsByRef,
               std::function<size_t(const std::string&, size_t)> ConformantDimsOf,
               std::function<std::optional<int64_t>(const std::string&, size_t)> ParamSetBaseOf,
               std::function<const std::string&()> CurFuncName,
               std::function<std::shared_ptr<plang::Type>()> CurRetSemaType,
               std::function<llvm::BasicBlock*()> CurrentContinueTarget,
               std::function<llvm::BasicBlock*()> CurrentBreakTarget,
               std::function<llvm::BasicBlock*()> ExitBlock,
               std::function<llvm::Value*(const std::string&,
                   std::span<const std::unique_ptr<plang::ExprNode>>,
                   plang::SourceLocation)> EmitBuiltinFuncCall)
        : Ctx(Ctx), Mod(Mod), B(B), FileVars(FileVars), RtFns(RtFns),
          Builtins(Builtins), ClosureAbi(ClosureAbi), Schema(Schema), Types(Types),
          SymTab(SymTab), Linkage(Linkage), Sets(Sets), StrCall(StrCall),
          PackUnpack(PackUnpack), RangeGuards(RangeGuards), Assign(Assign),
          I8Ty(I8Ty), I64Ty(I64Ty), PtrTy(PtrTy),
          EmitExpr(std::move(EmitExpr)), EmitLValue(std::move(EmitLValue)),
          ToI64(std::move(ToI64)), EnsureI1(std::move(EnsureI1)),
          InitialStateShapeOf(std::move(InitialStateShapeOf)),
          HasInitialState(std::move(HasInitialState)),
          EmitInitialState(std::move(EmitInitialState)),
          EmitSchemaInitialState(std::move(EmitSchemaInitialState)),
          BuildStaticLinkFrame(std::move(BuildStaticLinkFrame)),
          ProcParamArg(std::move(ProcParamArg)), ParamIsByRef(std::move(ParamIsByRef)),
          ConformantDimsOf(std::move(ConformantDimsOf)),
          ParamSetBaseOf(std::move(ParamSetBaseOf)),
          CurFuncName(std::move(CurFuncName)), CurRetSemaType(std::move(CurRetSemaType)),
          CurrentContinueTarget(std::move(CurrentContinueTarget)),
          CurrentBreakTarget(std::move(CurrentBreakTarget)),
          ExitBlock(std::move(ExitBlock)),
          EmitBuiltinFuncCall(std::move(EmitBuiltinFuncCall)) {}

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
    /// TP's Assertions switch, read through RangeGuards.assertionsAt --
    /// reused rather than duplicated here, since RangeCheckGuards already
    /// carries the Opts reference this needs and the emitGuard/reporter
    /// shape Assert's own guard is built from.
    RangeCheckGuards& RangeGuards;
    /// TP-only: Exit(value)'s store into the enclosing function's result --
    /// see emitAssignValue's own doc comment for why this is reused rather
    /// than reimplemented here.
    CGAssign& Assign;
    llvm::IntegerType* I8Ty;
    llvm::IntegerType* I64Ty;
    llvm::PointerType* PtrTy;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue;
    std::function<llvm::Value*(llvm::Value*)> ToI64;
    /// Normalizes a Boolean expression's raw LLVM value to i1, the type
    /// CreateCondBr (and so RangeGuards.emitGuard) requires -- the same
    /// widening every OTHER boolean-condition call site (emitIf, emitWhile,
    /// ...) already goes through, needed here for Assert's own condition.
    std::function<llvm::Value*(llvm::Value*)> EnsureI1;
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

    // TP-only: Exit/Break/Continue (all reached through CallStmt, dispatched
    // on spelling below exactly like Halt/Assert).
    /// The enclosing function or procedure's own mangled-source name --
    /// Exit(value)'s synthesized target IdentExpr is named this, which
    /// EmitLValue's own IdentExpr case already resolves straight to
    /// CurRetAlloca (its fast path, matched on this same name) exactly as it
    /// would for a written-out `FuncName := value`.
    std::function<const std::string&()> CurFuncName;
    /// The semantic type CurFuncName's result cell holds, or null outside a
    /// function -- Sema's checkCallStmt Exit arm has already refused
    /// Exit(value) wherever this would be null, so it is read only when
    /// non-null.  See CodeGenImpl.h's curRetSemaType.
    std::function<std::shared_ptr<plang::Type>()> CurRetSemaType;
    /// CGFunction::LoopStack.back()'s two halves -- see CGControlFlow.h's
    /// PushLoopTargets/PopLoopTargets for where the stack is maintained.
    /// Sema's LoopDepth_ (Sema.h) has already refused a Break/Continue
    /// reaching here with nothing pushed.
    std::function<llvm::BasicBlock*()> CurrentContinueTarget;
    std::function<llvm::BasicBlock*()> CurrentBreakTarget;
    /// Where Exit branches; see CGFunction::ExitBB.
    std::function<llvm::BasicBlock*()> ExitBlock;
    /// Turbo `{$X+}`: CGFuncCall::emitBuiltinCall, bridged rather than
    /// called directly because CGFuncCall (funcCall_) is constructed after
    /// CGProcCall (procCall_) in Codegen::Impl::init -- see that ordering's
    /// own comment in CodeGenTypes.cpp.  Reached only from emitCallStmt's
    /// tail, once every required-PROCEDURE name it dispatches by spelling
    /// has already failed to match; see emitBuiltinCall's own comment for
    /// why that means a required FUNCTION called as a statement.
    std::function<llvm::Value*(const std::string&,
        std::span<const std::unique_ptr<plang::ExprNode>>,
        plang::SourceLocation)> EmitBuiltinFuncCall;
};
