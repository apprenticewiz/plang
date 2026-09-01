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
#include "CGCallMarshal.h"
#include "CGLinkage.h"
#include "CGPackUnpack.h"
#include "CGSymbolTable.h"
#include "CGTypes.h"
#include "ClosureAndCallABI.h"
#include "FileVarHelpers.h"
#include "OrdinalSignedness.h"
#include "RangeCheckGuards.h"
#include "RuntimeFunctionCache.h"
#include "SchemaAccess.h"
#include "SetOps.h"
#include "StringCallMarshalling.h"
#include "StringRuntime.h"

namespace llvm { class BasicBlock; class Module; class Value; }
namespace plang {
struct CallStmt; struct ExprNode; struct TypeNode;
struct ProcedureTypeNode; struct MethodCallStmt; struct InheritedCallStmt;
}

class CGProcCall {
public:
    CGProcCall(llvm::LLVMContext& Ctx, llvm::Module& Mod, llvm::IRBuilder<>& B,
               FileVarHelpers& FileVars, RuntimeFunctionCache& RtFns,
               BuiltinIO& Builtins, ClosureAndCallABI& ClosureAbi,
               SchemaAccess& Schema, CGTypes& Types, CGSymbolTable& SymTab,
               CGLinkage& Linkage, SetOps& Sets, StringCallMarshalling& StrCall,
               CGPackUnpack& PackUnpack, RangeCheckGuards& RangeGuards,
               CGAssign& Assign, StringRuntime& Strings, CGCallMarshal& Marshal,
               llvm::IntegerType* I8Ty, llvm::IntegerType* I64Ty, llvm::PointerType* PtrTy,
               llvm::Type* DblTy,
               std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr,
               std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue,
               std::function<llvm::Value*(llvm::Value*, bool)> ToI64,
               std::function<llvm::Value*(llvm::Value*)> EnsureI1,
               std::function<llvm::Value*(llvm::Value*, llvm::Type*)> CoerceToType,
               std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca,
               std::function<int64_t(const plang::ExprNode&)> ExprShortStrCap,
               std::function<bool(const plang::ExprNode&)> ExprIsShortStr,
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
               std::function<llvm::Value*()> CurCtorOkAlloca,
               std::function<llvm::Value*(const std::string&,
                   std::span<const std::unique_ptr<plang::ExprNode>>,
                   plang::SourceLocation)> EmitBuiltinFuncCall,
               std::function<void(llvm::Value*, const plang::Type&)> StampVptr,
               std::function<void(llvm::Value*, const plang::Type&)> StampFieldVptrs)
        : Ctx(Ctx), Mod(Mod), B(B), FileVars(FileVars), RtFns(RtFns),
          Builtins(Builtins), ClosureAbi(ClosureAbi), Schema(Schema), Types(Types),
          SymTab(SymTab), Linkage(Linkage), Sets(Sets), StrCall(StrCall),
          PackUnpack(PackUnpack), RangeGuards(RangeGuards), Assign(Assign),
          Marshal(Marshal), Strings(Strings),
          I8Ty(I8Ty), I64Ty(I64Ty), PtrTy(PtrTy), DblTy(DblTy),
          EmitExpr(std::move(EmitExpr)), EmitLValue(std::move(EmitLValue)),
          ToI64(std::move(ToI64)), EnsureI1(std::move(EnsureI1)),
          CoerceToType(std::move(CoerceToType)),
          CreateEntryAlloca(std::move(CreateEntryAlloca)),
          ExprShortStrCap(std::move(ExprShortStrCap)),
          ExprIsShortStr(std::move(ExprIsShortStr)),
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
          CurCtorOkAlloca(std::move(CurCtorOkAlloca)),
          EmitBuiltinFuncCall(std::move(EmitBuiltinFuncCall)),
          StampVptr(std::move(StampVptr)),
          StampFieldVptrs(std::move(StampFieldVptrs)) {}

    void emitCallStmt(const plang::CallStmt& s);
    void emitUserProcCall(const plang::CallStmt& s);
    /// Turbo Tier 5, Cluster A item 4: 'Obj.Method(args);' / 'P^.Method(args);'
    /// / the bare no-parens 'Obj.Method;' used as a STATEMENT -- the
    /// CGProcCall sibling of CGFuncCall::emitMethodCallExpr (see its own
    /// comment for the whole design); a function method reaching here (
    /// Turbo `{$X+}`, MethodCallStmt::ResolvedType non-null) has its result
    /// simply discarded, exactly like an ordinary CallStmt's identical case.
    void emitMethodCallStmt(const plang::MethodCallStmt& s);
    /// Turbo Tier 5, Cluster A item 5: 'inherited [Method[(args)]];' -- a
    /// STATIC call (never through the VMT) to the mangled symbol
    /// Sema::checkInheritedCallStmt already resolved (InheritedCallStmt::
    /// ImplementingType/ResolvedMethod), with the CURRENTLY EXECUTING
    /// function's own Self argument (llvm::Function::getArg(0) -- every
    /// method has one prepended, see emitFunctionDef's own 'Self' comment)
    /// forwarded as this call's own Self, unchanged.  The bare 'inherited;'
    /// form (InheritedCallStmt::Method empty) forwards this activation's
    /// remaining LLVM arguments (getArg(1) onward) verbatim, with no
    /// re-marshalling at all -- sound only because Sema's own override-
    /// signature check already guarantees the ancestor's own parameter list
    /// is identical; see InheritedCallStmt's own comment (AstStmt.h).
    void emitInheritedCallStmt(const plang::InheritedCallStmt& s);

private:
    /// Turbo Tier 5, Cluster A item 6: a bound method call where the
    /// receiver's own address (\p selfPtr) is already in hand -- built for
    /// New(P, Init(...))'s constructor call and Dispose(P, Done)'s
    /// destructor call, neither of which has a real Receiver ExprNode the
    /// way an ordinary 'Obj.Method(...)' does (P^'s storage did not exist
    /// as a variable before New allocated it).  Otherwise identical to
    /// emitMethodCallStmt's own marshal-and-dispatch tail -- same fresh-copy
    /// convention as methodOwnerType/methodEntryOf (this file's own
    /// anonymous namespace) rather than a shared helper, see their own
    /// comments.  \p RecvTy is the STATIC type selfPtr's storage was laid
    /// out as (Pointee, for both New/Init and Dispose/Done -- never an
    /// ancestor: unlike an ordinary method call there is no polymorphic
    /// receiver expression here, only the pointer's own declared domain
    /// type), used both to find Method's owning type in the ancestor chain
    /// and, for a virtual Method, to compute '_vptr's own offset.  Returns
    /// the call's own result (a constructor's i1 success flag; null/void
    /// for a destructor), matching CreateCall's own return either way.
    llvm::Value* emitBoundMethodCall(llvm::Value* selfPtr, const plang::Type& RecvTy,
                                     const std::string& Method,
                                     std::span<const std::unique_ptr<plang::ExprNode>> Args);

    /// Turbo Tier 4, Cluster C item 6: recognizes a call to one of Dos.pas's
    /// six string-VALUE-parameter exports (ChDir/MkDir/RmDir/Exec/FindFirst)
    /// and emits it directly against its own scalar-only runtime entry
    /// point, bypassing the ordinary mangled-external-call path below --
    /// see this method's own definition (CGProcCall.cpp) for why.  Returns
    /// false (having emitted nothing) for every other call, including one
    /// that merely happens to share a name with one of these but was never
    /// reached through 'uses Dos'.
    bool tryEmitDosProcCall(const plang::CallStmt& s);

    /// -std=turbo only: emits `call void @plang_iocheck()` right after a
    /// write/writeln/read/readln/Reset/Rewrite/Append/Close statement's own
    /// call sequence, but ONLY when RangeGuards.isTurbo() && RangeGuards.
    /// ioChecksAt(Loc) -- \p Loc is the STATEMENT's own s.Loc, not any
    /// failing operation's, which is what makes `{$I-}` genuinely
    /// positional/deferred rather than eager-at-the-failing-call (see
    /// RangeCheckGuards::ioChecksAt's own doc comment).  A no-op call site
    /// for ISO/EP -- RangeGuards.isTurbo() is false there, so nothing is
    /// ever emitted and those dialects' write/writeln/read/readln/Reset/
    /// Rewrite codegen is byte-for-byte unchanged.
    void emitIoCheckIfNeeded(plang::SourceLocation Loc);

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
    /// Issue #299 Phase 1: the per-argument marshalling loop shared with
    /// CGFuncCall::emitUserFuncCall/emitMethodCallExpr -- see CGCallMarshal.h.
    CGCallMarshal& Marshal;
    /// The Turbo System-unit string routines that mutate a ShortString var
    /// parameter in place (Delete/Insert/SetLength) and Str/Val (CGProcCall.cpp)
    /// call plang_sstr_*/plang_val_* runtime entry points directly through
    /// this, the same way StringRuntime already serves CGFuncCall/CGBinaryOps
    /// -- CGProcCall had no need of it before these five, everything else it
    /// lowers going through StrCall (StringCallMarshalling) instead.
    StringRuntime& Strings;
    llvm::IntegerType* I8Ty;
    llvm::IntegerType* I64Ty;
    llvm::PointerType* PtrTy;
    /// double -- Val(s, v, code)'s own runtime result cell for a Real
    /// destination, and CoerceToType's target/source when v is Turbo's
    /// Single (float) instead.
    llvm::Type* DblTy;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue;
    /// The bool is the operand's actual Sema-resolved Type::IsSigned --
    /// CGBinaryOps.h's identical ToI64 member has the fuller version of this
    /// comment.  Upgraded from a 1-arg (no operand-type context) bridge to
    /// this 2-arg one in issue #177's sibling audit: every call site in this
    /// file now passes exprIsSigned(x) (OrdinalSignedness.h) for whatever
    /// ExprNode x the value being widened came from.
    std::function<llvm::Value*(llvm::Value*, bool)> ToI64;
    /// Normalizes a Boolean expression's raw LLVM value to i1, the type
    /// CreateCondBr (and so RangeGuards.emitGuard) requires -- the same
    /// widening every OTHER boolean-condition call site (emitIf, emitWhile,
    /// ...) already goes through, needed here for Assert's own condition.
    std::function<llvm::Value*(llvm::Value*)> EnsureI1;
    /// Widens/narrows a loaded scalar to another LLVM type -- Val(s, v, code)
    /// needs it to store its runtime-parsed int64/double result into v's own
    /// (possibly narrower, possibly Single-not-double) declared width, the
    /// same coercion emitReadArg's own "convert" path already applies for an
    /// ordinary read() target.
    std::function<llvm::Value*(llvm::Value*, llvm::Type*)> CoerceToType;
    /// A stack temporary, sized and named -- Val's own int64_t/double result
    /// cells before CoerceToType narrows them into v's address.
    std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca;
    /// Turbo string[N]'s own capacity query -- see exprShortStrCap's own doc
    /// comment (CodeGenImpl.h) and CGFuncCall.h's identical bridge, needed
    /// here for the same reason: Delete/Insert/SetLength/Str/Val all size a
    /// ShortString operand or result by it.
    std::function<int64_t(const plang::ExprNode&)> ExprShortStrCap;
    /// Turbo string[N]'s own predicate -- see exprIsShortStr's doc comment
    /// (CodeGenImpl.h) and CGFuncCall.h's identical bridge.  Delete/Insert/
    /// SetLength/Str/Val's local sstrArgPtr lambda (CGProcCall.cpp) uses this
    /// to tell an already-ShortString operand (address + capacity, taken
    /// directly) from a Char/literal one (materialized into a fresh temp).
    std::function<bool(const plang::ExprNode&)> ExprIsShortStr;
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
    /// Turbo Tier 5, Cluster A item 6: the CURRENTLY EXECUTING constructor's
    /// own hidden success-flag alloca (Codegen::Impl::curCtorOkAlloca --
    /// see its own comment, CodeGenImpl.h, for the whole ABI design), or
    /// null outside a constructor body.  'Fail' (this file's own 'fail'
    /// arm) stores false here then branches to ExitBlock() exactly like
    /// Exit does -- the two share the same epilogue, just a different flag.
    std::function<llvm::Value*()> CurCtorOkAlloca;
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
    /// Turbo Tier 5, Cluster A item 6: stamps \p Type's own VMT global
    /// address into the '_vptr' slot of the memory \p llvm::Value* points
    /// at -- Codegen::Impl::stampVptr itself (CodeGenProcs.cpp), reached
    /// through a closure the same way BuildStaticLinkFrame/
    /// EmitBuiltinFuncCall are: getOrCreateVmt (stampVptr's own callee) is
    /// private to Impl and stays there, so New(P, Init(...))'s freshly
    /// allocated, not-yet-a-variable memory reaches the exact same
    /// stamping logic emitVarValueInit already gives a directly declared
    /// local/global, without a second implementation of it here.
    std::function<void(llvm::Value*, const plang::Type&)> StampVptr;
    /// Issue #511: Codegen::Impl::stampFieldVptrs, reached the same way
    /// StampVptr just above is -- the nested-member counterpart New(P,
    /// Init(...)) needs exactly as much as a directly declared local/global
    /// does, for the identical reason (see stampFieldVptrs's own comment,
    /// CodeGenImpl.h).  Called right beside StampVptr, never instead of it:
    /// this reaches only what \p Type's OWN structure holds nested inside
    /// it, not \p Type's own top-level slot.
    std::function<void(llvm::Value*, const plang::Type&)> StampFieldVptrs;
};
