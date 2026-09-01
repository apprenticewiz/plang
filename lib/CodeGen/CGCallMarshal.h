// CGCallMarshal.h — the per-argument marshalling loop shared by every call
// site keyed by (mangledName, astArgIdx)-shaped lookups (issue #299):
// CGProcCall::emitUserProcCall, CGFuncCall::emitUserFuncCall,
// CGFuncCall::emitMethodCallExpr (Phase 1), and
// ClosureAndCallABI::emitProcParamCall (Phase 2) used to each carry their
// own byte-for-byte copy of "how do I marshal one Pascal-formal-shaped
// actual argument" -- the same ProcParamArg/pushSchemaArgs/
// pushConformantArgs/plain-value dispatch chain.  Extracted here so there
// is exactly ONE implementation of that chain, not four.
//
// The five lookup callbacks (ProcParamArg/ParamIsByRef/ConformantDimsOf/
// ParamSetBaseOf/SchemaArgDiscsOf) are the whole reason this generalizes to
// emitProcParamCall too: the three direct-call sites bind them to a real
// paramMeta_ entry keyed by a mangled callee name, but emitProcParamCall has
// no such name -- it builds a transient, per-call vector of the same facts
// from its own ProcedureTypeNode (already in hand, its own declared
// signature) and binds these same callbacks to read THAT vector by
// astArgIdx instead, ignoring the mangledName parameter entirely.  Nothing
// else in marshalArgs needs to know which kind of lookup is behind them.
//
// calleeTy is an llvm::FunctionType*, not an llvm::Function*: the one piece
// of callee shape marshalArgs itself needs (a plain value argument's
// destination LLVM parameter type, via getParamType) is available as a bare
// signature, which is what emitProcParamCall's indirect call through a
// {entry point, frame} pair has on hand -- there is no llvm::Function* to
// give it, since the actual target is not known until runtime.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "llvm/IR/IRBuilder.h"

namespace llvm { class Function; class FunctionType; class LLVMContext; class Value; class AllocaInst; }
namespace plang { struct ExprNode; struct ProcedureTypeNode; }

class ClosureAndCallABI;
class SchemaAccess;
class SetOps;
class StringCallMarshalling;

class CGCallMarshal {
public:
    CGCallMarshal(llvm::IRBuilder<>& B, ClosureAndCallABI& ClosureAbi,
                  SchemaAccess& Schema, SetOps& Sets, StringCallMarshalling& StrCall,
                  std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca,
                  std::function<const plang::ProcedureTypeNode*(const std::string&, size_t)> ProcParamArg,
                  std::function<bool(const std::string&, size_t)> ParamIsByRef,
                  std::function<size_t(const std::string&, size_t)> ConformantDimsOf,
                  std::function<std::optional<int64_t>(const std::string&, size_t)> ParamSetBaseOf,
                  std::function<unsigned(const std::string&, size_t)> SchemaArgDiscsOf,
                  std::function<bool(const plang::ExprNode&)> ExprIsVarStr,
                  std::function<bool(const plang::ExprNode&)> ExprIsShortStr)
        : B(B), ClosureAbi(ClosureAbi), Schema(Schema), Sets(Sets), StrCall(StrCall),
          CreateEntryAlloca(std::move(CreateEntryAlloca)),
          ProcParamArg(std::move(ProcParamArg)), ParamIsByRef(std::move(ParamIsByRef)),
          ConformantDimsOf(std::move(ConformantDimsOf)),
          ParamSetBaseOf(std::move(ParamSetBaseOf)),
          SchemaArgDiscsOf(std::move(SchemaArgDiscsOf)),
          ExprIsVarStr(std::move(ExprIsVarStr)), ExprIsShortStr(std::move(ExprIsShortStr)) {}

    /// Marshals \p Args (the call's own written argument list, in AST order)
    /// into \p args (the LLVM actual-argument list being built for a call
    /// through \p calleeTy, named \p mangledName for the ProcParamArg/
    /// SchemaArgDiscsOf/ConformantDimsOf/ParamSetBaseOf/ParamIsByRef lookups
    /// below) -- appending one or more llvm::Value*s per Pascal-level
    /// actual, exactly mirroring whatever shape \p mangledName's
    /// astArgIdx-th formal has: a procedural parameter (entry point plus
    /// frame), a schema parameter (body pointer plus discriminants), a
    /// conformant-array parameter (data pointer plus two bounds words per
    /// dimension), or an ordinary value/var parameter (a single value,
    /// set-rebased through SetOps::alignSetArg where relevant).
    ///
    /// \p args may already hold a leading static-link frame or Self pointer
    /// -- the loop starts counting LLVM parameter positions (for the
    /// calleeTy->getParamType(pi) probe plain values use to pick a
    /// destination type) from args.size() as it stands on entry, exactly as
    /// each of the original call sites did before this was factored out.
    void marshalArgs(const std::string& mangledName, llvm::FunctionType* calleeTy,
                      std::span<const std::unique_ptr<plang::ExprNode>> Args,
                      std::vector<llvm::Value*>& args) const;

    /// A string/ShortString RESULT comes back from \p ret as the whole
    /// struct-shaped value ({length,bytes} or the packed ShortString
    /// layout), but every consumer of a string expression expects an
    /// address instead.  Spills \p ret into a fresh entry-block temporary
    /// and returns its address whenever \p e's own static type calls for
    /// that treatment (ExprIsVarStr/ExprIsShortStr) AND \p ret is in fact
    /// struct-shaped (a call declared to return one of these but reached
    /// through an external/foreign declaration with a differently-shaped
    /// LLVM type would not be); returns \p ret unchanged otherwise.  Shared
    /// by CGFuncCall::emitUserFuncCall and CGFuncCall::emitMethodCallExpr,
    /// which used to each carry their own byte-for-byte copy of this tail.
    llvm::Value* spillStructReturnIfNeeded(const plang::ExprNode& e, llvm::Value* ret) const;

private:
    llvm::IRBuilder<>& B;
    ClosureAndCallABI& ClosureAbi;
    SchemaAccess& Schema;
    SetOps& Sets;
    StringCallMarshalling& StrCall;
    std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca;
    std::function<const plang::ProcedureTypeNode*(const std::string&, size_t)> ProcParamArg;
    std::function<bool(const std::string&, size_t)> ParamIsByRef;
    std::function<size_t(const std::string&, size_t)> ConformantDimsOf;
    std::function<std::optional<int64_t>(const std::string&, size_t)> ParamSetBaseOf;
    std::function<unsigned(const std::string&, size_t)> SchemaArgDiscsOf;
    std::function<bool(const plang::ExprNode&)> ExprIsVarStr;
    std::function<bool(const plang::ExprNode&)> ExprIsShortStr;
};
