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
#include "OrdinalSignedness.h"
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
              std::function<llvm::Value*(llvm::Value*, bool)> ToI64,
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
    /// TP-only: Str(x [: width [: decimals]], var s) -- formats x (args[0],
    /// which may carry a WriteParam width/decimals the same way write's own
    /// arguments do -- see ParseStmt.cpp's parseWriteArg) into ShortString
    /// destination s (args[1]), reusing the same writestr capture machinery
    /// emitBuiltinWriteStr does, just with x written FIRST (not a
    /// destination-first argument order) and a ShortString (one-byte header)
    /// destination rather than writestr's EP one.
    void emitBuiltinStr(const std::vector<std::unique_ptr<plang::ExprNode>>& args);

private:
    /// \p end bounds the range of args formatted, defaulting to the whole
    /// vector (SIZE_MAX, clamped to args.size()) -- write/writeln/writestr's
    /// own calls below are all "format everything from start to the end of
    /// the argument list" and never pass it, so only emitBuiltinStr (which
    /// must format ONLY args[0], never args[1], the destination) needs to
    /// narrow it.
    void emitWriteArgs(const std::vector<std::unique_ptr<plang::ExprNode>>& args, size_t start,
                        bool newline, llvm::Value* fp, bool binaryTyped,
                        size_t end = SIZE_MAX);
    void emitWriteValue(llvm::Value* val, bool newline, llvm::Value* fp = nullptr,
                         const plang::Type* semaTy = nullptr);
    void emitWriteValueFormatted(llvm::Value* val, llvm::Value* w, llvm::Value* d,
                                  bool newline, llvm::Value* fp,
                                  const plang::Type* semaTy = nullptr);
    static std::string readFnSuffix(llvm::Type* ty, const plang::Type* semaTy);
    void emitReadArg(const plang::ExprNode& arg, llvm::Value* fp);
    void emitSkipLine(llvm::Value* fp);
    /// Emits the trailing plang_writeln_file(fp) call every write(f,...)/
    /// writeln(f,...) value (and a bare writeln(f)) needs -- dispatches to
    /// the `_turbo` sibling (runtime/plang_file.cpp) under -std=turbo, this
    /// item's P7-rule choke point, instead of inlining the same ternary at
    /// each of this file's several call sites.
    void emitWritelnFile(llvm::Value* fp);
    /// -std=turbo only: the storage pointer for the predefined Input/Output
    /// Var (Sema::registerBuiltins), or null under any other dialect or if
    /// somehow unregistered.  emitBuiltinWrite/Read/Readln's own "no
    /// explicit file argument" case resolves fp through this under Turbo,
    /// rather than leaving fp null to mean "write straight to the console"
    /// the way ISO/EP's identical case still does (Opts.turbo() guards it
    /// off for both) -- see this method's call sites for the reasoning.
    llvm::Value* turboStdFilePtr(bool isInput) const;
    /// A file-directed runtime function's base (ISO/EP) name, resolved to its
    /// `_turbo`-suffixed sibling under -std=turbo -- the one-line version of
    /// emitWritelnFile's own dispatch, for call sites that build the rest of
    /// their own CreateCall by hand instead of going through a shared helper.
    std::string fileFn(const std::string& base) const {
        return Opts.turbo() ? base + "_turbo" : base;
    }

    /// Whether a value should be written as 'true'/'false'.  A boolean is
    /// normally i1, but the predefined TimeStamp holds its two flags as i8 so
    /// that the record matches its C counterpart byte for byte, and at that
    /// width nothing in the IR distinguishes a boolean from a char — only the
    /// Pascal type does.  i16/i32 are Turbo's loose WordBool/LongBool
    /// (Type::IsLooseBool); semaTy's own Kind is what tells those apart from
    /// an equally-wide Word/LongInt.
    static bool writesAsBoolean(const llvm::Type* ty, const plang::Type* semaTy);
    /// Normalizes any Boolean-shaped value to the single byte
    /// plang_write(ln)_bool(_w) expects.  i1 (strict Boolean) zero-extends
    /// directly, and an already-i8 value (ByteBool) is passed through as-is
    /// -- plang_write_bool does its own C truthiness test on the raw byte,
    /// so a ByteBool holding a non-canonical value like 200 still prints
    /// TRUE.  A WIDER loose Boolean (WordBool/LongBool, i16/i32) is NOT
    /// simply truncated to its low byte first: a genuinely nonzero value
    /// whose low byte happens to be zero (0x100, say) would truncate to a
    /// zero byte and misprint FALSE, so the truth test (nonzero-or-not) has
    /// to run at the value's own full width before narrowing to a byte.
    llvm::Value* toBoolByte(llvm::Value* val) const;
    /// Whether a value should be written as a character.  A char is normally
    /// i8, but a subrange of char is held in an integer-width slot, and at
    /// that width only the Pascal type says it is not a number.
    static bool writesAsChar(const llvm::Type* ty, const plang::Type* semaTy);
    /// Whether an i64-wide value must be formatted as unsigned decimal
    /// (Turbo's QWord) rather than the ordinary signed i64 writer every other
    /// ordinal that reaches an isIntegerTy(64) dispatch site uses.
    static bool writesAsUnsigned64(const plang::Type* semaTy);

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
    /// The bool is the operand's actual Sema-resolved Type::IsSigned; see
    /// CGBinaryOps.h's identical member for the fuller comment.  Used for
    /// a write-parameter's Width/Decimals (`write(x:w:d)`) and for the
    /// read-back range check on a subrange-typed read() target, each with
    /// exprIsSigned(x) (OrdinalSignedness.h) for its own ExprNode.
    std::function<llvm::Value*(llvm::Value*, bool)> ToI64;
    /// CoerceToType is 2-arg (no operand-type context), unlike the sibling
    /// ToI64 just above: every call site here is either explicitly gated
    /// `!Opts.turbo()` (ISO/EP's own Integer is always 64-bit signed, so
    /// the pre-ladder LLVM-width guess this falls back to cannot disagree
    /// with a real operand type -- see emitWriteArgs/emitBuiltinRead's own
    /// comments) or narrows/holds steady from an always-i64 runtime-loaded
    /// source (emitReadArg's own comment), for which the guess is exact
    /// regardless of signedness either way (issue #177's sibling audit
    /// checked all three and found no live bug).
    std::function<llvm::Value*(llvm::Value*, llvm::Type*)> CoerceToType;
    std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca;
    std::function<bool(const plang::ExprNode&)> ExprIsVarStr;
    std::function<bool(const plang::ExprNode&)> ExprIsCharStr;
    std::function<int64_t(const plang::ExprNode&)> ExprCharStrLen;
};
