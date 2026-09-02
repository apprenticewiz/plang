// CGFuncCall.h — call-expression emission: the built-in function dispatch
// chain (math/complex/file-status/ordinal/EP §6.7.5.4 string functions) and
// the tail call to a user-declared function (ISO §6.6.5).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

#include "CGCallMarshal.h"
#include "CGLinkage.h"
#include "CGSymbolTable.h"
#include "CGTypes.h"
#include "ClosureAndCallABI.h"
#include "ComplexOps.h"
#include "FileVarHelpers.h"
#include "OrdinalSignedness.h"
#include "RangeCheckGuards.h"
#include "RuntimeFunctionCache.h"
#include "SchemaAccess.h"
#include "SetOps.h"
#include "StringCallMarshalling.h"
#include "StringRuntime.h"

namespace llvm { class Module; class Value; class GlobalVariable; class Function; }
namespace plang {
struct CallExpr; struct ExprNode; struct ProcedureTypeNode; struct MethodCallExpr;
struct InheritedCallExpr;
struct Type;
}

class CGFuncCall {
public:
    CGFuncCall(llvm::LLVMContext& Ctx, llvm::Module& Mod, llvm::IRBuilder<>& B,
               RuntimeFunctionCache& RtFns, SetOps& Sets, ComplexOps& Complex,
               FileVarHelpers& FileVars, CGTypes& Types, SchemaAccess& Schema,
               StringRuntime& Strings, StringCallMarshalling& StrCall,
               CGLinkage& Linkage, CGSymbolTable& SymTab,
               ClosureAndCallABI& ClosureAbi, RangeCheckGuards& RangeGuards,
               CGCallMarshal& Marshal,
               llvm::IntegerType* I64Ty, llvm::IntegerType* I8Ty,
               llvm::Type* DblTy, llvm::PointerType* PtrTy,
               std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr,
               std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue,
               std::function<llvm::Value*(llvm::Value*)> ToDouble,
               std::function<llvm::Value*(llvm::Value*, bool)> ToI64,
               std::function<llvm::Value*(llvm::Value*)> EnsureI1,
               std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca,
               std::function<llvm::Value*(llvm::Value*, const std::string&)> CreateDynStrAlloca,
               std::function<llvm::Value*(const std::string&)> BuildStaticLinkFrame,
               std::function<bool(const plang::ExprNode&)> ExprIsVarStr,
               std::function<bool(const plang::ExprNode&)> ExprIsCharStr,
               std::function<int64_t(const plang::ExprNode&)> ExprCharStrLen,
               std::function<int64_t(const plang::ExprNode&)> ExprStrCapStatic,
               std::function<bool(const plang::ExprNode&)> ExprIsShortStr,
               std::function<int64_t(const plang::ExprNode&)> ExprShortStrCap,
               std::function<llvm::GlobalVariable*(const plang::Type&)> GetOrCreateVmt,
               std::function<llvm::Function*(const plang::Type::Method&,
                                             const std::string&)> DeclareForeignInheritedCallee,
               std::function<llvm::Value*(const plang::Type&,
                                          const plang::ExprNode&)> EmitNewObjectValue)
        : Ctx(Ctx), Mod(Mod), B(B), RtFns(RtFns), Sets(Sets), Complex(Complex),
          FileVars(FileVars), Types(Types), Schema(Schema), Strings(Strings),
          StrCall(StrCall), Linkage(Linkage), SymTab(SymTab), ClosureAbi(ClosureAbi),
          RangeGuards(RangeGuards), Marshal(Marshal),
          I64Ty(I64Ty), I8Ty(I8Ty), DblTy(DblTy), PtrTy(PtrTy),
          EmitExpr(std::move(EmitExpr)), EmitLValue(std::move(EmitLValue)),
          ToDouble(std::move(ToDouble)), ToI64(std::move(ToI64)), EnsureI1(std::move(EnsureI1)),
          CreateEntryAlloca(std::move(CreateEntryAlloca)),
          CreateDynStrAlloca(std::move(CreateDynStrAlloca)),
          BuildStaticLinkFrame(std::move(BuildStaticLinkFrame)),
          ExprIsVarStr(std::move(ExprIsVarStr)), ExprIsCharStr(std::move(ExprIsCharStr)),
          ExprCharStrLen(std::move(ExprCharStrLen)), ExprStrCapStatic(std::move(ExprStrCapStatic)),
          ExprIsShortStr(std::move(ExprIsShortStr)), ExprShortStrCap(std::move(ExprShortStrCap)),
          GetOrCreateVmt(std::move(GetOrCreateVmt)),
          DeclareForeignInheritedCallee(std::move(DeclareForeignInheritedCallee)),
          EmitNewObjectValue(std::move(EmitNewObjectValue)) {}

    llvm::Value* emitCallExpr(const plang::CallExpr& e);
    llvm::Value* emitUserFuncCall(const plang::CallExpr& e);
    /// Turbo Tier 5, Cluster A item 4: 'Obj.Method(args)' / 'P^.Method(args)'
    /// used as a value -- a STATIC/direct call to Method's own mangled
    /// symbol (CGLinkage::mangledMethod), with the receiver's address
    /// prepended as an extra leading argument ahead of the ordinary
    /// Pascal-declared ones, mirroring emitUserFuncCall's own static-link
    /// prepend just below it for a nested function.  Virtual dispatch
    /// through a VMT global is explicitly NOT this item's job (Cluster A
    /// item 5): whatever method the ancestor-chain walk below finds is
    /// called directly, whether or not it happens to be declared 'virtual'.
    llvm::Value* emitMethodCallExpr(const plang::MethodCallExpr& e);

    /// Turbo Tier 5, issue #509: 'inherited [Method[(args)]]' used as a
    /// VALUE -- the CGFuncCall sibling of CGProcCall::emitInheritedCallStmt
    /// (see its own comment, CGProcCall.h, for the whole design): a STATIC
    /// call (never through the VMT) to the mangled symbol Sema::
    /// checkInheritedCall already resolved (InheritedCallExpr::
    /// ImplementingType/ResolvedMethod), with the CURRENTLY EXECUTING
    /// function's own Self argument forwarded as this call's own Self,
    /// unchanged, and this call's own result returned rather than
    /// discarded -- including the same string/ShortString return-value
    /// spill emitMethodCallExpr/emitUserFuncCall perform just above/below,
    /// since an inherited FUNCTION returning one is exactly as reachable
    /// here as through an ordinary call.
    llvm::Value* emitInheritedCallExpr(const plang::InheritedCallExpr& e);

    /// The built-in dispatch chain that used to be emitCallExpr's whole body,
    /// factored out so a call site with no CallExpr of its own -- Turbo
    /// `{$X+}`'s "a built-in function may be called as a statement, its
    /// result discarded" -- can still reach it.  CGProcCall::emitCallStmt's
    /// tail (a builtin call that matched none of the required-PROCEDURE
    /// names it dispatches by spelling) is the one other caller: every
    /// Proc-kind row in Builtins.def already has its own named arm there, so
    /// reaching that tail with ResolvedBuiltin already known non-None means
    /// this, a builtin FUNCTION, and Sema having allowed it through only
    /// under {$X+}.  Args is a span, not an owned vector, so CGProcCall can
    /// pass CallStmt's own Args straight through with nothing to move or
    /// copy -- s.Args is not `mutable`, and this project's convention
    /// reserves const_cast-around-constness for fields that ARE (see
    /// ExprNode::ResolvedType's own comment).
    ///
    /// Returns nullptr only for a builtin name every arm below fails to
    /// match, which Builtins.def and this dispatch's own completeness
    /// should make unreachable -- emitCallExpr still falls back to
    /// emitUserFuncCall the same way it always has, and CGProcCall's own
    /// caller has no such fallback and reports a codegen ICE instead.
    llvm::Value* emitBuiltinCall(const std::string& Name,
                                  std::span<const std::unique_ptr<plang::ExprNode>> Args,
                                  plang::SourceLocation Loc);

private:
    /// Turbo Tier 4, Cluster C item 6: CGProcCall::tryEmitDosProcCall's own
    /// twin for GetEnv, Dos.pas's only Func-kind export needing this
    /// treatment.  Returns nullptr (having emitted nothing) unless the
    /// call resolved to Dos's own GetEnv -- see that method's own comment
    /// (CGProcCall.h) for the full rationale, shared unchanged here.
    llvm::Value* tryEmitDosFuncCall(const plang::CallExpr& e);

    /// Turbo Tier 5, issues #571/#623: emitMethodCallExpr's own core --
    /// mangled-symbol resolution, argument marshalling, and the virtual/
    /// VMT-vs-direct dispatch choice -- factored out so
    /// emitImplicitMethodCallExpr's own CallExpr::ImplicitMethodReceiverType
    /// branch can reuse it with a receiver that has no real Receiver
    /// ExprNode of its own (an unqualified call resolving to the enclosing
    /// method's own Self or an active with-block's own object, looked up
    /// under a reserved CGSymbolTable name instead -- see
    /// implicitCallReceiverVarName, CGSymbolTable.h).  \p RecvTy is the
    /// STATIC type \p selfPtr's storage was laid out as.  Same fresh-copy
    /// convention as this file's own methodOwnerType/methodEntryOf helpers
    /// (matching CGProcCall::emitBoundMethodCall's identical name/shape,
    /// its own copy for CGProcCall.cpp's statement-context call sites) --
    /// mirrors, not calls, that one: the two classes share no reference to
    /// each other.  Returns the call's own (not yet struct-return-spilled)
    /// result.
    llvm::Value* emitBoundMethodCall(llvm::Value* selfPtr, const plang::Type& RecvTy,
                                     const std::string& Method,
                                     std::span<const std::unique_ptr<plang::ExprNode>> Args);

    llvm::LLVMContext& Ctx;
    llvm::Module& Mod;
    llvm::IRBuilder<>& B;
    RuntimeFunctionCache& RtFns;
    SetOps& Sets;
    ComplexOps& Complex;
    FileVarHelpers& FileVars;
    CGTypes& Types;
    SchemaAccess& Schema;
    StringRuntime& Strings;
    StringCallMarshalling& StrCall;
    CGLinkage& Linkage;
    CGSymbolTable& SymTab;
    ClosureAndCallABI& ClosureAbi;
    RangeCheckGuards& RangeGuards;
    /// Issue #299 Phase 1: the per-argument marshalling loop shared with
    /// CGProcCall::emitUserProcCall -- see CGCallMarshal.h.
    CGCallMarshal& Marshal;
    llvm::IntegerType* I64Ty;
    llvm::IntegerType* I8Ty;
    llvm::Type* DblTy;
    llvm::PointerType* PtrTy;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue;
    std::function<llvm::Value*(llvm::Value*)> ToDouble;
    /// The bool is the operand's actual Sema-resolved Type::IsSigned; see
    /// CGBinaryOps.h's identical member for the fuller comment.  Every
    /// built-in this file lowers with an ordinal argument (Abs/Sqr/Odd/Chr/
    /// Succ/Pred/Copy/StringOfChar/...) passes exprIsSigned(x)
    /// (OrdinalSignedness.h) for whatever ExprNode x the value came from.
    std::function<llvm::Value*(llvm::Value*, bool)> ToI64;
    std::function<llvm::Value*(llvm::Value*)> EnsureI1;
    std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca;
    std::function<llvm::Value*(llvm::Value*, const std::string&)> CreateDynStrAlloca;
    std::function<llvm::Value*(const std::string&)> BuildStaticLinkFrame;
    std::function<bool(const plang::ExprNode&)> ExprIsVarStr;
    std::function<bool(const plang::ExprNode&)> ExprIsCharStr;
    std::function<int64_t(const plang::ExprNode&)> ExprCharStrLen;
    std::function<int64_t(const plang::ExprNode&)> ExprStrCapStatic;
    /// Turbo string[N]'s own predicate -- see exprIsShortStr's doc comment
    /// (CodeGenImpl.h).  Used only by emitUserFuncCall's struct-return spill;
    /// the EP-only builtin string functions (Length/Substr/Trim/EQ and
    /// friends, all gated `EP` in Builtins.def) never see a ShortString
    /// argument at all, so they need no ShortString capacity query to match
    /// ExprStrCapStatic's VarString one.
    std::function<bool(const plang::ExprNode&)> ExprIsShortStr;
    /// Turbo string[N]'s own capacity query -- the ShortString sibling of
    /// ExprStrCapStatic, needed by the Turbo System-unit string routines
    /// (Copy/Pos/Concat/StringOfChar/UpCase, CGFuncCall.cpp) to size a
    /// result temporary or shape a ShortString operand the same way
    /// CGBinaryOps' own local sstrOperand lambda already does for `+`/
    /// comparison.  See exprShortStrCap's own doc comment (CodeGenImpl.h).
    std::function<int64_t(const plang::ExprNode&)> ExprShortStrCap;
    /// Turbo Tier 5, Cluster A item 7 (issue #508 fix): TypeOf(x)'s own
    /// lowering -- reuses Codegen::Impl::getOrCreateVmt's existing per-type
    /// VMT global exactly as-is (Sema already refused any argument whose
    /// type has no VMT to build), rather than duplicating its
    /// memoized-global-lookup logic a second time the way
    /// CGWith.cpp/CGFieldAccess.cpp's own object-field GEP walk had to
    /// (getOrCreateVmt lives on a different class, Impl, with no
    /// bytes-and-instructions duplication possible here the way there was
    /// for that -- this is a single call, not an algorithm). Since the
    /// #508 fix, only TypeOf's bare-TYPE-NAME shape (TypeOf(TDog), no
    /// runtime instance to read a `_vptr` from) still goes through this --
    /// TypeOf(x) for a value expression now reads x's own runtime `_vptr`
    /// instead (CGFuncCall.cpp's "typeof.vptr.addr"/"typeof.vmt").
    std::function<llvm::GlobalVariable*(const plang::Type&)> GetOrCreateVmt;
    /// Issue #682: see Codegen::Impl::declareForeignInheritedCallee's own
    /// comment (CodeGenImpl.h) -- emitInheritedCallExpr's explicit-form
    /// "not yet declared in this translation unit" fallback bridges
    /// through here rather than reaching Impl::paramMeta_ directly (which
    /// this class, unlike Impl itself, has no access to).
    std::function<llvm::Function*(const plang::Type::Method&,
                                  const std::string&)> DeclareForeignInheritedCallee;
    /// Issue #622: New used as a FUNCTION with a constructor -- 'p :=
    /// New(PtrType, Ctor[(args)])' (Args.size() > 1; the bare 'New(PtrType)'
    /// form is handled directly in this file's own 'new' arm instead, since
    /// -- like the statement form's own bare 'new(p)' -- it must NOT stamp a
    /// '_vptr', issue #514's own policy).  Bridges to CGProcCall::
    /// emitNewObjectValue (its own comment, CGProcCall.h, has the whole
    /// design) exactly the way GetOrCreateVmt/DeclareForeignInheritedCallee
    /// just above bridge to Impl-level logic this class has no direct
    /// access to -- here, StampVptr/StampFieldVptrs/emitBoundMethodCall, all
    /// private to CGProcCall.  The second argument is *Args[1].
    std::function<llvm::Value*(const plang::Type&,
                               const plang::ExprNode&)> EmitNewObjectValue;

    llvm::Constant* i64c(int64_t v) const {
        return llvm::ConstantInt::get(I64Ty, v, true);
    }
};
