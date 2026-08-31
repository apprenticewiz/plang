/// plang -pc1 — Pascal compiler front end
///
/// Implements plang::frontendPC1Main(), compiled into the shared library
/// libplang-frontend.
/// The plang driver re-invokes itself as "plang -pc1 ..." to run the front
/// end as a subprocess; embedders can also call frontendPC1Main() directly.
///
/// Argument layout:
///   argv[0]  = "plang"
///   argv[1]  = "-pc1"
///   argv[2…] = front-end options and the source file

#include "plang/Frontend/Frontend.h"
#include "plang/Basic/MessageCatalog.h"
#include "plang/Basic/Version.h"

#include "plang/AST/AstPrinter.h"
#include "plang/AST/Ast.h"
#include "plang/CodeGen/CodeGen.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Driver/Options.h"
#include "plang/Basic/DiagnosticPrinter.h"
#include "plang/Basic/SourceManager.h"
#include "plang/Basic/LangOptions.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Basic/UnitSearchPath.h"
#include "plang/Parse/Parser.h"
#include "plang/Lex/Scanner.h"
#include "plang/Sema/DumpVmt.h"
#include "plang/Sema/Sema.h"

#include <charconv>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <print>
#include <set>
#include <string>
#include <string_view>
#include <unistd.h>

#include "llvm/Support/Casting.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

namespace plang {

// See NumExprKinds in AstBase.h.  The interface writer takes the two together:
// a kind exprToString learns to write has to be one canSerializeExpr agrees can
// be written, or the gate keeps out what the writer could have said, and a kind
// only the gate learns lets through what the writer will then write as a zero.
static_assert(NumExprKinds == 19,
              "a new expression may need a case in exprToString and in "
              "canSerializeExpr");
static_assert(NumTypeKinds == 15,
              "a new type denoter needs a case in typeDenoterToString");

namespace {


std::string findInstallDir(const char *Argv0) {
    // getMainExecutable uses /proc/self/exe (Linux), _NSGetExecutablePath (macOS),
    // or GetModuleFileName (Windows) — no /proc dependency.
    std::string ExePath = llvm::sys::fs::getMainExecutable(
        Argv0, reinterpret_cast<void *>(+[]() {})); // free fn addr as image hint
    if (!ExePath.empty()) {
        llvm::SmallString<256> Dir(ExePath);
        llvm::sys::path::remove_filename(Dir);
        return std::string(Dir);
    }
    return ".";
}

void printVersion(const char *Argv0) {
    llvm::Triple T(llvm::sys::getDefaultTargetTriple());
    const std::string Dir = findInstallDir(Argv0);
    std::println("plang version {}\nTarget: {}\nThread model: {}\nInstalledDir: {}",
                 PLANG_VERSION_STRING, T.str(),
                 T.isOSWindows() ? "win32" : "posix", Dir);
    std::println("Messages: {}", describeLocale());
}

void usagePC1() {
    std::print(stderr,
        "OVERVIEW: plang Pascal front end (-pc1 mode)\n"
        "\n"
        "USAGE: plang -pc1 [options] source.pas\n"
        "\n"
        "OPTIONS:\n"
        "{}", opts::helpText(opts::Consumer::Frontend));
}

// Symbols -std=turbo predefines automatically for `{$IFDEF}`, matching the
// target being compiled for -- Opts.TargetTriple, or the host default when
// it is empty, the same triple Opts.PointerWidthBits above was just derived
// from and CodeGen's own DataLayout is about to use.  Deliberately minimal:
// just enough for `{$IFDEF UNIX}`/`{$IFDEF LINUX}`-style idioms to work on
// the platforms/architectures this project actually supports (see
// README.md) -- Linux and macOS, x86_64 or aarch64.  Names match FPC's own
// spelling of the same OS/CPU facts, since these are facts about the target
// machine rather than about which compiler is running, so real-world source
// that already tests them ports unmodified.
//
// FPC itself is deliberately NOT predefined: source guarded by
// `{$IFDEF FPC}` takes branches written against FPC-only features this
// Turbo milestone does not implement, and plang predefining that name would
// let such a branch compile as if it did, which is worse than the branch
// simply not being taken.  If a real need for more predefined symbols shows
// up later, this is where they get added.
void addPredefinedConditionalSymbols(LangOptions &Opts) {
    const llvm::Triple T(Opts.TargetTriple.empty()
                              ? llvm::sys::getDefaultTargetTriple()
                              : llvm::Triple::normalize(Opts.TargetTriple));
    if (T.isOSLinux()) {
        Opts.Defines.insert("linux");
        Opts.Defines.insert("unix");
    }
    if (T.isMacOSX()) {
        Opts.Defines.insert("darwin");
        Opts.Defines.insert("unix");
    }
    Opts.Defines.insert(Opts.PointerWidthBits == 32 ? "cpu32" : "cpu64");
    if (T.getArch() == llvm::Triple::x86_64)  Opts.Defines.insert("cpux86_64");
    if (T.getArch() == llvm::Triple::aarch64) Opts.Defines.insert("cpuaarch64");
}

// ---------------------------------------------------------------------------
// PMI writer — serialize a module's exported declarations to Pascal text
// ---------------------------------------------------------------------------

/// Convert an ExprNode back to a Pascal constant expression string.
/// Only handles the simple cases that appear in type bounds.
///
/// \p TurboConstStyle says which of two grammars a TypeName-less
/// StructuredValueExpr (see that node's own comment just below) is to be
/// written back as -- true only while serializing a Turbo typed constant's
/// OWN value (ConstDef::Type set; the writeConst lambdas in both
/// buildPMIContent and buildTUIContent are the only two callers that ever
/// pass true), propagated through every recursive call so a value nested
/// inside a typed constant's own structured literal (an array of records, a
/// record with a nested array field, ...) picks the same grammar all the
/// way down. Every OTHER caller -- including EP's own `type ... value
/// [...]` initial-state clause, which also leaves TypeName empty (see
/// ParseInit.cpp's parseComponentValue) -- takes the default false and gets
/// EP's own bracketed form exactly as before this parameter existed.
static std::string exprToString(const ExprNode& E, bool TurboConstStyle = false) {
    if (auto* IL = llvm::dyn_cast<IntLitExpr>(&E))
        return std::to_string(IL->Value);
    if (auto* Id = llvm::dyn_cast<IdentExpr>(&E))
        return Id->Name;
    if (auto* BL = llvm::dyn_cast<BoolLitExpr>(&E))
        return BL->Value ? "true" : "false";
    if (auto* SL = llvm::dyn_cast<StringLitExpr>(&E)) {
        // ISO §6.1.7: an apostrophe in a string is written twice.  The value
        // went out as it stood, so `'it''s'` was recorded as `'it's'` -- which
        // an importer reads as `it` followed by nonsense, and where the quote
        // count happens to stay even, reads silently as a different string.
        std::string R = "'";
        for (const char C : SL->Value) {
            R += C;
            if (C == '\'') R += C;
        }
        return R + "'";
    }
    if (auto* UN = llvm::dyn_cast<UnaryExpr>(&E)) {
        if (UN->Operand)
            return std::string(opSpelling(UN->Op)) + " "
                 + exprToString(*UN->Operand, TurboConstStyle);
    }
    // EP §6.8.2: a constant may be written as an expression over other
    // constants, and the interface has to carry the expression, not a number
    // it might or might not fold to here.  Parentheses because the text is
    // read back on its own, with none of the surrounding to hold it together.
    if (auto* BN = llvm::dyn_cast<BinaryExpr>(&E)) {
        if (BN->Left && BN->Right)
            return "(" + exprToString(*BN->Left, TurboConstStyle) + " "
                 + std::string(opSpelling(BN->Op)) + " "
                 + exprToString(*BN->Right, TurboConstStyle) + ")";
    }
    if (llvm::isa<NilExpr>(&E)) return "nil";
    // EP §6.8.2: `ord('a')` and its like are constant expressions too, and
    // read back as what they were written as.
    if (auto* CE = llvm::dyn_cast<CallExpr>(&E)) {
        std::string R = CE->Name + "(";
        for (size_t I = 0; I < CE->Args.size(); ++I) {
            if (I) R += ", ";
            R += exprToString(*CE->Args[I], TurboConstStyle);
        }
        return R + ")";
    }
    if (auto* RL = llvm::dyn_cast<RealLitExpr>(&E)) {
        // std::to_string gives six decimals, which is not a double.  A module
        // kept the full value and its interface file recorded 3.141593 for
        // 3.14159265358979, and 0.000000 for 1e-9 -- so an importer computed
        // with a different constant than the module it imported from, or with
        // exactly zero.
        //
        // Seventeen significant digits is what round-trips a double.  %g drops
        // a trailing '.0', and a real literal needs one or an exponent to be
        // read back as a real at all, so it is put back when neither is there.
        char Buf[40];
        std::snprintf(Buf, sizeof Buf, "%.17g", RL->Value);
        std::string R = Buf;
        if (R.find_first_of(".eEnN") == std::string::npos) R += ".0";
        return R;
    }
    if (auto* RG = llvm::dyn_cast<SetRangeExpr>(&E))
        return exprToString(*RG->Low, TurboConstStyle) + ".."
             + exprToString(*RG->High, TurboConstStyle);
    // Turbo typecast: round-trips as the same TypeName(expr) syntax it was
    // written with.
    if (auto* TC = llvm::dyn_cast<TypeCastExpr>(&E))
        return TC->TypeName + "(" + exprToString(*TC->Operand, TurboConstStyle) + ")";
    // EP §6.6: an initial state may be a whole array, record or set, and the
    // interface has to keep saying what state that is.
    if (auto* SL = llvm::dyn_cast<SetLiteralExpr>(&E)) {
        std::string R = SL->TypeName + "[";
        for (size_t I = 0; I < SL->Elements.size(); ++I) {
            if (I) R += ", ";
            R += exprToString(*SL->Elements[I], TurboConstStyle);
        }
        return R + "]";
    }
    if (auto* SV = llvm::dyn_cast<StructuredValueExpr>(&E)) {
        // Two different grammars can both leave TypeName empty, so TypeName
        // alone can NOT tell them apart -- TurboConstStyle (the caller's own
        // context, threaded down from wherever this recursion started) is
        // what does. EP's structured value constructor (§6.8.7) is
        // `TypeName '[' arm... ']'`, and its OWN `type ... value [...]`
        // initial-state clause (ParseInit.cpp's parseComponentValue) leaves
        // TypeName empty too, the same as Turbo's typed-constant value does
        // -- both read back through EP's own '[' / ']' grammar regardless.
        // Turbo's typed-constant value (parseTurboConstValue, ParseDecl.cpp)
        // is `'(' arm... ')'` with no type name at all -- the type is the
        // one the const's own `identifier ':' type` already said, never
        // repeated in the value -- and is the ONLY case TurboConstStyle is
        // ever true for (both writeConst lambdas set it, and only for a
        // ConstDef::Type'd constant's own value). Writing the wrong
        // bracket/paren for either is not a cosmetic difference, it is
        // invalid syntax that grammar's own reader does not accept, so the
        // whole file it sits in fails to reparse -- not just the one
        // declaration.
        std::string R = SV->TypeName + (TurboConstStyle ? "(" : "[");
        // parseTurboConstValue (ParseDecl.cpp) picks its arm separator from
        // the FIRST arm alone and holds every later arm to it: a 'name:
        // value' first arm means every arm is a record field and the
        // separator is ';', anything else means every arm is a positional
        // array element and the separator is ','. Writing the wrong one back
        // -- ';' for a positional list, say -- is not read as the same
        // value: parseArm's own separator check stops after the first arm
        // and "expected ')'" fails the rest of the file with it. EP's own
        // arm separator is always ';' regardless of arm shape (unchanged).
        const std::string TurboSep =
            (!SV->Arms.empty() && !SV->Arms[0].Labels.empty()
             && !SV->Arms[0].IsOtherwise) ? "; " : ", ";
        for (size_t I = 0; I < SV->Arms.size(); ++I) {
            const auto& Arm = SV->Arms[I];
            if (I) R += TurboConstStyle ? TurboSep : "; ";
            if (Arm.IsOtherwise) {
                R += "otherwise ";
            } else if (!Arm.Labels.empty()) {
                // A positional arm (no label at all -- e.g. a plain array
                // element or Turbo's own untagged array-constant entries)
                // has an empty Labels list; writing ": " for it anyway
                // produced `(: 10, : 20, : 30)`, which is not the same
                // expression read back -- it is not a valid one at all.
                for (size_t J = 0; J < Arm.Labels.size(); ++J) {
                    if (J) R += ", ";
                    R += exprToString(*Arm.Labels[J], TurboConstStyle);
                }
                R += ": ";
            }
            if (Arm.Value) R += exprToString(*Arm.Value, TurboConstStyle);
        }
        return R + (TurboConstStyle ? ")" : "]");
    }
    return "0"; // safe fallback
}

static bool canSerializeExpr(const ExprNode& E);

/// The `lo..hi` of an array or subrange denoter, or nothing when either end is
/// one exprToString would have to invent a value for.  A bound is where the
/// rule above matters most: a type written with a bound of zero it never had is
/// not a narrower version of itself, it is a different type, and an importer
/// given it lays out storage of the wrong size and never learns otherwise.
static std::optional<std::string> boundsToString(const ExprNode* Lo,
                                                 const ExprNode* Hi) {
    if (!Lo || !Hi || !canSerializeExpr(*Lo) || !canSerializeExpr(*Hi))
        return std::nullopt;
    return exprToString(*Lo) + ".." + exprToString(*Hi);
}

/// Whether exprToString can write \p E without inventing a value for it.
/// A type bound that falls back to a number reads as a different type, and a
/// constant that falls back to one is a wrong answer in the importing unit;
/// what cannot be written is left out, so the importer says the name is
/// undefined rather than quietly standing on a zero.
static bool canSerializeExpr(const ExprNode& E) {
    if (llvm::isa<IntLitExpr>(&E)  || llvm::isa<RealLitExpr>(&E)
     || llvm::isa<BoolLitExpr>(&E) || llvm::isa<StringLitExpr>(&E)
     || llvm::isa<IdentExpr>(&E)   || llvm::isa<NilExpr>(&E))
        return true;
    if (auto* UN = llvm::dyn_cast<UnaryExpr>(&E))
        return UN->Operand && canSerializeExpr(*UN->Operand);
    if (auto* BN = llvm::dyn_cast<BinaryExpr>(&E))
        return BN->Left && BN->Right
            && canSerializeExpr(*BN->Left) && canSerializeExpr(*BN->Right);
    if (auto* RG = llvm::dyn_cast<SetRangeExpr>(&E))
        return RG->Low && RG->High
            && canSerializeExpr(*RG->Low) && canSerializeExpr(*RG->High);
    if (auto* SL = llvm::dyn_cast<SetLiteralExpr>(&E)) {
        for (const auto& El : SL->Elements)
            if (!El || !canSerializeExpr(*El)) return false;
        return true;
    }
    if (auto* SV = llvm::dyn_cast<StructuredValueExpr>(&E)) {
        for (const auto& Arm : SV->Arms) {
            for (const auto& L : Arm.Labels)
                if (!L || !canSerializeExpr(*L)) return false;
            if (!Arm.Value || !canSerializeExpr(*Arm.Value)) return false;
        }
        return true;
    }
    if (auto* CE = llvm::dyn_cast<CallExpr>(&E)) {
        for (const auto& A : CE->Args)
            if (!A || !canSerializeExpr(*A)) return false;
        return true;
    }
    if (auto* TC = llvm::dyn_cast<TypeCastExpr>(&E))
        return TC->Operand && canSerializeExpr(*TC->Operand);
    return false;
}

static std::string typeDenoterToString(const TypeNode& TN);
static std::string routineHeadingToString(const ProcedureTypeNode& PT,
                                          const std::string& Name);

/// Serialize a TypeNode back to Pascal source text, or "" if the type cannot
/// be written down — see the end of typeDenoterToString.
static std::string typeNodeToString(const TypeNode& TN) {
    std::string R = typeDenoterToString(TN);
    if (R.empty()) return R;
    // EP §6.6: the state a variable of the type begins in is part of what the
    // type-denoter says, so an importer has to be told it too.
    if (TN.InitialState) R += " value " + exprToString(*TN.InitialState);
    return R;
}

/// The fields of a record, joined with \p Sep.  \p Ok is cleared if some
/// field's type cannot be written down.
static std::string fieldListToString(const std::vector<FieldDecl>& Fields,
                                     const std::string& Sep, bool& Ok) {
    std::string R;
    for (size_t I = 0; I < Fields.size(); ++I) {
        if (I) R += Sep;
        const auto& F = Fields[I];
        for (size_t J = 0; J < F.Names.size(); ++J) {
            if (J) R += ", ";
            R += F.Names[J];
        }
        std::string T = F.Type ? typeNodeToString(*F.Type) : std::string();
        if (T.empty()) Ok = false;
        R += ": " + T;
    }
    return R;
}

/// ISO §6.4.3.3: the variant part of a record, written as it was declared.
static std::string variantPartToString(const VariantPart& VP, bool& Ok) {
    std::string R = "  case ";
    if (!VP.TagField.empty()) R += VP.TagField + ": ";
    std::string T = VP.TagType ? typeNodeToString(*VP.TagType) : std::string();
    if (T.empty()) Ok = false;
    R += T + " of\n";
    for (size_t I = 0; I < VP.Cases.size(); ++I) {
        const auto& C = VP.Cases[I];
        if (I) R += ";\n";
        R += "    ";
        for (size_t J = 0; J < C.Labels.size(); ++J) {
            if (J) R += ", ";
            if (C.Labels[J]) R += exprToString(*C.Labels[J]);
        }
        R += ": (" + fieldListToString(C.Fields, "; ", Ok);
        if (C.NestedVariant) {
            if (!C.Fields.empty()) R += "; ";
            R += variantPartToString(*C.NestedVariant, Ok);
        }
        R += ")";
    }
    return R;
}

/// A formal parameter list, in parentheses, or "" when there are no
/// parameters.  \p Ok is cleared if some parameter's type cannot be written.
static std::string paramListToString(const std::vector<ParamGroup>& Params,
                                     bool& Ok) {
    if (Params.empty()) return "";
    std::string R = "(";
    for (size_t I = 0; I < Params.size(); ++I) {
        if (I) R += "; ";
        const auto& Pg = Params[I];
        // ISO §6.6.3.1: a procedural or functional parameter is written as a
        // heading around its own name, not as `name: type`.
        if (auto* PT = llvm::dyn_cast_or_null<ProcedureTypeNode>(Pg.Type.get())) {
            for (size_t J = 0; J < Pg.Names.size(); ++J) {
                if (J) R += "; ";
                R += routineHeadingToString(*PT, Pg.Names[J]);
            }
            continue;
        }
        if (Pg.IsVar)       R += "var ";
        if (Pg.IsProtected) R += "protected ";
        for (size_t J = 0; J < Pg.Names.size(); ++J) {
            if (J) R += ", ";
            R += Pg.Names[J];
        }
        R += ": ";
        std::string T = Pg.Type ? typeNodeToString(*Pg.Type) : std::string();
        if (T.empty()) Ok = false;
        R += T;
    }
    return R + ")";
}

/// The heading of a procedure or function called \p Name, which may be empty
/// for the type of one rather than one of them.
static std::string routineHeadingToString(const ProcedureTypeNode& PT,
                                          const std::string& Name) {
    bool Ok = true;
    std::string R = PT.IsFunction ? "function" : "procedure";
    if (!Name.empty()) R += " " + Name;
    R += paramListToString(PT.Params, Ok);
    if (PT.IsFunction && PT.ReturnType) {
        std::string T = typeNodeToString(*PT.ReturnType);
        if (T.empty()) Ok = false;
        R += ": " + T;
    }
    return Ok ? R : std::string();
}

static std::string typeDenoterToString(const TypeNode& TN) {
    if (auto* NT = llvm::dyn_cast<NamedTypeNode>(&TN))
        // EP §6.4.2.5: what a restricted type keeps from an importer is its
        // structure, which is exactly what the interface has to keep saying.
        return NT->Restricted ? "restricted " + NT->Name : NT->Name;
    if (auto* AT = llvm::dyn_cast<ArrayTypeNode>(&TN)) {
        // ISO §6.4.3.2: packed is part of the type, and an importer told
        // otherwise lays out storage of a different size and shape.
        std::string R = AT->Packed ? "packed array[" : "array[";
        if (AT->Index) {
            R += typeNodeToString(*AT->Index);
        } else {
            auto B = boundsToString(AT->Low.get(), AT->High.get());
            if (!B) return {};
            R += *B;
        }
        R += "] of ";
        std::string E = AT->Element ? typeNodeToString(*AT->Element)
                                    : std::string();
        return E.empty() ? E : R + E;
    }
    if (auto* SR = llvm::dyn_cast<SubrangeTypeNode>(&TN)) {
        auto B = boundsToString(SR->Low.get(), SR->High.get());
        return B ? *B : std::string();
    }
    if (auto* PT = llvm::dyn_cast<PointerTypeNode>(&TN)) {
        if (!PT->Base) return "^integer";
        std::string B = typeNodeToString(*PT->Base);
        return B.empty() ? B : "^" + B;
    }
    if (auto* RT = llvm::dyn_cast<RecordTypeNode>(&TN)) {
        bool Ok = true;
        std::string R = RT->Packed ? "packed record\n" : "record\n";
        R += fieldListToString(RT->Fields, ";\n", Ok);
        if (!RT->Fields.empty()) R += ";\n";
        // ISO §6.4.3.3: the variants are as much of the record as the fixed
        // part.  Left out, the importer lays out a record short of them.
        if (RT->Variant) R += variantPartToString(*RT->Variant, Ok) + "\n";
        if (!Ok) return "";
        return R + "end";
    }
    if (auto* ST = llvm::dyn_cast<SetTypeNode>(&TN)) {
        const std::string Lead = ST->Packed ? "packed set of " : "set of ";
        if (!ST->Base) return Lead + "integer";
        std::string B = typeNodeToString(*ST->Base);
        return B.empty() ? B : Lead + B;
    }
    if (auto* ET = llvm::dyn_cast<EnumTypeNode>(&TN)) {
        std::string R = "(";
        for (size_t I = 0; I < ET->Values.size(); ++I) {
            if (I) R += ", ";
            R += ET->Values[I];
        }
        R += ")";
        return R;
    }
    if (auto* StrT = llvm::dyn_cast<StringTypeNode>(&TN)) {
        // Turbo's string[N] (ShortString) and EP's string(N) (VarString) are
        // different types with different binary layouts -- see
        // StringTypeNode::IsShortString's own comment -- so which bracket
        // gets written back has to track the flag, not default to EP's, or a
        // re-imported interface would resolve the wrong one.
        if (StrT->Capacity)
            return StrT->IsShortString
                ? "string[" + exprToString(*StrT->Capacity) + "]"
                : "string(" + exprToString(*StrT->Capacity) + ")";
        return "string";
    }
    if (auto* PkT = llvm::dyn_cast<PackedTypeNode>(&TN))
        return PkT->Inner ? "packed " + typeNodeToString(*PkT->Inner)
                          : "packed array[1..1] of char";
    // ISO §6.4.3.5 with EP §6.4.3.6: a file's element type is what an importer
    // reads and writes through it, and a direct-access file's index type is
    // part of what it is.
    if (auto* FT = llvm::dyn_cast<FileTypeNode>(&TN)) {
        std::string R = "file";
        if (FT->Index) R += "[" + typeNodeToString(*FT->Index) + "]";
        if (FT->Element) R += " of " + typeNodeToString(*FT->Element);
        return R;
    }
    // EP §6.4.8: a schema instantiation is the schema's name and the values
    // its discriminants take.
    if (auto* SchT = llvm::dyn_cast<SchemaTypeNode>(&TN)) {
        std::string R = SchT->Name;
        if (SchT->Actuals.empty()) return R;
        R += "(";
        for (size_t I = 0; I < SchT->Actuals.size(); ++I) {
            if (I) R += ", ";
            R += SchT->Actuals[I] ? exprToString(*SchT->Actuals[I]) : "0";
        }
        return R + ")";
    }
    // ISO §6.6.3.7: a conformant array parameter, whose bounds are names the
    // body uses and which therefore have to be written out as they stand.
    if (auto* CA = llvm::dyn_cast<ConformantArrayTypeNode>(&TN)) {
        std::string R = CA->Packed ? "packed array[" : "array[";
        for (size_t I = 0; I < CA->Specs.size(); ++I) {
            if (I) R += "; ";
            R += CA->Specs[I].Lo + ".." + CA->Specs[I].Hi + ": "
               + CA->Specs[I].OrdType;
        }
        R += "] of ";
        return R + (CA->Element ? typeNodeToString(*CA->Element) : "integer");
    }
    // ISO §6.6.3.1: a procedural or functional parameter is written as the
    // heading of what it will receive.
    if (auto* PT = llvm::dyn_cast<ProcedureTypeNode>(&TN))
        return routineHeadingToString(*PT, "");
    if (auto* TO = llvm::dyn_cast<TypeOfNode>(&TN))
        return "type of " + TO->VarName;
    // Turbo Tier 5, Cluster B item 8: an object type declared in a unit's
    // own interface, written back as real, re-parseable 'object ... end'
    // syntax -- exactly the same round-trip contract every other case here
    // already keeps.  Deliberately does NOT filter members by visibility:
    // a private field stays inaccessible to code in the consuming unit
    // (Sema enforces that from each ObjectMember's own Vis, carried through
    // resolveObjectType exactly as it is for a single-file object type),
    // but it still has to be PRESENT here, because a descendant object type
    // declared in a DIFFERENT unit needs the ancestor's full, real field
    // layout to compute correct memory layout for itself -- an ancestor's
    // private field omitted here would shrink or misalign every field a
    // cross-unit descendant declares after it.  Method headings carry their
    // own 'virtual'/'abstract' markers back out exactly as written (a
    // constructor/destructor never repeats them, matching
    // parseObjectMethodHeading's own read side); the out-of-line bodies
    // themselves are never part of an interface and so never appear here.
    if (auto* OT = llvm::dyn_cast<ObjectTypeNode>(&TN)) {
        bool Ok = true;
        std::string R = "object";
        if (!OT->Ancestor.empty()) R += "(" + OT->Ancestor + ")";
        R += "\n";
        MemberVisibility CurVis = MemberVisibility::Public;
        for (const auto& M : OT->Members) {
            if (M.Vis != CurVis) {
                R += (M.Vis == MemberVisibility::Private ? "private\n" : "public\n");
                CurVis = M.Vis;
            }
            if (M.IsMethod) {
                const ProcDecl& PD = *M.Method;
                std::string Head = PD.IsConstructor ? "constructor"
                                  : PD.IsDestructor  ? "destructor"
                                  : PD.IsFunction     ? "function"
                                                       : "procedure";
                Head += " " + PD.Name;
                Head += paramListToString(PD.Params, Ok);
                if (PD.IsFunction && PD.ReturnType) {
                    std::string T = typeNodeToString(*PD.ReturnType);
                    if (T.empty()) Ok = false;
                    Head += ": " + T;
                }
                R += "  " + Head + ";";
                // Confirmed against a local fpc -Mtp build (see
                // parseObjectMethodHeading's own comment): 'virtual' is
                // always written before a following 'abstract'.
                if (PD.IsVirtual)  R += " virtual;";
                if (PD.IsAbstract) R += " abstract;";
                R += "\n";
            } else {
                std::string T = M.Field.Type ? typeNodeToString(*M.Field.Type)
                                             : std::string();
                if (T.empty()) Ok = false;
                R += "  ";
                for (size_t J = 0; J < M.Field.Names.size(); ++J) {
                    if (J) R += ", ";
                    R += M.Field.Names[J];
                }
                R += ": " + T + ";\n";
            }
        }
        if (!Ok) return "";
        return R + "end";
    }
    // Nothing left that a type-denoter can be.  Writing a guess here is worse
    // than saying nothing: the importer would lay out the wrong storage and
    // never know, so the caller drops the declaration instead.
    return "";
}

/// The export-list of \p Iface, written as an export-part.
static std::string exportPartText(const ModuleNode& Iface) {
    std::string Text = "export " + Iface.Name + " = (";
    for (size_t I = 0; I < Iface.Exports.size(); ++I) {
        const auto& Item = Iface.Exports[I];
        if (I) Text += ", ";
        if (Item.Protected) Text += "protected ";
        Text += Item.Name;
        if (!Item.RangeEnd.empty()) Text += ".." + Item.RangeEnd;
        if (!Item.Alias.empty())    Text += " => " + Item.Alias;
    }
    return Text + ");\n";
}

/// \p Imports, written as an import-part (EP §6.11.3).  A .pmi is read back
/// through the same parser that reads a module heading in source, so a type
/// or var the interface declares in terms of a name it imports needs that
/// import written out here too — without it, the identifier the declaration
/// names is one the reloaded interface has never heard of.
static std::string importPartText(const std::vector<ImportClause>& Imports) {
    std::string Text;
    for (const auto& Clause : Imports) {
        Text += "import " + Clause.ModuleName;
        if (Clause.Qualified) Text += " qualified";
        if (!Clause.Names.empty()) {
            Text += Clause.Selective ? " only (" : " (";
            for (size_t I = 0; I < Clause.Names.size(); ++I) {
                if (I) Text += ", ";
                Text += Clause.Names[I];
                for (const auto& [From, To] : Clause.Renames)
                    if (eqCI(From, Clause.Names[I])) {
                        Text += " => " + To;
                        break;
                    }
            }
            Text += ")";
        }
        Text += ";\n";
    }
    return Text;
}

/// Build the Pascal text of a .pmi file for one module body.
///
/// A .pmi is the module's interface written out as Extended Pascal, so that
/// reading it back is the same act as reading an interface in the source: the
/// export-list travels with it, and with it every renaming, which no filtered
/// list of declarations could carry.
///
/// \p Iface is the matching interface module, or null when there is none, in
/// which case everything the body declares is exported.  Reports
/// err_pmi_cannot_serialize_export through \p Diags for each declaration
/// that had to be left out because it could not be turned back into Pascal
/// text (issue #397) -- callers must not publish the returned text under
/// those diagnostics, since it is then missing something the module's own
/// interface promised.
static std::string buildPMIContent(const ModuleNode& Mod,
                                    const ModuleNode* Iface,
                                    DiagnosticsEngine& Diags) {
    if (!Mod.Body) return "";

    // What an interface says is what a .pmi has to say, and the interface is
    // where it is written: a body repeats none of the types and writes its
    // routines as the name alone, so serializing the body left an importer
    // with types it had never heard of and headings with no parameters.
    const BlockNode& Decls =
        (Iface && Iface->Body) ? *Iface->Body : *Mod.Body;

    // A name is written out when the export-list mentions it, either on its
    // own or as an end of a range.  Types are always written: an exported
    // signature is unreadable without the types it names.
    auto exported = [&](const std::string& Name) -> bool {
        if (!Iface || Iface->Exports.empty()) return true;
        for (const auto& E : Iface->Exports)
            if (eqCI(E.Name, Name) || eqCI(E.RangeEnd, Name)) return true;
        return false;
    };

    // The imports the interface declarations above were resolved against: the
    // interface's own import-part when there is a separate interface, else
    // the one body standing in for it (module Foo; ... end. with no separate
    // heading) — the same choice Decls just made above, for the same reason.
    const std::vector<ImportClause>& RelevantImports =
        (Iface && Iface->Body) ? Iface->Imports : Mod.Imports;

    std::string PMI;
    PMI += "{ plang module interface: ";
    PMI += Mod.Name;
    PMI += " }\n";
    PMI += "module " + Mod.Name + " interface;\n";
    PMI += importPartText(RelevantImports);
    if (Iface && !Iface->Exports.empty()) PMI += exportPartText(*Iface);

    // Constants before the types, which may be written in terms of them: the
    // bound of an exported array is as often a constant as a number.  They go
    // out whether or not the export-list names them, for the same reason the
    // types do — what the list leaves out stays unimportable, but a bound that
    // names a constant the file never declares leaves the type unreadable.
    // A structured constant is the other way round, naming the type it is a
    // value of, so those follow the types instead.
    // A Turbo typed constant (Cd.Type set) names its own type the same way a
    // structured constant names the type of its value -- see ConstDef::Type's
    // own comment -- so it has to wait for the types loop below for the same
    // reason: `const CB: Byte = 200;` written before `type` has resolved
    // nothing yet reads back fine only because Byte happens to be predefined,
    // but `const CRec: TRec = (...);` written before `type TRec = ...` names
    // a type the reader has never heard of yet.  EP itself never sets
    // Cd.Type (see that comment), so this never changes EP's own ordering.
    auto isStructured = [](const ConstDef& Cd) {
        return Cd.Type || llvm::isa<StructuredValueExpr>(Cd.Value.get());
    };
    // A declaration this function is about to leave out of the .pmi: says so
    // through Diags rather than doing it quietly (issue #397).  Every site
    // below that used to just "continue"/"return" without writing anything
    // calls this instead.
    //
    // Constants and types are attempted whether or not the export-list names
    // them (see the comments above and at the type loop below) -- that part
    // of issue #397's fix is unchanged.  But only a name the export-list
    // actually contains can ever be needed by anything outside this module,
    // so only that case is worth failing the compile over: an internal-only
    // declaration nothing can import was already fine to drop silently
    // before #397, and #397 must not turn that into a hard error just
    // because it happens to share these two loops with declarations that do
    // matter (issue #413).
    auto reportDropped = [&](std::string_view Kind, const std::string& Name) {
        if (!exported(Name)) return;
        Diags.report(SourceLocation(), diag::err_pmi_cannot_serialize_export,
                     {Kind, Name, Mod.Name});
    };
    auto writeConst = [&](const ConstDef& Cd) {
        if (!Cd.Value || !canSerializeExpr(*Cd.Value)) {
            reportDropped("constant", Cd.Name);
            return;
        }
        // Turbo's typed-constant form (ConstDef::Type's own comment): dropping
        // the ': Type' here would round-trip a value that reads back with
        // whatever type Sema infers for the bare expression instead of the
        // one actually declared -- e.g. `CB: Byte = 200` losing its Byte and
        // coming back as plang's default Integer, sized and signed
        // differently, so code compiled against the reloaded interface reads
        // a different value than the unit that published it (issue: this
        // writer used to drop it silently).
        std::string TypeAnnotation;
        if (Cd.Type) {
            TypeAnnotation = typeNodeToString(*Cd.Type);
            if (TypeAnnotation.empty()) {
                reportDropped("constant", Cd.Name);
                return;
            }
        }
        PMI += "const " + Cd.Name;
        if (Cd.Type) PMI += ": " + TypeAnnotation;
        // TurboConstStyle=Cd.Type!=null: a typed constant's own value reads
        // back through parseTurboConstValue's '(...)' grammar, not EP's
        // 'TypeName[...]' -- see exprToString's own comment on
        // TurboConstStyle for why TypeName alone can't make this call.
        PMI += " = " + exprToString(*Cd.Value, /*TurboConstStyle=*/Cd.Type != nullptr) + ";\n";
    };
    for (const auto& Cd : Decls.Consts)
        if (Cd.Value && !isStructured(Cd)) writeConst(Cd);

    // Type definitions next (so procs that reference them can resolve them).
    for (const auto& Td : Decls.Types) {
        std::string T = Td.Type ? typeNodeToString(*Td.Type) : std::string();
        if (T.empty()) { reportDropped("type", Td.Name); continue; }
        PMI += "type ";
        PMI += Td.Name;
        // EP §6.4.7: a schema is a type with discriminants, and an importer
        // that could not see them could not instantiate it.
        if (!Td.SchemaParams.empty()) {
            PMI += "(";
            for (size_t I = 0; I < Td.SchemaParams.size(); ++I) {
                if (I) PMI += "; ";
                const auto& Sp = Td.SchemaParams[I];
                for (size_t J = 0; J < Sp.Names.size(); ++J) {
                    if (J) PMI += ", ";
                    PMI += Sp.Names[J];
                }
                PMI += ": " + Sp.TypeName;
            }
            PMI += ")";
        }
        PMI += " = ";
        PMI += T;
        PMI += ";\n";
    }

    // EP §6.8.7: a value constructor names its type, so it is written once the
    // types are there to be named.
    for (const auto& Cd : Decls.Consts)
        if (Cd.Value && isStructured(Cd)) writeConst(Cd);

    // Variable declarations.
    for (const auto& Vg : Decls.Vars) {
        std::vector<std::string> ExportedNames;
        for (const auto& Nm : Vg.Names)
            if (exported(Nm)) ExportedNames.push_back(Nm);
        if (ExportedNames.empty()) continue;
        std::string T = Vg.Type ? typeNodeToString(*Vg.Type) : std::string();
        if (T.empty()) {
            for (const auto& Nm : ExportedNames) reportDropped("variable", Nm);
            continue;
        }
        PMI += "var ";
        for (size_t I = 0; I < ExportedNames.size(); ++I) {
            if (I) PMI += ", ";
            PMI += ExportedNames[I];
        }
        PMI += ": ";
        PMI += T;
        PMI += ";\n";
    }

    // Procedure / function forward declarations.
    // Track which names we've already emitted to avoid duplicates
    // (a body may have both a forward decl and an implementation).
    std::set<std::string> Written;
    for (const auto& Proc : Decls.Procs) {
        if (!exported(Proc->Name)) continue;
        std::string Lo = toLower(Proc->Name);
        if (Written.count(Lo)) continue;
        Written.insert(Lo);

        bool Ok = true;
        std::string Heading = Proc->IsFunction ? "function " : "procedure ";
        Heading += Proc->Name;
        Heading += paramListToString(Proc->Params, Ok);
        if (Proc->IsFunction && Proc->ReturnType) {
            std::string T = typeNodeToString(*Proc->ReturnType);
            if (T.empty()) Ok = false;
            Heading += ": " + T;
        }
        // A heading written with a type the writer had to guess at would be a
        // heading for some other routine than the one compiled.
        if (!Ok) { reportDropped("routine", Proc->Name); continue; }
        PMI += Heading + ";\n";
    }

    PMI += "end.\n";
    return PMI;
}

/// Write PMI files for all module bodies in the program.
/// PMI files are written to the same directory as the input source file.
/// Returns false if any interface could not be published -- either an I/O
/// failure (reported directly to stderr below) or a declaration that could
/// not be serialized (reported through \p Diags by buildPMIContent, issue
/// #397).  Either way, the caller then fails the whole compilation rather
/// than let it succeed without a usable interface for something that
/// imports this module later.
static bool writePMIFiles(const ProgramNode& Program,
                           const std::string& InputFile,
                           DiagnosticsEngine& Diags) {
    // Determine directory from input file path.
    std::string PmiDir = ".";
    {
        auto Slash = InputFile.rfind('/');
        if (Slash != std::string::npos)
            PmiDir = InputFile.substr(0, Slash);
    }

    // Every .pmi this call has actually published (renamed into place), so
    // that a failure discovered later -- either this module's own I/O
    // failure, or another module's, including one buildPMIContent already
    // reported through Diags -- can undo them before returning false.  This
    // compilation unit is what the driver reports success or failure for as
    // a whole: nothing outside it can tell an interface published before the
    // failure was found from one published after, so an all-or-nothing
    // compile must not leave an all-or-nothing-shaped mix of some published
    // and some not (issue #414). Each entry here already went through the
    // atomic write-temp-then-rename dance below, so undoing one is a plain
    // best-effort remove -- there is no partial file to worry about.
    std::vector<std::string> PublishedPaths;
    auto cleanupPublished = [&]() {
        for (const auto& P : PublishedPaths) llvm::sys::fs::remove(P);
    };

    // For each body module, find its matching interface (if any) for the
    // export filter, then serialize.
    for (const auto& Mod : Program.OwnedModules) {
        if (Mod->IsInterface || !Mod->Body) continue;

        const ModuleNode* Iface = nullptr;
        for (const auto& M : Program.OwnedModules)
            if (M->IsInterface && eqCI(M->Name, Mod->Name)) {
                Iface = M.get();
                break;
            }

        const unsigned ErrorsBefore = Diags.numErrors();
        std::string Content = buildPMIContent(*Mod, Iface, Diags);
        // A declaration buildPMIContent could not serialize has already been
        // diagnosed (issue #397); publishing the rest of the interface
        // anyway would report a clean compile for a module some later import
        // of the missing name is guaranteed to fail against, with nothing
        // to connect that failure back to here.  Skip writing it -- the
        // error already reported fails the whole compile once this loop
        // returns, same as an I/O failure below.
        if (Diags.numErrors() != ErrorsBefore) continue;
        if (Content.empty()) continue;

        // Canonicalized (lowercased): Pascal identifiers are case-insensitive,
        // and Sema::processImports looks a module up by its lowercased import
        // spelling (see the matching toLower(Clause.ModuleName) there), which
        // need not be how this module's own declaration spelled its name. A
        // module declared ReviewCaseModule but imported as reviewcasemodule
        // must find the same file on a case-sensitive filesystem; the module's
        // true identity still travels inside the file itself (the "module ...
        // interface" heading loadPMI validates against the import).
        std::string PmiPath = PmiDir + "/" + toLower(Mod->Name) + ".pmi";

        // Publish atomically: write to a sibling temp file, checking every
        // step, and only rename it into place once it is known-good. A crash
        // or a write failure (disk full, permission denied, ...) partway
        // through must never leave a corrupt or truncated ModName.pmi sitting
        // where a valid one used to be for a later, unrelated compile to load
        // and trust.
        llvm::SmallString<128> TmpPath;
        int TmpFd = -1;
        if (auto EC = llvm::sys::fs::createUniqueFile(PmiPath + "-%%%%%%.tmp",
                                                        TmpFd, TmpPath)) {
            std::cerr << "plang -pc1: cannot create module interface file '"
                       << PmiPath << "': " << EC.message() << "\n";
            cleanupPublished();
            return false;
        }

        {
            llvm::raw_fd_ostream OS(TmpFd, /*shouldClose=*/true);
            OS << Content;
            OS.close();
            if (OS.has_error()) {
                std::error_code EC = OS.error();
                OS.clear_error();
                std::cerr << "plang -pc1: cannot write module interface '"
                           << Mod->Name << "' to '" << PmiPath << "': "
                           << EC.message() << "\n";
                llvm::sys::fs::remove(TmpPath);
                cleanupPublished();
                return false;
            }
        }

        if (auto EC = llvm::sys::fs::rename(TmpPath, PmiPath)) {
            std::cerr << "plang -pc1: cannot publish module interface '"
                       << Mod->Name << "' to '" << PmiPath << "': "
                       << EC.message() << "\n";
            llvm::sys::fs::remove(TmpPath);
            cleanupPublished();
            return false;
        }
        PublishedPaths.push_back(PmiPath);
    }
    // Every module's own interface published cleanly, unless one of them had
    // a declaration buildPMIContent could not serialize (issue #397): that
    // module's .pmi was skipped above, but the loop still ran to completion
    // over the rest, so this checks Diags rather than the loop itself having
    // already returned false.  A module later in the loop than the one that
    // failed can still have published its own .pmi successfully by this
    // point (issue #414) -- undo those too before reporting failure, so a
    // compile that is about to fail overall never leaves any of this call's
    // interfaces sitting on disk for something else to find and trust.
    if (Diags.hasErrors()) {
        cleanupPublished();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Turbo Tier 4, Cluster A item 2: .tui writer -- serialize a unit's own
// INTERFACE section to Pascal text, replacing item 1's own temporary loader
// (Sema.cpp's loadUnitInterfaceExports re-parsing "<name>.pas" from scratch
// every time) with a real one, modeled closely on the .pmi mechanism just
// above but genuinely parallel to it, not a reuse of it: buildPMIContent/
// writePMIFiles/loadPMI/processImports are never called or modified by any
// of this.  A .tui is, on purpose, valid Turbo unit syntax -- literally
// `unit Name; interface uses ...; <decls>; implementation end.` -- so the
// reader (Sema::loadUnitInterfaceExports) needs no synthetic wrapper the way
// loadPMI needs one to turn a bare module heading into a parseable file:
// Parser::parseUnitFile already parses a standalone unit file, so a .tui is
// read back through exactly that, unchanged.
// ---------------------------------------------------------------------------

/// \p Uses, written as a Turbo `uses` clause, or "" when \p Uses is empty.
/// Turbo's `uses` has none of EP's ImportClause syntax (no qualified/only/
/// renaming), so this is a plain comma-separated name list -- see
/// UsedUnit's own comment for why the AST already carries nothing more.
static std::string usesPartText(const std::vector<UsedUnit>& Uses) {
    if (Uses.empty()) return "";
    std::string Text = "uses ";
    for (size_t I = 0; I < Uses.size(); ++I) {
        if (I) Text += ", ";
        Text += Uses[I].Name;
    }
    return Text + ";\n";
}

/// Build the Pascal text of a .tui file for one Turbo unit's INTERFACE
/// section only -- never its implementation, which is exactly what real
/// separate compilation means here: whoever reads this back gets the same
/// names Sema::checkUnitInterfaceOnly already harvests from `interface ...`
/// directly, with no access to `implementation ...` at all, matching real
/// `fpc -Mtp`'s own "a unit's implementation is its own business" rule.
///
/// Reports err_pmi_cannot_serialize_export-shaped diagnostics is NOT done
/// here on purpose: unlike EP's export-list (which promises a name the body
/// must then supply), a Turbo unit's `interface` section IS its own export
/// list -- everything declared there is exported unconditionally (see
/// UnitNode's own comment) -- so a declaration this function cannot
/// serialize is simply left out of the .tui, and Sema::loadUnitInterfaceExports
/// harvests one fewer symbol than the unit itself has, exactly the same
/// silent-drop behavior item 1's own .pas-reparsing loader already had for
/// anything checkUnitInterfaceOnly could not resolve.  A future item (3) that
/// teaches this writer every remaining Turbo denoter closes that gap; this
/// item's own report says explicitly what is and is not covered yet.
static std::string buildTUIContent(const UnitNode& Unit) {
    if (!Unit.InterfaceBlock) return "";
    const BlockNode& Decls = *Unit.InterfaceBlock;

    std::string TUI;
    TUI += "{ plang turbo unit interface: ";
    TUI += Unit.Name;
    TUI += " }\n";
    TUI += "unit " + Unit.Name + ";\n\n";
    TUI += "interface\n\n";
    TUI += usesPartText(Unit.InterfaceUses);

    // A typed constant (ConstDef::Type set -- the common case for a Turbo
    // unit's own const section) names its own type, the same way an EP
    // structured constant names the type of its value, so it has to wait for
    // the types loop below for the same reason buildPMIContent's own
    // isStructured does (see that function's matching comment): a type named
    // in the const section itself may not exist yet.
    auto isStructured = [](const ConstDef& Cd) {
        return Cd.Type || llvm::isa<StructuredValueExpr>(Cd.Value.get());
    };
    auto writeConst = [&](const ConstDef& Cd) -> bool {
        if (!Cd.Value || !canSerializeExpr(*Cd.Value)) return false;
        // Dropping ': Type' here would round-trip `CB: Byte = 200` as a
        // plain untyped const, which Sema then gives plang's default
        // Integer type instead of Byte -- a different width and signedness,
        // so an importer reads a wrong value back (see buildPMIContent's
        // matching writeConst for the full story; this is the same bug for
        // the same reason, just in the sibling writer).
        std::string TypeAnnotation;
        if (Cd.Type) {
            TypeAnnotation = typeNodeToString(*Cd.Type);
            if (TypeAnnotation.empty()) return false;
        }
        TUI += "const " + Cd.Name;
        if (Cd.Type) TUI += ": " + TypeAnnotation;
        // TurboConstStyle=Cd.Type!=null: see buildPMIContent's matching
        // writeConst / exprToString's own TurboConstStyle comment.
        TUI += " = " + exprToString(*Cd.Value, /*TurboConstStyle=*/Cd.Type != nullptr) + ";\n";
        return true;
    };
    for (const auto& Cd : Decls.Consts)
        if (Cd.Value && !isStructured(Cd)) writeConst(Cd);

    for (const auto& Td : Decls.Types) {
        std::string T = Td.Type ? typeNodeToString(*Td.Type) : std::string();
        if (T.empty()) continue; // dropped -- see this function's own comment
        TUI += "type " + Td.Name + " = " + T + ";\n";
    }

    for (const auto& Cd : Decls.Consts)
        if (Cd.Value && isStructured(Cd)) writeConst(Cd);

    for (const auto& Vg : Decls.Vars) {
        std::string T = Vg.Type ? typeNodeToString(*Vg.Type) : std::string();
        if (T.empty()) continue;
        TUI += "var ";
        for (size_t I = 0; I < Vg.Names.size(); ++I) {
            if (I) TUI += ", ";
            TUI += Vg.Names[I];
        }
        TUI += ": " + T + ";\n";
    }

    // Procedure/function headings.  Every ProcDecl in a unit's own
    // InterfaceBlock is already a heading with no body (IsForward, exactly
    // like EP's own HeadingsOnly module interface -- see UnitNode's own
    // comment), so there is no "already emitted this one as a forward decl"
    // bookkeeping to do the way buildPMIContent needs for a module BODY,
    // which mixes forward declarations and real definitions in one list.
    for (const auto& Proc : Decls.Procs) {
        bool Ok = true;
        std::string Heading = Proc->IsFunction ? "function " : "procedure ";
        Heading += Proc->Name;
        Heading += paramListToString(Proc->Params, Ok);
        if (Proc->IsFunction && Proc->ReturnType) {
            std::string T = typeNodeToString(*Proc->ReturnType);
            if (T.empty()) Ok = false;
            Heading += ": " + T;
        }
        if (!Ok) continue; // dropped -- see this function's own comment
        TUI += Heading + ";\n";
    }

    TUI += "\nimplementation\n\nend.\n";
    return TUI;
}

/// Write \p Unit's own .tui file next to \p InputFile.  Mirrors
/// writePMIFiles' atomic write-temp-then-rename publish dance (a crash or a
/// write failure partway through must never leave a corrupt or truncated
/// interface file for a later, unrelated compile to load and trust), but is
/// its own, Turbo-specific function -- writePMIFiles itself is untouched,
/// and this never calls it.  Returns false (after reporting to stderr) on
/// any I/O failure.
static bool writeTUIFile(const UnitNode& Unit, const std::string& InputFile) {
    std::string TuiDir = ".";
    if (auto Slash = InputFile.rfind('/'); Slash != std::string::npos)
        TuiDir = InputFile.substr(0, Slash);

    std::string Content = buildTUIContent(Unit);
    if (Content.empty()) return true; // nothing to publish (no interface at all)

    // Canonicalized (lowercased) for the same reason writePMIFiles' own
    // PmiPath is: Sema::loadUnitInterfaceExports looks up a unit's file by
    // its lowercased 'uses' spelling, regardless of how the unit's own
    // `unit Name;` heading capitalized it.
    std::string TuiPath = TuiDir + "/" + toLower(Unit.Name) + ".tui";

    llvm::SmallString<128> TmpPath;
    int TmpFd = -1;
    if (auto EC = llvm::sys::fs::createUniqueFile(TuiPath + "-%%%%%%.tmp",
                                                    TmpFd, TmpPath)) {
        std::cerr << "plang -pc1: cannot create unit interface file '"
                   << TuiPath << "': " << EC.message() << "\n";
        return false;
    }
    {
        llvm::raw_fd_ostream OS(TmpFd, /*shouldClose=*/true);
        OS << Content;
        OS.close();
        if (OS.has_error()) {
            std::error_code EC = OS.error();
            OS.clear_error();
            std::cerr << "plang -pc1: cannot write unit interface '"
                       << Unit.Name << "' to '" << TuiPath << "': "
                       << EC.message() << "\n";
            llvm::sys::fs::remove(TmpPath);
            return false;
        }
    }
    if (auto EC = llvm::sys::fs::rename(TmpPath, TuiPath)) {
        std::cerr << "plang -pc1: cannot publish unit interface '"
                   << Unit.Name << "' to '" << TuiPath << "': "
                   << EC.message() << "\n";
        llvm::sys::fs::remove(TmpPath);
        return false;
    }
    return true;
}

/// Diagnoses and reports whether Os is in a fail state after its writer has
/// already been flushed (Name empty means Os is std::cout; otherwise it is
/// the -o path that was opened into Os).
///
/// A write can fail after the open already succeeded -- a full disk, or
/// /dev/full in a repro -- and std::ostream only ever surfaces that through
/// its own failbit, which a small write does not set until the underlying
/// buffer is actually flushed.  Callers must flush/close Os before calling
/// this, or a failure that has not reached the OS yet reads as success.
static bool reportIfWriteFailed(std::ostream& Os, const std::string& Name) {
    if (Os) return false;
    std::cerr << "plang -pc1: error writing "
              << (Name.empty() ? "to standard output" : "output file '" + Name + "'")
              << "\n";
    return true;
}

} // namespace

int frontendPC1Main(int Argc, char *Argv[]) {
    // The front end is a separate process from the driver and prints its own
    // diagnostics, so it resolves its own catalog and applies its own
    // -w/-Werror/-Wno-<name>/-W<name>/-fcolor-diagnostics policy.  Both are a
    // prescan rather than a case in the loop below, because that loop reports
    // as it goes: a locale chosen partway through would translate only the
    // messages after it, and a policy known only partway through would let
    // "-Wbogus -w" print a warning that "-w -Wbogus" -- the same two flags,
    // the other order -- would have suppressed.  Driver::configureDiagnostics
    // is the same prescan for the same reason.
    std::string_view         Lang;
    bool                     ShowFuzzy        = false;
    bool                     SuppressWarnings = false;
    bool                     WarningsAsErrors = false;
    std::vector<std::string> DisabledWarnings;
    ColorDiagnostics         Color            = ColorDiagnostics::Auto;
    {
        constexpr std::string_view LangOpt = "-fdiagnostics-language=";
        for (int I = 2; I < Argc; ++I) {
            const std::string_view A = Argv[I];
            if (A.starts_with(LangOpt))               Lang = A.substr(LangOpt.size());
            else if (A == "-fdiagnostics-show-fuzzy") ShowFuzzy = true;
            else if (A == "-w")                       SuppressWarnings = true;
            else if (A == "-Werror")                  WarningsAsErrors = true;
            else if (A == "-Wall")                     DisabledWarnings.clear();
            else if (A.starts_with("-Wno-") && A.size() > 5) {
                const std::string Name(A.substr(5));
                if (Name == "all") SuppressWarnings = true;
                else                DisabledWarnings.push_back(Name);
            } else if (A.starts_with("-W") && A.size() > 2) {
                std::erase(DisabledWarnings, std::string(A.substr(2)));
            } else if (auto C = colorDiagnosticsArg(A); C != ColorDiagnostics::Auto) {
                Color = C;
            }
        }
        (void)selectLocale(Lang, findInstallDir(Argc > 0 ? Argv[0] : nullptr),
                           ShowFuzzy);
    }

    DiagnosticOptions DiagOpts;
    DiagOpts.SuppressWarnings = SuppressWarnings;
    DiagOpts.WarningsAsErrors = WarningsAsErrors;
    DiagOpts.DisabledWarnings = std::move(DisabledWarnings);

    DiagnosticsEngine Diags(std::move(DiagOpts));
    SourceManager     SrcMgr;
    // No Prefix: a front-end diagnostic with nowhere to point (a bad
    // argument, same as a missing input file already reported this way)
    // prints bare "error: ...", not "plang -pc1: error: ...".  clang -cc1
    // differs from clang the same way, and the existing
    // err_file_not_found/DriverDiagnostics tests already pin this down for
    // the front end -- this Printer has to agree with them, since it is the
    // one Scanner/Parser/Sema also print through.
    DiagnosticPrinter Printer(SrcMgr, useColor(Color, isatty(STDERR_FILENO)));

    // Prints whatever has not been printed yet.  report() below calls this
    // after every command-line diagnostic, so each one appears as soon as it
    // is found -- like Driver::diag() -- while the Scanner/Parser/Sema
    // pipeline further down calls it in batches instead.  Tracking how much
    // has already been printed, rather than replaying every diagnostic in
    // Diags each time, is what keeps the two from ever printing the same one
    // twice, whichever runs first.
    size_t Flushed = 0;
    auto emitAll = [&] {
        for (; Flushed < Diags.size(); ++Flushed)
            std::cerr << Printer.print(Diags[Flushed]) << "\n";
    };
    // Issue #276: every command-line diagnostic below used to print straight
    // to std::cerr, so -w, -Werror and -Wno-<name> had no effect on any of
    // them.  report() is this file's equivalent of Driver::diag(): apply
    // that policy through DiagnosticsEngine like everything else in the
    // compiler, and print immediately if it was not suppressed.
    auto report = [&](DiagID ID, std::initializer_list<std::string_view> Args = {}) {
        Diags.report(SourceLocation(), ID, Args);
        emitAll();
    };

    std::string               InputFile;
    std::string               OutputFile;
    std::string               Std;
    std::string               Target;
    // Unset (neither -frange-checks nor -fno-range-checks given) means "the
    // active dialect's own default" -- ISO 7185/Extended Pascal on, Turbo
    // off ({$R-}, matching real Turbo Pascal) -- resolved below, once Std
    // is known, since -std= and these two flags may arrive in either order
    // on the command line.  See LangOptions::RangeChecks's own default,
    // which this feeds.
    std::optional<bool>      RangeChecks;
    bool                      NilChecks     = true;
    unsigned                  OptLevel      = 0;
    bool                      Debug         = false;
    bool                      DumpAst       = false;
    bool                      DumpTokens    = false;
    bool                      DumpParseTree = false;
    // Turbo Tier 5, Cluster A item 1: see plang/Sema/DumpVmt.h for why this
    // exists as its own dump mode rather than folded into -dump-ast.
    bool                      DumpVmt       = false;
    std::vector<std::string>  ModuleSearchPaths;
    std::vector<std::string>  IncludeSearchPaths; // -Fi<dir>: {$I}/{$INCLUDE}
    // -d<symbol>/-u<symbol>, in the order given on the command line: true
    // for a -d (define), false for a -u (undefine).  Applied in order onto
    // Opts.Defines below, after addPredefinedConditionalSymbols has set the
    // starting baseline, so a later -u can undo an earlier -d (or a
    // predefined symbol) and vice versa -- see LangOptions::Defines's own
    // comment for why this is the right order.
    std::vector<std::pair<bool, std::string>> DefineOps;

    // Options start at Argv[2]; Argv[0]="plang", Argv[1]="-pc1".
    for (int I = 2; I < Argc; ++I) {
        std::string Arg = Argv[I];

        if (Arg == "--version") {
            printVersion(Argc > 0 ? Argv[0] : nullptr);
            return 0;
        } else if (Arg == "-h" || Arg == "--help") {
            usagePC1();
            return 0;
        } else if (Arg == "--help-warnings") {
            // Issue #181: Options.def has always listed this as Consumer::Both,
            // but this parser -- separate from the driver's own -- never grew
            // an arm for it, so "plang -pc1 --help-warnings" fell through to
            // "unrecognized argument" even though the driver already
            // implements it (see Driver.cpp's own arm, which this mirrors) and
            // never reaches here for a driver-mediated compile, since the
            // driver's own --help-warnings arm returns before spawning -pc1 at
            // all.  Only matters to someone invoking -pc1 directly.
            std::println("Warnings, all enabled by default.  Turn one off with");
            std::println("-Wno-<name>, or all of them with -w.\n");
            forEachWarningName([](const std::string &N) {
                std::println("  -Wno-{}", N);
            });
            return 0;
        } else if (Arg.starts_with("-o") && Arg.size() > 2) {
            // Joined form (issue #244): "-ofile.ll".  Options.def has always
            // declared -o JoinedOrSeparate, but this parser -- entirely
            // separate from the driver's own, and from Options.def's table
            // (see issue #181) -- implemented only the separate form below,
            // same gap as the driver's had.  Normally masked when going
            // through the driver, since compile() always constructs this
            // process's own "-o" as two argv entries, the resolved path, no
            // matter what the user typed; matters when -pc1 is invoked
            // directly, as the driver itself does not for this exact option
            // -- see the -I arm just below, which already has this shape for
            // the same JoinedOrSeparate reason.
            OutputFile = Arg.substr(2);
        } else if (Arg == "-o") {
            if (I + 1 >= Argc) {
                report(diag::err_arg_requires_value, {"-o"});
                return 1;
            }
            OutputFile = Argv[++I];
        } else if (Arg.starts_with("-fdiagnostics-language=") ||
                   Arg == "-fdiagnostics-show-fuzzy") {
            // Acted on by the prescan above.  The arm is still required: this
            // chain ends in "unrecognized argument", and the driver forwards
            // both of these, so without it every driver-mediated compile would
            // warn about an option the driver itself just passed on.
        } else if (Arg.starts_with("--target=")) {
            Target = Arg.substr(9);
        } else if (Arg.starts_with("-std=")) {
            Std = Arg.substr(5);
            if (!LangOptions::parseDialect(Std)) {
                report(diag::err_unknown_dialect,
                       {Std, LangOptions::knownDialects()});
                return 1;
            }
            if (!LangOptions::isImplementedDialect(Std)) {
                report(diag::err_dialect_not_implemented,
                       {Std, LangOptions::implementedDialects()});
                return 1;
            }
        } else if (Arg == "-w" || Arg == "-Werror") {
            // Acted on by the prescan above; still matched here so this
            // chain does not end in "unrecognized argument".
        } else if (Arg == "-frange-checks") {
            RangeChecks = true;
        } else if (Arg == "-fno-range-checks") {
            RangeChecks = false;
        } else if (Arg == "-fnil-checks") {
            NilChecks = true;
        } else if (Arg == "-fno-nil-checks") {
            NilChecks = false;
        } else if (Arg == "-fcolor-diagnostics" || Arg == "-fno-color-diagnostics") {
            // Acted on by the prescan above; Printer's color is already set.
        } else if (Arg.size() == 3 && Arg.starts_with("-O")
                                   && Arg[2] >= '0' && Arg[2] <= '3') {
            OptLevel = static_cast<unsigned>(Arg[2] - '0');
        } else if (Arg == "-dump-ast") {
            DumpAst = true;
        } else if (Arg == "-dump-tokens") {
            DumpTokens = true;
        } else if (Arg == "-dump-parse-tree") {
            DumpParseTree = true;
        } else if (Arg == "-dump-vmt") {
            DumpVmt = true;
        } else if (Arg == "-g") {
            Debug = true;
        // Must come after the "-dump-ast"/"-dump-tokens"/"-dump-parse-tree"/
        // "-dump-vmt" exact-match arms above: those also start with "-d", and an
        // else-if chain resolves on the first match, unlike the driver's
        // own opts::lookup (which picks the longest spelling regardless of
        // arm order) -- see Options.def's -d entry.
        } else if (Arg.starts_with("-d") && Arg.size() > 2) {
            DefineOps.emplace_back(true, Arg.substr(2));
        } else if (Arg.starts_with("-u") && Arg.size() > 2) {
            DefineOps.emplace_back(false, Arg.substr(2));
        } else if (Arg.starts_with("-I") && Arg.size() > 2) {
            ModuleSearchPaths.push_back(Arg.substr(2));
        } else if (Arg == "-I") {
            if (I + 1 >= Argc) {
                report(diag::err_arg_requires_value, {"-I"});
                return 1;
            }
            ModuleSearchPaths.push_back(Argv[++I]);
        // -Fi<dir>/-Fi dir: the {$I}/{$INCLUDE} search path, parsed the same
        // JoinedOrSeparate way as -I just above -- a deliberately separate
        // flag and a deliberately separate list (see
        // LangOptions::IncludeSearchPaths's own comment for why -I itself is
        // not reused here). Must come before the "-I" arms would ever be
        // reached for it: they only match an exact "-I" or something
        // starting with "-I" -- "-Fi..." starts with neither, so order
        // between the two pairs does not actually matter, but keeping this
        // one right after -I's own keeps the two search-path flags next to
        // each other for anyone reading top to bottom.
        } else if (Arg.starts_with("-Fi") && Arg.size() > 3) {
            IncludeSearchPaths.push_back(Arg.substr(3));
        } else if (Arg == "-Fi") {
            if (I + 1 >= Argc) {
                report(diag::err_arg_requires_value, {"-Fi"});
                return 1;
            }
            IncludeSearchPaths.push_back(Argv[++I]);
        } else if (Arg.starts_with("-ferror-limit=")) {
            const std::string N = Arg.substr(14);
            if (N.find_first_not_of("0123456789") != std::string::npos ||
                N.empty()) {
                report(diag::err_ferror_limit_not_a_number);
                return 1;
            }
            // N is all-digits and non-empty, but that alone does not mean it
            // fits in ErrorLimit's unsigned range -- std::stoul would throw
            // std::out_of_range on a value like that, and this build is
            // -fno-exceptions (see the from_chars use in ParseExpr.cpp's
            // integer-literal handling for the same reason), so parse with
            // std::from_chars and check its error code instead of a type
            // that can throw.
            unsigned Parsed = 0;
            auto [Ptr, Ec] = std::from_chars(N.data(), N.data() + N.size(), Parsed);
            if (Ec != std::errc{}) {
                report(diag::err_ferror_limit_too_large, {N});
                return 1;
            }
            DiagnosticOptions O = Diags.options();
            O.ErrorLimit = Parsed;
            Diags.setOptions(std::move(O));
        } else if (Arg == "-Wall") {
            // Acted on by the prescan above.
        } else if (Arg.starts_with("-Wno-") && Arg.size() > 5) {
            const std::string Name = Arg.substr(5);
            if (Name != "all" && getWarningNamed(Name) == diag::none)
                report(diag::warn_unknown_warning_name, {Arg});
            // DisabledWarnings, and -Wno-all's SuppressWarnings, were
            // already captured by the prescan above.
        } else if (Arg.starts_with("-W") && Arg.size() > 2) {
            const std::string Name = Arg.substr(2);
            if (getWarningNamed(Name) == diag::none)
                report(diag::warn_unknown_warning_name, {Arg});
            // DisabledWarnings already updated by the prescan above.
        } else if (!Arg.empty() && Arg[0] == '-') {
            report(diag::warn_unrecognized_argument, {Arg});
        } else {
            if (!InputFile.empty()) {
                report(diag::err_multiple_input_files);
                return 1;
            }
            InputFile = Arg;
        }
    }

    // -Werror can turn a warning reported above (an unknown -W name, an
    // unrecognized argument) into an error; checked once here, after the
    // whole command line has been read, so a -Werror anywhere on the line
    // still catches a warning from earlier on it -- order-independent, the
    // same way Driver::run() checks its own Diags_ once after parseArgs
    // rather than after each diagnostic.
    if (Diags.hasErrors()) return 1;

    if (InputFile.empty()) {
        usagePC1();
        return 1;
    }

    LangOptions Opts;
    // Every dialect, not just Extended Pascal.  This tested one name and made
    // everything else ISO 7185, so -std=turbo would have compiled as ISO 7185
    // and said nothing -- harmless only for as long as the check above rejects
    // turbo before this is reached, which is exactly what Tier 1 removes.
    if (const auto D = LangOptions::parseDialect(Std)) Opts.Std = *D;
    // Real Turbo Pascal ships with {$R-}: range checking off by default.
    // ISO 7185 and Extended Pascal keep plang's long-standing default of on.
    // value_or, not a plain assignment, so an explicit -frange-checks /
    // -fno-range-checks (RangeChecks already holding true/false) still wins
    // over the dialect's own starting point.
    Opts.RangeChecks       = RangeChecks.value_or(!Opts.turbo());
    Opts.NilChecks         = NilChecks;
    Opts.OptLevel          = OptLevel;
    Opts.Debug             = Debug;
    Opts.TargetTriple      = std::move(Target);
    // The one llvm::Triple query LangOptions::PointerWidthBits needs (see its
    // comment): made here, where Frontend.cpp already depends on LLVM for
    // printVersion's own Triple use below, so that Sema/TypeContext can stay
    // free of the dependency and just read the plain integer this leaves in
    // Opts.  isArch32Bit() rather than a hand-rolled architecture-name list:
    // it is the same fact CodeGen's real DataLayout is about to derive from
    // this identical triple string, by construction rather than by two
    // implementations happening to agree, and it is what
    // CGTypes::checkSizeAgreement/checkFieldOffsetAgreement's cross-check
    // (issue #243's follow-up) needs Sema's answer to actually match.
    if (!Opts.TargetTriple.empty()) {
        const llvm::Triple T(llvm::Triple::normalize(Opts.TargetTriple));
        Opts.PointerWidthBits = T.isArch32Bit() ? 32 : 64;
    }
    Opts.ModuleSearchPaths = std::move(ModuleSearchPaths);
    Opts.IncludeSearchPaths = std::move(IncludeSearchPaths);
    // Tier 4, Cluster B item 4: the default installed-RTL search path a
    // `uses` clause falls back to with no -I and no PLANG_UNIT_DIR override
    // -- see UnitSearchPaths's own comment for why this is a third list
    // rather than folded into ModuleSearchPaths above.
    Opts.UnitSearchPaths = unitSearchPaths(findInstallDir(Argc > 0 ? Argv[0] : nullptr));

    // Opts.Defines: the predefined platform symbols first (the baseline),
    // then -d/-u in the order given on the command line, so a later -u can
    // undo an earlier -d or a predefined symbol and vice versa -- see
    // LangOptions::Defines's own comment.  Gated on Opts.turbo(): a
    // `{$IFDEF}` cannot appear at all in any other dialect (see
    // Directives.cpp), so building this set for one would be pure waste,
    // and -dSYMBOL under -std=iso7185 silently defining nothing is the
    // right answer, not a diagnostic -- the same way -Wno-<name> is
    // accepted, just inert, under a dialect that never raises that warning.
    if (Opts.turbo()) {
        addPredefinedConditionalSymbols(Opts);
        for (const auto &[Define, Symbol] : DefineOps) {
            if (!looksLikeIdentifier(Symbol)) {
                report(diag::warn_invalid_define_symbol, {Symbol});
                continue;
            }
            const std::string Folded = toLower(Symbol);
            if (Define) Opts.Defines.insert(Folded);
            else        Opts.Defines.erase(Folded);
        }
    }

    // Route output: a dump mode or LLVM IR, to stdout or a named file.  Moved
    // above the Scanner/Parser/Sema pipeline so -dump-tokens (Scanner-only)
    // and -dump-parse-tree (Scanner+Parser, no Sema) can both use it to stop
    // the pipeline early, the same way -dump-ast already does further down.
    auto withOutput = [&](auto action) -> int {
        if (OutputFile.empty()) {
            action(std::cout);
            std::cout.flush();
            return reportIfWriteFailed(std::cout, "") ? 1 : 0;
        }
        std::ofstream F(OutputFile);
        if (!F) {
            report(diag::err_cannot_open_output_file, {OutputFile});
            return 1;
        }
        action(F);
        F.close();
        return reportIfWriteFailed(F, OutputFile) ? 1 : 0;
    };

    // A path that exists but names a directory reads back empty through
    // std::ifstream (issue #275 -- the front end's own entry point never got
    // the guard issue #125/#134 added to the driver's): without this, Scanner
    // below would construct successfully with no tokens at all, and every
    // stage past it would report nothing but "expected 'program', got end of
    // file" and the cascade that follows, instead of one diagnostic naming
    // the real problem.
    if (llvm::sys::fs::is_directory(InputFile)) {
        report(diag::err_is_a_directory, {InputFile});
        return 1;
    }

    Scanner Sc(SrcMgr, InputFile, Diags, Opts);
    if (Diags.hasErrors()) { emitAll(); return 1; }
    // Captured before the move below takes Sc apart; -g's DIFile/DICompileUnit
    // need it and have no other way to ask which buffer was the main one.
    const FileID MainFileID = Sc.fileID();

    if (DumpTokens) {
        const int Rc = withOutput([&](std::ostream& Os) {
            for (;;) {
                const Token T = Sc.next();
                const PresumedLoc PL = SrcMgr.getPresumedLoc(T.Loc);
                Os << PL.Line << ':' << PL.Column << ": " << kindName(T.Kind)
                   << " \"" << T.Lexeme << "\"\n";
                if (T.Kind == TokenKind::Eof) break;
            }
        });
        if (Diags.hasErrors()) { emitAll(); return 1; }
        return Rc;
    }

    Parser P(std::move(Sc), Diags, Opts);
    auto Program = P.parse();
    if (!Program) { emitAll(); return 1; }

    // Parser::switches() forwards to the Scanner it moved Sc into, which has
    // now read the whole token stream (every `{$I file}`/`{$INCLUDE file}`
    // it spliced in along the way included) and so has seen every `{$R+}`-
    // style switch directive there is to see.  Opts itself is a plain value
    // copied into Sc/P at their own construction above (see Scanner::Opts's
    // own comment for why), so nothing below sees what Sc recorded unless
    // this attaches it back on -- null for every ISO 7185/Extended Pascal
    // compile, and for a Turbo one that never wrote a switch directive,
    // which is exactly what keeps LangOptions::switchOn on its no-table fast
    // path for both (SwitchTable.h's own "null means none" contract).
    Opts.Switches = P.switches();

    if (DumpParseTree)
        return withOutput([&](std::ostream& Os) { printAst(*Program, Os); });

    // Turbo Tier 4, Cluster A item 1 taught a standalone unit file to run
    // through Sema for real (Sema::checkUnit).  Cluster A item 2 replaces
    // item 1's own "type-checks, but stops there" placeholder
    // (err_unit_compilation_not_yet_supported, now unused -- kept in the
    // catalog as a historical diagnostic, the same way this codebase treats
    // every other retired-but-still-formattable message) with real
    // separate-compilation codegen: publish this unit's own .tui (its
    // INTERFACE, written back out as Pascal text -- buildTUIContent/
    // writeTUIFile just above) and emit its object code (Codegen::emitUnit)
    // exactly the way a program does just below, minus the parts (a `main`,
    // .pmi writing) that only apply to one.
    if (Program->BareUnit) {
        const UnitNode& Unit = *Program->BareUnit;
        Sema UnitSem(Diags, Opts);
        bool UnitOk = UnitSem.checkUnit(Unit);
        emitAll();
        if (!UnitOk) return 1;
        if (DumpAst)
            return withOutput([&](std::ostream& Os) { printAst(*Program, Os); });
        if (DumpVmt)
            return withOutput([&](std::ostream& Os) { printVmt(*Program, Os); });

        // Publish this unit's own .tui before codegen, mirroring the
        // program path's own "publish interfaces, then emit" order just
        // below -- a compile that is about to fail (codegen verification,
        // an I/O error opening -o) must not have left a stale-looking but
        // untrustworthy interface file behind, but writeTUIFile's own
        // atomic write-temp-then-rename already makes ITS OWN publish
        // failure impossible to observe as a corrupt file, so there is
        // nothing further to roll back here the way writePMIFiles' own
        // multi-module cleanup has to.
        if (!writeTUIFile(Unit, InputFile)) return 1;

        Codegen Cg(Opts);
        // What this unit's own 'uses' clauses import, for CGLinkage's own
        // mangling (importOwner/importLinkName) to resolve a call/reference
        // to a used unit's export to the right pas_<unit>$<name> symbol --
        // Sema::pushUnitUsesScopes now fills this in for a Turbo 'uses'
        // exactly as Sema::processImports already does for EP's own
        // 'import', see that function's own comment.
        Cg.setImportOwners(UnitSem.importOwners());
        // A unit may itself 'uses' other units, in both its interface and
        // implementation sections (UnitNode::InterfaceUses/
        // ImplementationUses) -- registered the same way a program's own
        // top-level 'uses' is, just below, so this unit's own declarations
        // can read a used unit's constants/variables and call its
        // procedures exactly as a program compiled against it can.
        std::vector<const UnitNode*> UsedUnits;
        for (const auto& U : Unit.InterfaceUses)
            if (const UnitNode* UN = UnitSem.loadedUnit(toLower(U.Name)))
                UsedUnits.push_back(UN);
        for (const auto& U : Unit.ImplementationUses)
            if (const UnitNode* UN = UnitSem.loadedUnit(toLower(U.Name)))
                UsedUnits.push_back(UN);
        if (!UsedUnits.empty()) Cg.setUsedUnits(std::move(UsedUnits));
        if (Opts.Debug) Cg.setSourceManager(SrcMgr, MainFileID);

        if (OutputFile.empty()) {
            const bool EmitOk = Cg.emitUnit(Unit, std::cout);
            std::cout.flush();
            if (EmitOk && reportIfWriteFailed(std::cout, "")) return 1;
            return EmitOk ? 0 : 1;
        }
        std::ofstream F(OutputFile);
        if (!F) {
            report(diag::err_cannot_open_output_file, {OutputFile});
            return 1;
        }
        const bool EmitOk = Cg.emitUnit(Unit, F);
        F.close();
        if (EmitOk && reportIfWriteFailed(F, OutputFile)) return 1;
        return EmitOk ? 0 : 1;
    }

    Sema Sem(Diags, Opts);
    bool Ok = Sem.check(*Program);
    emitAll();
    if (!Ok) return 1;

    // -dump-ast is a read-only inspection mode -- like -dump-tokens and
    // -dump-parse-tree above, it must return before anything that writes to
    // the source tree.  Checked before writePMIFiles below, not after: Sema
    // has already run by this point (the AST dump reflects its results), but
    // the .pmi side effect that normal compilation needs for later separate
    // compilation must not happen just because someone asked to look at the
    // AST.
    if (DumpAst)
        return withOutput([&](std::ostream& Os) { printAst(*Program, Os); });
    // -dump-vmt: same read-only-inspection placement as -dump-ast just
    // above, and for the identical reason -- Sema has already run (the VMT
    // dump reflects its results), but must return before any side effect
    // (.pmi writing, ...) that normal compilation needs.
    if (DumpVmt)
        return withOutput([&](std::ostream& Os) { printVmt(*Program, Os); });

    // Write .pmi files for any module bodies found in this compilation unit.
    // This is a no-op for pure-program files (no OwnedModules). A failure
    // here has either already been diagnosed to stderr directly (an I/O
    // failure) or reported through Diags (a declaration that could not be
    // serialized into the interface, issue #397 -- emitAll below prints it);
    // either way, let it fail the compile rather than report success for a
    // module nothing can now import, or that some later, unrelated compile
    // will fail importing with no clue why.
    if (!Program->OwnedModules.empty()) {
        const bool PmiOk = writePMIFiles(*Program, InputFile, Diags);
        emitAll();
        if (!PmiOk) return 1;
    }

    // withOutput opens the file then calls the action; we need emit's bool result.
    Codegen Cg(Opts);
    Cg.setImportOwners(Sem.importOwners());
    Cg.setLoadedInterfaces(Sem.loadedInterfaces());
    // Turbo Tier 4, Cluster A item 1: the narrow codegen support this item's
    // own runtime shadowing test needs -- see Codegen::setUsedUnits's own
    // comment for exactly what it does and does not cover.  Sem already
    // parsed and checked every unit this program's own 'uses' clause named
    // (Sema::loadUnitInterfaceExports); this just hands CodeGen the same
    // already-loaded ASTs back, in the same 'uses' order, rather than having
    // CodeGen re-find and re-parse the files itself.
    if (Opts.turbo() && !Program->Uses.empty()) {
        std::vector<const UnitNode*> UsedUnits;
        for (const auto& U : Program->Uses)
            if (const UnitNode* UN = Sem.loadedUnit(toLower(U.Name)))
                UsedUnits.push_back(UN);
        Cg.setUsedUnits(std::move(UsedUnits));
    }
    if (Opts.Debug) Cg.setSourceManager(SrcMgr, MainFileID);
    if (OutputFile.empty()) {
        const bool EmitOk = Cg.emit(*Program, std::cout);
        std::cout.flush();
        if (EmitOk && reportIfWriteFailed(std::cout, "")) return 1;
        return EmitOk ? 0 : 1;
    }

    std::ofstream F(OutputFile);
    if (!F) {
        report(diag::err_cannot_open_output_file, {OutputFile});
        return 1;
    }
    const bool EmitOk = Cg.emit(*Program, F);
    F.close();
    if (EmitOk && reportIfWriteFailed(F, OutputFile)) return 1;
    return EmitOk ? 0 : 1;
}

} // namespace plang
