// StringCallMarshalling.h — call-argument marshalling (ISO §6.6.3.2) and
// the EP string-store/address operations it and everyday string
// assignment both rest on.
//
// Mutually recursive with itself (emitCallArg -> emitStrStore ->
// emitCharStrAsStr, emitCharStrStore -> emitStrAddr), which is why these
// five stay one unit rather than splitting further.
#pragma once

#include <cstdint>
#include <functional>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

#include "CGTypes.h"
#include "RangeCheckGuards.h"
#include "RuntimeFunctionCache.h"
#include "SchemaAccess.h"
#include "StringRuntime.h"

namespace llvm {
class Value;
class AllocaInst;
}

namespace plang {
struct ExprNode;
}

class StringCallMarshalling {
public:
    StringCallMarshalling(
        llvm::LLVMContext& Ctx, llvm::IRBuilder<>& B,
        StringRuntime& Strings, RangeCheckGuards& RangeGuards,
        RuntimeFunctionCache& RtFns, CGTypes& Types, SchemaAccess& Schema,
        llvm::IntegerType* I64Ty, llvm::PointerType* PtrTy,
        std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr,
        std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue,
        std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca,
        std::function<llvm::Value*(llvm::Value*, const std::string&)> CreateDynAlloca,
        std::function<llvm::Value*(llvm::Value*, llvm::Type*, bool)> CoerceToType,
        std::function<bool(const plang::ExprNode&)> ExprIsCharStr,
        std::function<bool(const plang::ExprNode&)> ExprIsVarStr,
        std::function<int64_t(const plang::ExprNode&)> ExprCharStrLen,
        std::function<int64_t(const plang::ExprNode&)> ExprStrCap,
        std::function<bool(const plang::ExprNode&)> ExprIsShortStr,
        std::function<int64_t(const plang::ExprNode&)> ExprShortStrCap)
        : Ctx(Ctx), B(B), Strings(Strings), RangeGuards(RangeGuards),
          RtFns(RtFns), Types(Types), Schema(Schema), I64Ty(I64Ty), PtrTy(PtrTy),
          EmitExpr(std::move(EmitExpr)), EmitLValue(std::move(EmitLValue)),
          CreateEntryAlloca(std::move(CreateEntryAlloca)),
          CreateDynAlloca(std::move(CreateDynAlloca)),
          CoerceToType(std::move(CoerceToType)),
          ExprIsCharStr(std::move(ExprIsCharStr)),
          ExprIsVarStr(std::move(ExprIsVarStr)),
          ExprCharStrLen(std::move(ExprCharStrLen)),
          ExprStrCap(std::move(ExprStrCap)),
          ExprIsShortStr(std::move(ExprIsShortStr)),
          ExprShortStrCap(std::move(ExprShortStrCap)) {}

    /// One argument of a call to a user-declared procedure or function,
    /// given the LLVM type the callee declared for that position: an
    /// address for a var parameter, a copy for a string, the value
    /// otherwise.  \p byRef says the formal is a variable parameter, which
    /// the LLVM type cannot: a value parameter of pointer type is declared
    /// `ptr` there as well.
    llvm::Value* emitCallArg(const plang::ExprNode& arg, llvm::Type* paramTy,
                             bool byRef);
    /// The address of the { length, bytes } struct a string expression
    /// denotes, which is what every string runtime entry point takes.
    llvm::Value* emitStrAddr(const plang::ExprNode& e);
    /// A NUL-terminated C string for a call argument whose runtime
    /// signature is `const char *` rather than the { length, bytes } ABI --
    /// EP §6.7.5.2's optional reset/rewrite/extend/update file name is the
    /// one user today.  A string(n) value has no terminator of its own (only
    /// a length field in front of its bytes), so it is copied into one here;
    /// a char value (a one-character file name, e.g. `update(f, 'x')`) is
    /// widened into a one-byte-plus-NUL buffer the same way; anything else (a
    /// plain string literal outside Extended Pascal, or an already-null-
    /// terminated `String`) already IS a `char *` and is returned unchanged.
    llvm::Value* emitCStrArg(const plang::ExprNode& e);
    /// Wraps a §6.4.3.2 char-array value as a temporary `string(n)` struct.
    llvm::Value* emitCharStrAsStr(const plang::ExprNode& e);
    /// Stores \p src into the fixed-\p n-byte char-array at \p dst.
    void emitCharStrStore(llvm::Value* dst, int64_t n, const plang::ExprNode& src);
    /// Store \p src into the string variable at \p dst, whose capacity is
    /// \p capDst.  A string is a length and a buffer, so which runtime call
    /// this takes depends on what the source is; assignment and the
    /// 'value' initializer both come through here.
    void emitStrStore(llvm::Value* dst, llvm::Value* capDst, const plang::ExprNode& src);
    /// Turbo string[N]'s own sibling of emitStrStore just above -- stores
    /// \p src (a ShortString, a char, or a plain literal/String) into the
    /// ShortString variable at \p dst via plang_sstr_* (TRUNCATING, never
    /// erroring; see plang_sstr.cpp), never plang_str_*.  A separate
    /// function rather than a branch inside emitStrStore: the two runtimes'
    /// struct layouts are incompatible, and every call site must pick one
    /// or the other explicitly rather than have a single function decide
    /// which shape \p dst actually has.
    void emitSstrStore(llvm::Value* dst, llvm::Value* capDst, const plang::ExprNode& src);
    /// The raw bytes address behind a char-string LITERAL actual passed to a
    /// packed conformant-array-of-char formal (ISO 7185 §6.4.3.2/§6.6.3.6.2,
    /// issue #687) -- the interned constant backing the literal, with no
    /// length prefix the way emitCallArg's general VarString path would
    /// build one.  A dedicated entry point rather than routing this through
    /// the general-purpose EmitLValue: several OTHER callers of EmitLValue
    /// (e.g. emitBuiltinReadStr, BuiltinIO.cpp) treat a null result as "try
    /// EmitExpr instead" for a StringLitExpr, and having EmitLValue itself
    /// answer for one there instead broke every one of those call sites --
    /// caught by CodeGen/TextFiles/a-long-numeric-literal-does-not-truncate
    /// .pas, whose readstr(literal, realVar) silently misread the literal's
    /// own bytes as a VarString struct's length field once EmitLValue no
    /// longer returned null for it.  Only pushConformantArgs
    /// (ClosureAndCallABI.cpp) is meant to see this one.
    llvm::Value* charLiteralDataPtr(const plang::ExprNode& e) const;

private:
    llvm::LLVMContext& Ctx;
    llvm::IRBuilder<>& B;
    StringRuntime& Strings;
    RangeCheckGuards& RangeGuards;
    RuntimeFunctionCache& RtFns;
    CGTypes& Types;
    SchemaAccess& Schema;
    llvm::IntegerType* I64Ty;
    llvm::PointerType* PtrTy;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue;
    std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca;
    std::function<llvm::Value*(llvm::Value*, const std::string&)> CreateDynAlloca;
    /// The bool is the source operand's actual Sema-resolved Type::IsSigned
    /// -- CGBinaryOps.h's identical CoerceToType member has the fuller
    /// version of this comment.  Upgraded from a 2-arg (no operand-type
    /// context) bridge to this 3-arg one in issue #177's own fix: emitCallArg
    /// is what threads a value-parameter actual into its formal's declared
    /// width, and a signed narrow (or unsigned wide) Turbo-ordinal actual
    /// reaches it with no other coercion in between.
    std::function<llvm::Value*(llvm::Value*, llvm::Type*, bool)> CoerceToType;
    /// Stateless string-shape predicates -- static Impl methods used far
    /// outside this unit too, so they stay put; reached via closure rather
    /// than a qualified call, which would need this file to see all of
    /// Impl.  Same treatment SchemaAccess already gives these same three.
    std::function<bool(const plang::ExprNode&)> ExprIsCharStr;
    std::function<bool(const plang::ExprNode&)> ExprIsVarStr;
    std::function<int64_t(const plang::ExprNode&)> ExprCharStrLen;
    std::function<int64_t(const plang::ExprNode&)> ExprStrCap;
    /// Turbo string[N]'s own predicate/capacity pair -- see exprIsShortStr's
    /// doc comment (CodeGenImpl.h).
    std::function<bool(const plang::ExprNode&)> ExprIsShortStr;
    std::function<int64_t(const plang::ExprNode&)> ExprShortStrCap;

    llvm::Constant* i64c(int64_t v) const {
        return llvm::ConstantInt::get(I64Ty, static_cast<uint64_t>(v), true);
    }
};
