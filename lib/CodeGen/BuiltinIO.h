// BuiltinIO.h — ISO §6.9/EP §6.7.5.5: the write/writeln/read/readln builtin
// procedures, and the string-transfer procedures (writestr/readstr) that
// bracket the same write/read lowering with a runtime redirect onto a
// memory buffer instead of a file.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

#include "plang/Basic/LangOptions.h"

#include "CGSymbolTable.h"
#include "CGTypes.h"
#include "ComplexOps.h"
#include "FileVarHelpers.h"
#include "RangeCheckGuards.h"
#include "RuntimeFunctionCache.h"
#include "SchemaAccess.h"
#include "StringCallMarshalling.h"
#include "StringRuntime.h"

namespace llvm { class Module; class Value; class AllocaInst; }
namespace plang { struct ExprNode; struct Type; }

class BuiltinIO {
public:
    BuiltinIO(llvm::LLVMContext& Ctx, llvm::Module& Mod, llvm::IRBuilder<>& B,
              FileVarHelpers& FileVars, RuntimeFunctionCache& RtFns,
              StringRuntime& Strings, SchemaAccess& Schema,
              StringCallMarshalling& StrCall, ComplexOps& Complex,
              CGSymbolTable& SymTab, RangeCheckGuards& RangeGuards, CGTypes& Types,
              const plang::LangOptions& Opts,
              llvm::IntegerType* I8Ty, llvm::IntegerType* I64Ty,
              llvm::Type* DblTy, llvm::PointerType* PtrTy,
              std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr,
              std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue,
              std::function<llvm::Value*(llvm::Value*)> ToI64,
              std::function<llvm::Value*(llvm::Value*, llvm::Type*)> CoerceToType,
              std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca,
              std::function<bool(const plang::ExprNode&)> ExprIsVarStr,
              std::function<bool(const plang::ExprNode&)> ExprIsCharStr,
              std::function<int64_t(const plang::ExprNode&)> ExprCharStrLen)
        : Ctx(Ctx), Mod(Mod), B(B), FileVars(FileVars), RtFns(RtFns),
          Strings(Strings), Schema(Schema), StrCall(StrCall), Complex(Complex),
          SymTab(SymTab), RangeGuards(RangeGuards), Types(Types), Opts(Opts),
          I8Ty(I8Ty), I64Ty(I64Ty), DblTy(DblTy), PtrTy(PtrTy),
          EmitExpr(std::move(EmitExpr)), EmitLValue(std::move(EmitLValue)),
          ToI64(std::move(ToI64)), CoerceToType(std::move(CoerceToType)),
          CreateEntryAlloca(std::move(CreateEntryAlloca)),
          ExprIsVarStr(std::move(ExprIsVarStr)), ExprIsCharStr(std::move(ExprIsCharStr)),
          ExprCharStrLen(std::move(ExprCharStrLen)) {}

    void emitBuiltinWrite(const std::vector<std::unique_ptr<plang::ExprNode>>& args, bool newline);
    void emitBuiltinRead(const std::vector<std::unique_ptr<plang::ExprNode>>& args);
    void emitBuiltinReadln(const std::vector<std::unique_ptr<plang::ExprNode>>& args);
    void emitBuiltinWriteStr(const std::vector<std::unique_ptr<plang::ExprNode>>& args);
    void emitBuiltinReadStr(const std::vector<std::unique_ptr<plang::ExprNode>>& args);

private:
    void emitWriteArgs(const std::vector<std::unique_ptr<plang::ExprNode>>& args, size_t start,
                        bool newline, llvm::Value* fp, bool binaryTyped);
    void emitWriteValue(llvm::Value* val, bool newline, llvm::Value* fp = nullptr,
                         const plang::Type* semaTy = nullptr);
    void emitWriteValueFormatted(llvm::Value* val, llvm::Value* w, llvm::Value* d,
                                  bool newline, llvm::Value* fp,
                                  const plang::Type* semaTy = nullptr);
    static std::string readFnSuffix(llvm::Type* ty);
    void emitReadArg(const plang::ExprNode& arg, llvm::Value* fp);
    void emitSkipLine(llvm::Value* fp);

    /// Whether a value should be written as 'true'/'false'.  A boolean is
    /// normally i1, but the predefined TimeStamp holds its two flags as i8 so
    /// that the record matches its C counterpart byte for byte, and at that
    /// width nothing in the IR distinguishes a boolean from a char — only the
    /// Pascal type does.
    static bool writesAsBoolean(const llvm::Type* ty, const plang::Type* semaTy);
    /// Whether a value should be written as a character.  A char is normally
    /// i8, but a subrange of char is held in an integer-width slot, and at
    /// that width only the Pascal type says it is not a number.
    static bool writesAsChar(const llvm::Type* ty, const plang::Type* semaTy);

    llvm::Constant* i64c(int64_t v) const {
        return llvm::ConstantInt::get(I64Ty, static_cast<uint64_t>(v), true);
    }

    /// The single CodeGen-resolved fact every Turbo reversal in this file
    /// reduces to (uppercase bool spelling, non-truncating field widths, a
    /// zero-width char that still writes, the Turbo real-format profile):
    /// whether the active dialect is Turbo, as a plain i8 constant threaded
    /// into the runtime call as an extra argument -- never a runtime-side
    /// dialect check (see RangeCheckGuards' own isTurbo()/emitTpRunError for
    /// the established precedent this follows: the runtime cannot hold a
    /// "which dialect" global, since an ISO object and a Turbo one can be
    /// linked into the same program).  A fresh call returns the same pooled
    /// llvm::Constant every time (LLVMContext interns ConstantInt by value),
    /// so there is no cost to calling it at each of the several sites below
    /// that need it instead of caching one Value*.
    llvm::Constant* turboFlag() const {
        return llvm::ConstantInt::get(I8Ty, Opts.turbo() ? 1 : 0);
    }

    llvm::LLVMContext& Ctx;
    llvm::Module& Mod;
    llvm::IRBuilder<>& B;
    FileVarHelpers& FileVars;
    RuntimeFunctionCache& RtFns;
    StringRuntime& Strings;
    SchemaAccess& Schema;
    StringCallMarshalling& StrCall;
    ComplexOps& Complex;
    CGSymbolTable& SymTab;
    RangeCheckGuards& RangeGuards;
    CGTypes& Types;
    const plang::LangOptions& Opts;
    llvm::IntegerType* I8Ty;
    llvm::IntegerType* I64Ty;
    llvm::Type* DblTy;
    llvm::PointerType* PtrTy;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue;
    std::function<llvm::Value*(llvm::Value*)> ToI64;
    std::function<llvm::Value*(llvm::Value*, llvm::Type*)> CoerceToType;
    std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca;
    std::function<bool(const plang::ExprNode&)> ExprIsVarStr;
    std::function<bool(const plang::ExprNode&)> ExprIsCharStr;
    std::function<int64_t(const plang::ExprNode&)> ExprCharStrLen;
};
