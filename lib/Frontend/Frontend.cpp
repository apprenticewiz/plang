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
#include "plang/Parse/Parser.h"
#include "plang/Lex/Scanner.h"
#include "plang/Sema/Sema.h"

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
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

namespace plang {

// See NumExprKinds in AstBase.h.  The interface writer takes the two together:
// a kind exprToString learns to write has to be one canSerializeExpr agrees can
// be written, or the gate keeps out what the writer could have said, and a kind
// only the gate learns lets through what the writer will then write as a zero.
static_assert(NumExprKinds == 16,
              "a new expression may need a case in exprToString and in "
              "canSerializeExpr");
static_assert(NumTypeKinds == 14,
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

// ---------------------------------------------------------------------------
// PMI writer — serialize a module's exported declarations to Pascal text
// ---------------------------------------------------------------------------

/// Convert an ExprNode back to a Pascal constant expression string.
/// Only handles the simple cases that appear in type bounds.
static std::string exprToString(const ExprNode& E) {
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
                 + exprToString(*UN->Operand);
    }
    // EP §6.8.2: a constant may be written as an expression over other
    // constants, and the interface has to carry the expression, not a number
    // it might or might not fold to here.  Parentheses because the text is
    // read back on its own, with none of the surrounding to hold it together.
    if (auto* BN = llvm::dyn_cast<BinaryExpr>(&E)) {
        if (BN->Left && BN->Right)
            return "(" + exprToString(*BN->Left) + " "
                 + std::string(opSpelling(BN->Op)) + " "
                 + exprToString(*BN->Right) + ")";
    }
    if (llvm::isa<NilExpr>(&E)) return "nil";
    // EP §6.8.2: `ord('a')` and its like are constant expressions too, and
    // read back as what they were written as.
    if (auto* CE = llvm::dyn_cast<CallExpr>(&E)) {
        std::string R = CE->Name + "(";
        for (size_t I = 0; I < CE->Args.size(); ++I) {
            if (I) R += ", ";
            R += exprToString(*CE->Args[I]);
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
        return exprToString(*RG->Low) + ".." + exprToString(*RG->High);
    // EP §6.6: an initial state may be a whole array, record or set, and the
    // interface has to keep saying what state that is.
    if (auto* SL = llvm::dyn_cast<SetLiteralExpr>(&E)) {
        std::string R = SL->TypeName + "[";
        for (size_t I = 0; I < SL->Elements.size(); ++I) {
            if (I) R += ", ";
            R += exprToString(*SL->Elements[I]);
        }
        return R + "]";
    }
    if (auto* SV = llvm::dyn_cast<StructuredValueExpr>(&E)) {
        std::string R = SV->TypeName + "[";
        for (size_t I = 0; I < SV->Arms.size(); ++I) {
            const auto& Arm = SV->Arms[I];
            if (I) R += "; ";
            if (Arm.IsOtherwise) {
                R += "otherwise ";
            } else {
                for (size_t J = 0; J < Arm.Labels.size(); ++J) {
                    if (J) R += ", ";
                    R += exprToString(*Arm.Labels[J]);
                }
                R += ": ";
            }
            if (Arm.Value) R += exprToString(*Arm.Value);
        }
        return R + "]";
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
        if (StrT->Capacity)
            return "string(" + exprToString(*StrT->Capacity) + ")";
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
/// which case everything the body declares is exported.
static std::string buildPMIContent(const ModuleNode& Mod,
                                    const ModuleNode* Iface) {
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
    auto isStructured = [](const ConstDef& Cd) {
        return llvm::isa<StructuredValueExpr>(Cd.Value.get());
    };
    auto writeConst = [&](const ConstDef& Cd) {
        if (!Cd.Value || !canSerializeExpr(*Cd.Value)) return;
        PMI += "const " + Cd.Name + " = " + exprToString(*Cd.Value) + ";\n";
    };
    for (const auto& Cd : Decls.Consts)
        if (Cd.Value && !isStructured(Cd)) writeConst(Cd);

    // Type definitions next (so procs that reference them can resolve them).
    for (const auto& Td : Decls.Types) {
        std::string T = Td.Type ? typeNodeToString(*Td.Type) : std::string();
        if (T.empty()) continue;
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
        if (T.empty()) continue;
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
        if (!Ok) continue;
        PMI += Heading + ";\n";
    }

    PMI += "end.\n";
    return PMI;
}

/// Write PMI files for all module bodies in the program.
/// PMI files are written to the same directory as the input source file.
static void writePMIFiles(const ProgramNode& Program,
                           const std::string& InputFile) {
    // Determine directory from input file path.
    std::string PmiDir = ".";
    {
        auto Slash = InputFile.rfind('/');
        if (Slash != std::string::npos)
            PmiDir = InputFile.substr(0, Slash);
    }

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

        std::string Content = buildPMIContent(*Mod, Iface);
        if (Content.empty()) continue;

        std::string PmiPath = PmiDir + "/" + Mod->Name + ".pmi";
        std::ofstream F(PmiPath);
        if (F) F << Content;
    }
}

} // namespace

int frontendPC1Main(int Argc, char *Argv[]) {
    // The front end is a separate process from the driver and prints its own
    // diagnostics, so it resolves its own catalog.  This is a prescan rather
    // than a case in the loop below, because that loop reports as it goes and
    // a locale chosen partway through would translate only the messages after
    // it.
    {
        std::string_view Lang;
        bool ShowFuzzy = false;
        constexpr std::string_view LangOpt = "-fdiagnostics-language=";
        for (int I = 2; I < Argc; ++I) {
            const std::string_view A = Argv[I];
            if (A.starts_with(LangOpt))            Lang = A.substr(LangOpt.size());
            else if (A == "-fdiagnostics-show-fuzzy") ShowFuzzy = true;
        }
        (void)selectLocale(Lang, findInstallDir(Argc > 0 ? Argv[0] : nullptr),
                           ShowFuzzy);
    }

    std::string              InputFile;
    std::string              OutputFile;
    std::string              Std;
    bool                     SuppressWarnings = false;
    bool                     WarningsAsErrors = false;
    bool                     RangeChecks      = true;
    bool                     NilChecks        = true;
    unsigned                 OptLevel         = 0;
    bool                     Debug            = false;
    bool                     DumpAst          = false;
    bool                     DumpTokens       = false;
    bool                     DumpParseTree    = false;
    std::vector<std::string> ModuleSearchPaths;
    std::vector<std::string> DisabledWarnings;
    unsigned                 ErrorLimit = 0;
    ColorDiagnostics         Color      = ColorDiagnostics::Auto;

    // Options start at Argv[2]; Argv[0]="plang", Argv[1]="-pc1".
    for (int I = 2; I < Argc; ++I) {
        std::string Arg = Argv[I];

        if (Arg == "--version") {
            printVersion(Argc > 0 ? Argv[0] : nullptr);
            return 0;
        } else if (Arg == "-h" || Arg == "--help") {
            usagePC1();
            return 0;
        } else if (Arg == "-o") {
            if (I + 1 >= Argc) {
                std::cerr << "plang -pc1: -o requires an argument\n";
                return 1;
            }
            OutputFile = Argv[++I];
        } else if (Arg.starts_with("-fdiagnostics-language=") ||
                   Arg == "-fdiagnostics-show-fuzzy") {
            // Acted on by the prescan above.  The arm is still required: this
            // chain ends in "unrecognized argument", and the driver forwards
            // both of these, so without it every driver-mediated compile would
            // warn about an option the driver itself just passed on.
        } else if (Arg.starts_with("-std=")) {
            Std = Arg.substr(5);
            if (!LangOptions::parseDialect(Std)) {
                std::cerr << "plang -pc1: unknown Pascal dialect '" << Std
                          << "'; known: " << LangOptions::knownDialects() << "\n";
                return 1;
            }
            if (!LangOptions::isImplementedDialect(Std)) {
                std::cerr << "plang -pc1: dialect '" << Std
                          << "' is not yet implemented; implemented dialects: "
                          << LangOptions::implementedDialects() << "\n";
                return 1;
            }
        } else if (Arg == "-w") {
            SuppressWarnings = true;
        } else if (Arg == "-Werror") {
            WarningsAsErrors = true;
        } else if (Arg == "-frange-checks") {
            RangeChecks = true;
        } else if (Arg == "-fno-range-checks") {
            RangeChecks = false;
        } else if (Arg == "-fnil-checks") {
            NilChecks = true;
        } else if (Arg == "-fno-nil-checks") {
            NilChecks = false;
        } else if (Arg == "-fcolor-diagnostics" || Arg == "-fno-color-diagnostics") {
            Color = colorDiagnosticsArg(Arg);
        } else if (Arg.size() == 3 && Arg.starts_with("-O")
                                   && Arg[2] >= '0' && Arg[2] <= '3') {
            OptLevel = static_cast<unsigned>(Arg[2] - '0');
        } else if (Arg == "-dump-ast") {
            DumpAst = true;
        } else if (Arg == "-dump-tokens") {
            DumpTokens = true;
        } else if (Arg == "-dump-parse-tree") {
            DumpParseTree = true;
        } else if (Arg == "-g") {
            Debug = true;
        } else if (Arg.starts_with("-I") && Arg.size() > 2) {
            ModuleSearchPaths.push_back(Arg.substr(2));
        } else if (Arg == "-I") {
            if (I + 1 >= Argc) {
                std::cerr << "plang -pc1: -I requires an argument\n";
                return 1;
            }
            ModuleSearchPaths.push_back(Argv[++I]);
        } else if (Arg.starts_with("-ferror-limit=")) {
            const std::string N = Arg.substr(14);
            if (N.find_first_not_of("0123456789") != std::string::npos ||
                N.empty()) {
                std::cerr << "plang -pc1: -ferror-limit= requires a number\n";
                return 1;
            }
            ErrorLimit = static_cast<unsigned>(std::stoul(N));
        } else if (Arg == "-Wall") {
            // Every warning is on already; -Wall is accepted so that a command
            // line written for another compiler does not have to be edited.
            DisabledWarnings.clear();
        } else if (Arg.starts_with("-Wno-") && Arg.size() > 5) {
            std::string Name = Arg.substr(5);
            if (Name == "all") { SuppressWarnings = true; continue; }
            if (getWarningNamed(Name) == diag::none) {
                std::cerr << "plang -pc1: warning: unknown warning '" << Arg
                          << "'\n";
                continue;
            }
            DisabledWarnings.push_back(std::move(Name));
        } else if (Arg.starts_with("-W") && Arg.size() > 2) {
            std::string Name = Arg.substr(2);
            if (getWarningNamed(Name) == diag::none) {
                std::cerr << "plang -pc1: warning: unknown warning '" << Arg
                          << "'\n";
                continue;
            }
            std::erase(DisabledWarnings, Name);
        } else if (!Arg.empty() && Arg[0] == '-') {
            std::cerr << "plang -pc1: warning: unrecognized argument '" << Arg << "'\n";
        } else {
            if (!InputFile.empty()) {
                std::cerr << "plang -pc1: error: only one input file is supported\n";
                return 1;
            }
            InputFile = Arg;
        }
    }

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
    Opts.RangeChecks       = RangeChecks;
    Opts.NilChecks         = NilChecks;
    Opts.OptLevel          = OptLevel;
    Opts.Debug             = Debug;
    Opts.ModuleSearchPaths = std::move(ModuleSearchPaths);

    DiagnosticOptions DiagOpts;
    DiagOpts.SuppressWarnings = SuppressWarnings;
    DiagOpts.WarningsAsErrors = WarningsAsErrors;
    DiagOpts.DisabledWarnings = std::move(DisabledWarnings);
    DiagOpts.ErrorLimit       = ErrorLimit;

    DiagnosticsEngine Diags(std::move(DiagOpts));
    SourceManager     SrcMgr;

    DiagnosticPrinter Printer(SrcMgr, useColor(Color, isatty(STDERR_FILENO)));
    auto emitAll = [&] {
        for (const auto &D : Diags) std::cerr << Printer.print(D) << "\n";
    };

    // Route output: a dump mode or LLVM IR, to stdout or a named file.  Moved
    // above the Scanner/Parser/Sema pipeline so -dump-tokens (Scanner-only)
    // and -dump-parse-tree (Scanner+Parser, no Sema) can both use it to stop
    // the pipeline early, the same way -dump-ast already does further down.
    auto withOutput = [&](auto action) -> int {
        if (OutputFile.empty()) {
            action(std::cout);
            return 0;
        }
        std::ofstream F(OutputFile);
        if (!F) {
            std::cerr << "plang -pc1: cannot open output file '" << OutputFile << "'\n";
            return 1;
        }
        action(F);
        return 0;
    };

    Scanner Sc(SrcMgr, InputFile, Diags, Opts);
    if (!Diags.empty()) { emitAll(); return 1; }
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
        if (!Diags.empty()) { emitAll(); return 1; }
        return Rc;
    }

    Parser P(std::move(Sc), Diags, Opts);
    auto Program = P.parse();
    if (!Program) { emitAll(); return 1; }

    if (DumpParseTree)
        return withOutput([&](std::ostream& Os) { printAst(*Program, Os); });

    Sema Sem(Diags, Opts);
    bool Ok = Sem.check(*Program);
    emitAll();
    if (!Ok) return 1;

    // Write .pmi files for any module bodies found in this compilation unit.
    // This is a no-op for pure-program files (no OwnedModules).
    if (!Program->OwnedModules.empty())
        writePMIFiles(*Program, InputFile);

    if (DumpAst)
        return withOutput([&](std::ostream& Os) { printAst(*Program, Os); });

    // withOutput opens the file then calls the action; we need emit's bool result.
    Codegen Cg(Opts);
    Cg.setImportOwners(Sem.importOwners());
    Cg.setLoadedInterfaces(Sem.loadedInterfaces());
    if (Opts.Debug) Cg.setSourceManager(SrcMgr, MainFileID);
    if (OutputFile.empty())
        return Cg.emit(*Program, std::cout) ? 0 : 1;

    std::ofstream F(OutputFile);
    if (!F) {
        std::cerr << "plang -pc1: cannot open output file '" << OutputFile << "'\n";
        return 1;
    }
    return Cg.emit(*Program, F) ? 0 : 1;
}

} // namespace plang
