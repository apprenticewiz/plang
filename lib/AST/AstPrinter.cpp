#include <ostream>
#include <string>
#include <string_view>

#include "plang/AST/Ast.h"
#include "llvm/Support/Casting.h"
#include "plang/AST/AstPrinter.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Basic/Token.h"

using namespace plang;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Returns a string of 'depth * 2' spaces for indentation.
static std::string ind(int depth) {
    return std::string(depth * 2, ' ');
}

// Returns the operator symbol used in S-expression output.
static std::string_view opSym(TokenKind k) { return spelling(k); }

// Writes its separator before every item but the first, for the
// space-separated lists this file is made of:
//
//     Sep sp;
//     for (const auto& name : Names) os << sp << name;
//
// The loops used to enumerate the list and test the index for zero, which was
// the only thing any of them wanted an index for.  std::views::enumerate is
// also absent from libc++, so plang did not build against it.
class Sep {
public:
    explicit Sep(std::string_view S = " ") : S_(S) {}

    friend std::ostream& operator<<(std::ostream& OS, Sep& Sp) {
        if (!Sp.First_) OS << Sp.S_;
        Sp.First_ = false;
        return OS;
    }

private:
    std::string_view S_;
    bool             First_ = true;
};

// Forward declarations.
static void printExpr(const ExprNode& node, std::ostream& os);
static void printType(const TypeNode& node, std::ostream& os);

// Prints a statement starting at the current column (no leading indent).
// Does NOT emit a trailing newline. 'depth' controls how deeply
// sub-statements are indented when they begin on new lines.
static void printStmt(const StmtNode* node, std::ostream& os, int depth);

// Prints all declarations and the body of a block, each prefixed with
// ind(depth). The last item (the body statement) is printed WITHOUT a
// trailing newline so the caller can append a closing ')' on the same line.
static void printBlock(const BlockNode& node, std::ostream& os, int depth);

// Prints a full procedure/function form starting at ind(depth). Ends with '\n'.
static void printProc(const ProcDecl& node, std::ostream& os, int depth);

// A procedural parameter's type is itself a parameter list, so these two recur.
static void printParams(const std::vector<ParamGroup>& params, std::ostream& os);

// See NumExprKinds in AstBase.h.  A dump that leaves a construct out reads as
// a tree that does not contain one, which is the most misleading thing this
// file could do, and it is what happened to every EP node added after it.
static_assert(NumExprKinds == 18, "a new expression needs a case in printExpr");
static_assert(NumStmtKinds == 13, "a new statement needs a case in printStmt");
static_assert(NumTypeKinds == 15, "a new type denoter needs a case in printType");

// ---------------------------------------------------------------------------
// Inline type printing
// ---------------------------------------------------------------------------

// The dispatch is a switch over NodeKind with no default: a kind added to the
// enum stops the build here rather than printing nothing and letting a reader
// conclude the tree does not have one.  Three of these were missed for exactly
// that reason — `type of`, a conformant array parameter and a schema
// instantiation each dumped as blank for as long as they have existed.
static void printType(const TypeNode& node, std::ostream& os) {
    switch (node.Kind) {
    case NodeKind::NamedTypeNode: {
        const auto* n = llvm::cast<NamedTypeNode>(&node);
        if (n->Restricted) os << "(restricted " << n->Name << ")";
        else               os << n->Name;
        break;
    }
    case NodeKind::ArrayTypeNode: {
        const auto* n = llvm::cast<ArrayTypeNode>(&node);
        os << (n->Packed ? "(packed-array " : "(array ");
        if (n->Index) {
            printType(*n->Index, os);
        } else {
            printExpr(*n->Low, os);
            os << " ";
            printExpr(*n->High, os);
        }
        os << " ";
        printType(*n->Element, os);
        os << ")";
        break;
    }
    case NodeKind::RecordTypeNode: {
        const auto* n = llvm::cast<RecordTypeNode>(&node);
        os << (n->Packed ? "(packed-record" : "(record");
        for (const auto& fd : n->Fields) {
            os << " (";
            Sep sp;
            for (const auto& name : fd.Names) os << sp << name;
            os << " ";
            printType(*fd.Type, os);
            os << ")";
        }
        if (n->Variant) {
            const auto& vp = *n->Variant;
            os << " (case";
            if (!vp.TagField.empty()) os << " " << vp.TagField << " :";
            os << " ";
            printType(*vp.TagType, os);
            for (const auto& vc : vp.Cases) {
                os << " (";
                Sep lsp;
                for (const auto& lbl : vc.Labels) {
                    os << lsp;
                    printExpr(*lbl, os);
                }
                os << " :";
                for (const auto& fd : vc.Fields) {
                    os << " (";
                    Sep nsp;
                    for (const auto& name : fd.Names) os << nsp << name;
                    os << " ";
                    printType(*fd.Type, os);
                    os << ")";
                }
                os << ")";
            }
            os << ")";
        }
        os << ")";
        break;
    }
    case NodeKind::ObjectTypeNode: {
        const auto* n = llvm::cast<ObjectTypeNode>(&node);
        os << "(object";
        if (!n->Ancestor.empty()) os << " (ancestor " << n->Ancestor << ")";
        for (const auto& m : n->Members) {
            os << " ("
               << (m.Vis == MemberVisibility::Private ? "private " : "public ");
            if (m.IsMethod) {
                const auto& pd = *m.Method;
                os << (pd.IsConstructor ? "constructor" :
                       pd.IsDestructor  ? "destructor"  :
                       pd.IsFunction    ? "function"    : "procedure")
                   << " " << pd.Name << " ";
                printParams(pd.Params, os);
                if (pd.IsFunction && pd.ReturnType) {
                    os << " ";
                    printType(*pd.ReturnType, os);
                }
                if (pd.IsVirtual) os << " virtual";
                if (pd.IsAbstract) os << " abstract";
            } else {
                os << "(";
                Sep sp;
                for (const auto& name : m.Field.Names) os << sp << name;
                os << " ";
                printType(*m.Field.Type, os);
                os << ")";
            }
            os << ")";
        }
        os << ")";
        break;
    }
    case NodeKind::PointerTypeNode: {
        const auto* n = llvm::cast<PointerTypeNode>(&node);
        os << "(^ ";
        printType(*n->Base, os);
        os << ")";
        break;
    }
    case NodeKind::SubrangeTypeNode: {
        const auto* n = llvm::cast<SubrangeTypeNode>(&node);
        os << "(subrange ";
        printExpr(*n->Low, os);
        os << " ";
        printExpr(*n->High, os);
        os << ")";
        break;
    }
    case NodeKind::EnumTypeNode: {
        const auto* n = llvm::cast<EnumTypeNode>(&node);
        os << "(enum";
        for (const auto& v : n->Values) os << " " << v;
        os << ")";
        break;
    }
    case NodeKind::SetTypeNode: {
        const auto* n = llvm::cast<SetTypeNode>(&node);
        os << (n->Packed ? "(packed-set " : "(set ");
        printType(*n->Base, os);
        os << ")";
        break;
    }
    case NodeKind::FileTypeNode: {
        const auto* n = llvm::cast<FileTypeNode>(&node);
        if (!n->Element && !n->Index) { os << "file"; break; }
        os << "(file";
        // EP §6.4.3.5: a direct-access file is written with the type it is
        // indexed by, which is as much a part of it as what it holds.
        if (n->Index) {
            os << " (index ";
            printType(*n->Index, os);
            os << ")";
        }
        if (n->Element) {
            os << " ";
            printType(*n->Element, os);
        }
        os << ")";
        break;
    }
    case NodeKind::PackedTypeNode: {
        const auto* n = llvm::cast<PackedTypeNode>(&node);
        os << "(packed ";
        printType(*n->Inner, os);
        os << ")";
        break;
    }
    case NodeKind::StringTypeNode: {
        const auto* n = llvm::cast<StringTypeNode>(&node);
        // Turbo string[N] (ShortString) and EP string(N) (VarString) share
        // this one AST node (see AstType.h's own comment) but are different
        // types with different binary layouts -- printing them alike here
        // would make a dump unable to tell which one a program actually
        // wrote, exactly the ambiguity this project's naming elsewhere goes
        // out of its way to avoid.
        os << (n->IsShortString ? "(shortstring" : "(string");
        // EP §6.4.3.3: written without a capacity in a parameter list.
        if (n->Capacity) { os << " "; printExpr(*n->Capacity, os); }
        os << ")";
        break;
    }
    case NodeKind::TypeOfNode:
        // EP §6.4.9: the type is the one the variable was declared with, and
        // the name of the variable is all the syntax has.
        os << "(type-of " << llvm::cast<TypeOfNode>(&node)->VarName << ")";
        break;
    case NodeKind::ConformantArrayTypeNode: {
        // ISO §6.6.3.7: the bounds are names the call binds, not values.
        const auto* n = llvm::cast<ConformantArrayTypeNode>(&node);
        os << (n->Packed ? "(packed-conformant-array" : "(conformant-array");
        for (const auto& s : n->Specs)
            os << " (" << s.Lo << " " << s.Hi << " " << s.OrdType << ")";
        os << " ";
        printType(*n->Element, os);
        os << ")";
        break;
    }
    case NodeKind::SchemaTypeNode: {
        // EP §6.4.8: the actual discriminants are what pick out the type.
        const auto* n = llvm::cast<SchemaTypeNode>(&node);
        os << "(schema " << n->Name;
        for (const auto& a : n->Actuals) { os << " "; printExpr(*a, os); }
        os << ")";
        break;
    }
    case NodeKind::ProcedureTypeNode: {
        const auto* n = llvm::cast<ProcedureTypeNode>(&node);
        os << (n->IsFunction ? "(function-param " : "(procedure-param ");
        printParams(n->Params, os);
        if (n->ReturnType) {
            os << " ";
            printType(*n->ReturnType, os);
        }
        os << ")";
        break;
    }
    default:
        // Not a type denoter at all; the caller had the wrong node.
        os << "(?type)";
        break;
    }
    // EP §6.6: the 'value' clause belongs to the denoter itself (see
    // TypeNode::InitialState's own comment) and every kind of denoter can
    // carry one, so it is checked once here rather than duplicated into
    // every case above -- matching the ' value ' spelling
    // Frontend.cpp's typeNodeToString already uses for the same field.
    if (node.InitialState) {
        os << " value ";
        printExpr(*node.InitialState, os);
    }
}

// ---------------------------------------------------------------------------
// Inline expression printing
// ---------------------------------------------------------------------------

static void printExpr(const ExprNode& node, std::ostream& os) {
    switch (node.Kind) {
    case NodeKind::IntLitExpr:
        os << llvm::cast<IntLitExpr>(&node)->Value;
        break;
    case NodeKind::RealLitExpr:
        os << llvm::cast<RealLitExpr>(&node)->Value;
        break;
    case NodeKind::StringLitExpr:
        os << '"' << llvm::cast<StringLitExpr>(&node)->Value << '"';
        break;
    case NodeKind::BoolLitExpr:
        os << (llvm::cast<BoolLitExpr>(&node)->Value ? "true" : "false");
        break;
    case NodeKind::NilExpr:
        os << "nil";
        break;
    case NodeKind::IdentExpr:
        os << llvm::cast<IdentExpr>(&node)->Name;
        break;
    case NodeKind::IndexExpr: {
        const auto* n = llvm::cast<IndexExpr>(&node);
        os << "(index ";
        printExpr(*n->Array, os);
        os << " ";
        printExpr(*n->Index, os);
        os << ")";
        break;
    }
    case NodeKind::FieldExpr: {
        const auto* n = llvm::cast<FieldExpr>(&node);
        os << "(field ";
        printExpr(*n->Record, os);
        os << " " << n->Field << ")";
        break;
    }
    case NodeKind::DerefExpr:
        os << "(deref ";
        printExpr(*llvm::cast<DerefExpr>(&node)->Pointer, os);
        os << ")";
        break;
    case NodeKind::BinaryExpr: {
        const auto* n = llvm::cast<BinaryExpr>(&node);
        os << "(" << opSym(n->Op) << " ";
        printExpr(*n->Left, os);
        os << " ";
        printExpr(*n->Right, os);
        os << ")";
        break;
    }
    case NodeKind::UnaryExpr: {
        const auto* n = llvm::cast<UnaryExpr>(&node);
        os << "(" << opSym(n->Op) << " ";
        printExpr(*n->Operand, os);
        os << ")";
        break;
    }
    case NodeKind::CallExpr: {
        const auto* n = llvm::cast<CallExpr>(&node);
        os << "(call " << n->Name;
        for (const auto& arg : n->Args) {
            os << " ";
            printExpr(*arg, os);
        }
        os << ")";
        break;
    }
    case NodeKind::MethodCallExpr: {
        const auto* n = llvm::cast<MethodCallExpr>(&node);
        os << "(methodcall ";
        printExpr(*n->Receiver, os);
        os << " " << n->Method;
        for (const auto& arg : n->Args) {
            os << " ";
            printExpr(*arg, os);
        }
        os << ")";
        break;
    }
    case NodeKind::SetRangeExpr: {
        const auto* n = llvm::cast<SetRangeExpr>(&node);
        os << "(.. ";
        printExpr(*n->Low, os);
        os << " ";
        printExpr(*n->High, os);
        os << ")";
        break;
    }
    case NodeKind::SetLiteralExpr: {
        const auto* n = llvm::cast<SetLiteralExpr>(&node);
        // EP §6.8.7.4: the type-name prefix is what tells checkSetLit this is
        // a TYPED constructor -- whose elements must match that type's base
        // type -- rather than the untyped [] literal, so leaving it out here
        // made the two indistinguishable in the dump.
        if (!n->TypeName.empty()) os << n->TypeName;
        os << "[";
        Sep sp;
        for (const auto& elem : n->Elements) {
            os << sp;
            printExpr(*elem, os);
        }
        os << "]";
        break;
    }
    case NodeKind::SubstringExpr: {
        const auto* n = llvm::cast<SubstringExpr>(&node);
        os << "(substring ";
        printExpr(*n->Str, os);
        os << " ";
        printExpr(*n->Low, os);
        os << "..";
        printExpr(*n->High, os);
        os << ")";
        break;
    }
    case NodeKind::StructuredValueExpr: {
        // EP §6.8.7: the type name is absent when the constructor stands for a
        // component of one written around it.
        const auto* n = llvm::cast<StructuredValueExpr>(&node);
        os << "(value";
        if (!n->TypeName.empty()) os << " " << n->TypeName;
        for (const auto& arm : n->Arms) {
            os << " (";
            if (arm.IsOtherwise) {
                os << "otherwise";
            } else {
                Sep sp;
                for (const auto& lbl : arm.Labels) {
                    os << sp;
                    printExpr(*lbl, os);
                }
            }
            if (arm.Value) {
                os << " : ";
                printExpr(*arm.Value, os);
            }
            os << ")";
        }
        os << ")";
        break;
    }
    case NodeKind::TypeCastExpr: {
        const auto* n = llvm::cast<TypeCastExpr>(&node);
        os << "(cast " << n->TypeName << " ";
        printExpr(*n->Operand, os);
        os << ")";
        break;
    }
    case NodeKind::WriteParam: {
        const auto* n = llvm::cast<WriteParam>(&node);
        os << "(write-param ";
        printExpr(*n->Value, os);
        if (n->Width) {
            os << " :";
            printExpr(*n->Width, os);
        }
        if (n->Decimals) {
            os << " :";
            printExpr(*n->Decimals, os);
        }
        os << ")";
        break;
    }
    default:
        os << "(?expr)";
        break;
    }
}

// ---------------------------------------------------------------------------
// Multi-line statement printing
// ---------------------------------------------------------------------------

static void printStmt(const StmtNode* node, std::ostream& os, int depth) {
    if (!node) {
        os << "()";   // empty / ε statement
        return;
    }

    switch (node->Kind) {
    case NodeKind::AssignStmt: {
        const auto* n = llvm::cast<AssignStmt>(node);
        os << "(assign ";
        printExpr(*n->Target, os);
        os << " ";
        printExpr(*n->Value, os);
        os << ")";
        break;
    }
    case NodeKind::CompoundStmt: {
        const auto* n = llvm::cast<CompoundStmt>(node);
        if (n->Stmts.empty()) {
            os << "(compound)";
        } else {
            os << "(compound";
            for (const auto& s : n->Stmts) {
                os << "\n" << ind(depth + 1);
                printStmt(s.get(), os, depth + 1);
            }
            os << ")";
        }
        break;
    }
    case NodeKind::IfStmt: {
        const auto* n = llvm::cast<IfStmt>(node);
        os << "(if ";
        printExpr(*n->Cond, os);
        os << "\n" << ind(depth + 1);
        printStmt(n->Then.get(), os, depth + 1);
        if (n->Else) {
            os << "\n" << ind(depth + 1);
            printStmt(n->Else.get(), os, depth + 1);
        }
        os << ")";
        break;
    }
    case NodeKind::WhileStmt: {
        const auto* n = llvm::cast<WhileStmt>(node);
        os << "(while ";
        printExpr(*n->Cond, os);
        os << "\n" << ind(depth + 1);
        printStmt(n->Body.get(), os, depth + 1);
        os << ")";
        break;
    }
    case NodeKind::ForStmt: {
        const auto* n = llvm::cast<ForStmt>(node);
        os << "(for " << n->Var << " := ";
        printExpr(*n->From, os);
        os << (n->Downto ? " downto " : " to ");
        printExpr(*n->Limit, os);
        os << "\n" << ind(depth + 1);
        printStmt(n->Body.get(), os, depth + 1);
        os << ")";
        break;
    }
    case NodeKind::ForInStmt: {
        // EP §6.9.3.9.3: the control variable takes each member of a set in
        // turn, so there is no bound to print — the set is the whole of it.
        const auto* n = llvm::cast<ForInStmt>(node);
        os << "(for-in " << n->Var << " ";
        printExpr(*n->SetExpr, os);
        os << "\n" << ind(depth + 1);
        printStmt(n->Body.get(), os, depth + 1);
        os << ")";
        break;
    }
    case NodeKind::RepeatStmt: {
        // Body is printed first (matching Pascal execution order), condition last.
        const auto* n = llvm::cast<RepeatStmt>(node);
        os << "(repeat";
        for (const auto& s : n->Stmts) {
            os << "\n" << ind(depth + 1);
            printStmt(s.get(), os, depth + 1);
        }
        os << "\n" << ind(depth + 1) << "(until ";
        printExpr(*n->Cond, os);
        os << "))";
        break;
    }
    case NodeKind::CallStmt: {
        const auto* n = llvm::cast<CallStmt>(node);
        os << "(call " << n->Name;
        for (const auto& arg : n->Args) {
            os << " ";
            printExpr(*arg, os);
        }
        os << ")";
        break;
    }
    case NodeKind::MethodCallStmt: {
        const auto* n = llvm::cast<MethodCallStmt>(node);
        os << "(methodcall ";
        printExpr(*n->Receiver, os);
        os << " " << n->Method;
        for (const auto& arg : n->Args) {
            os << " ";
            printExpr(*arg, os);
        }
        os << ")";
        break;
    }
    case NodeKind::WithStmt: {
        const auto* n = llvm::cast<WithStmt>(node);
        os << "(with (";
        Sep sp;
        for (const auto& rec : n->Records) {
            os << sp;
            printExpr(*rec, os);
        }
        os << ")\n" << ind(depth + 1);
        printStmt(n->Body.get(), os, depth + 1);
        os << ")";
        break;
    }
    case NodeKind::GotoStmt:
        os << "(goto " << llvm::cast<GotoStmt>(node)->Label << ")";
        break;
    case NodeKind::LabeledStmt: {
        const auto* n = llvm::cast<LabeledStmt>(node);
        os << "(label " << n->Label << "\n" << ind(depth + 1);
        printStmt(n->Stmt.get(), os, depth + 1);
        os << ")";
        break;
    }
    case NodeKind::CaseStmt: {
        const auto* n = llvm::cast<CaseStmt>(node);
        os << "(case ";
        printExpr(*n->Selector, os);
        for (const auto& arm : n->Arms) {
            os << "\n" << ind(depth + 1) << "(arm (";
            Sep sp;
            for (const auto& lbl : arm.Labels) {
                os << sp;
                printExpr(*lbl.Low, os);
                if (lbl.High) {
                    os << "..";
                    printExpr(*lbl.High, os);
                }
            }
            os << ") ";
            printStmt(arm.Body.get(), os, depth + 2);
            os << ")";
        }
        if (n->Else) {
            os << "\n" << ind(depth + 1) << "(otherwise ";
            printStmt(n->Else.get(), os, depth + 2);
            os << ")";
        }
        os << ")";
        break;
    }
    default:
        os << "(?stmt)";
        break;
    }
}

// ---------------------------------------------------------------------------
// Block and procedure printing
// ---------------------------------------------------------------------------

// Prints param groups inline: ((a b integer) (c real)) or () for empty.
static void printParams(const std::vector<ParamGroup>& params, std::ostream& os) {
    os << "(";
    Sep psp;
    for (const auto& pg : params) {
        os << psp << "(";
        if (pg.IsVar) os << "var ";
        if (pg.IsConst) os << "const ";
        Sep nsp;
        for (const auto& name : pg.Names) os << nsp << name;
        os << " ";
        // Turbo untyped parameter: Type is deliberately null -- see its own
        // comment (AstType.h) and the audit of every dereference of it this
        // feature required.
        if (pg.Type) printType(*pg.Type, os);
        else         os << "untyped";
        os << ")";
    }
    os << ")";
}

static void printBlock(const BlockNode& node, std::ostream& os, int depth) {
    if (!node.Labels.empty()) {
        os << ind(depth) << "(label";
        for (const auto& l : node.Labels) os << " " << l;
        os << ")\n";
    }

    for (const auto& cd : node.Consts) {
        os << ind(depth) << "(const " << cd.Name;
        // Turbo's typed-constant form; see ConstDef::Type's own comment.
        if (cd.Type) {
            os << " : ";
            printType(*cd.Type, os);
        }
        os << " ";
        printExpr(*cd.Value, os);
        os << ")\n";
    }

    for (const auto& td : node.Types) {
        os << ind(depth) << "(typedef " << td.Name << " ";
        printType(*td.Type, os);
        os << ")\n";
    }

    for (const auto& vg : node.Vars) {
        os << ind(depth) << "(var (";
        Sep sp;
        for (const auto& name : vg.Names) os << sp << name;
        os << ") ";
        printType(*vg.Type, os);
        // EP §6.4.1: the declaration's own 'value' initializer, distinct
        // from (and printed after) any 'value' clause on Type itself --
        // see VarGroup::InitExpr's own comment.
        if (vg.InitExpr) {
            os << " value ";
            printExpr(*vg.InitExpr, os);
        }
        // Turbo's 'absolute' directive; see VarGroup::AbsoluteExpr's own
        // comment.
        if (vg.AbsoluteExpr) {
            os << " absolute ";
            printExpr(*vg.AbsoluteExpr, os);
        }
        os << ")\n";
    }

    for (const auto& p : node.Procs) {
        printProc(*p, os, depth);
    }

    // Body — no trailing newline; the caller appends its closing ')'.
    os << ind(depth);
    printStmt(node.Body.get(), os, depth);
}

static void printProc(const ProcDecl& node, std::ostream& os, int depth) {
    os << ind(depth)
       << "(" << (node.IsConstructor ? "constructor" :
                   node.IsDestructor  ? "destructor"  :
                   node.IsFunction    ? "function"    : "procedure")
       << " ";
    // Turbo Tier 5: an object-type method's out-of-line body is qualified by
    // its owning type -- 'TAnimal.Speak' -- see ProcDecl::OwnerType's own
    // comment.  Empty for every other ProcDecl, ordinary and in-class alike.
    if (!node.OwnerType.empty()) os << node.OwnerType << ".";
    os << node.Name << " ";
    printParams(node.Params, os);
    // EP §6.7.2: the optional named-result-variable-specification is written
    // '= identifier' right after the parameter list and before the result
    // type (ParseDecl.cpp's parseProcDecl), so it is printed in that same
    // position -- see ProcDecl::ResultName's own comment.
    if (!node.ResultName.empty()) {
        os << " = " << node.ResultName;
    }
    if (node.IsFunction && node.ReturnType) {
        os << " ";
        printType(*node.ReturnType, os);
    }
    if (node.IsForward) {
        os << " forward)\n";
        return;
    }
    os << "\n";
    printBlock(*node.Body, os, depth + 1);
    os << ")\n";
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

static void printModule(const ModuleNode& mod, std::ostream& os, int depth) {
    os << ind(depth) << "(module " << mod.Name;
    if (mod.IsInterface) os << " interface";
    // EP §6.11.1: the 'implementation' module-identification -- mutually
    // exclusive with IsInterface (ParseModule.cpp's if/else-if), so there is
    // no ordering question between the two.
    if (mod.IsImplementation) os << " implementation";
    if (!mod.Exports.empty()) {
        os << " (export";
        for (const auto& e : mod.Exports) {
            os << " ";
            if (e.Protected) os << "protected ";
            os << e.Name;
            if (!e.RangeEnd.empty()) os << ".." << e.RangeEnd;
            if (!e.Alias.empty())    os << "=>" << e.Alias;
        }
        os << ")";
    }
    if (!mod.Imports.empty()) {
        for (const auto& ic : mod.Imports) {
            os << " (import " << ic.ModuleName;
            if (ic.Qualified) os << " qualified";
            if (!ic.Names.empty()) {
                os << (ic.Selective ? " only" : " renaming");
                for (const auto& n : ic.Names) {
                    os << " " << n;
                    for (const auto& [from, to] : ic.Renames)
                        if (eqCI(from, n)) os << "=>" << to;
                }
            }
            os << ")";
        }
    }
    if (mod.Body) {
        os << "\n";
        printBlock(*mod.Body, os, depth + 1);
    }
    if (mod.InitStmt) {
        os << "\n" << ind(depth + 1) << "(to-begin ";
        printStmt(mod.InitStmt.get(), os, depth + 2);
        os << ")";
    }
    if (mod.FinalStmt) {
        os << "\n" << ind(depth + 1) << "(to-end ";
        printStmt(mod.FinalStmt.get(), os, depth + 2);
        os << ")";
    }
    os << ")\n";
}

// Turbo Tier 4: a standalone unit file.  See UnitNode's own comment in
// AstDecl.h for the shape; printed the same '(kind ...)' way as printModule
// just above, reusing printBlock for both sections (printStmt already
// prints "()" for a null Body -- see its own null check -- so a HeadingsOnly
// interface block with no compound-statement body still prints cleanly).
static void printUnit(const UnitNode& unit, std::ostream& os) {
    os << "(unit " << unit.Name << "\n";
    os << ind(1) << "(interface";
    if (!unit.InterfaceUses.empty()) {
        os << " (uses";
        for (const auto& u : unit.InterfaceUses) os << " " << u.Name;
        os << ")";
    }
    os << "\n";
    printBlock(*unit.InterfaceBlock, os, 2);
    os << ")\n";
    os << ind(1) << "(implementation";
    if (!unit.ImplementationUses.empty()) {
        os << " (uses";
        for (const auto& u : unit.ImplementationUses) os << " " << u.Name;
        os << ")";
    }
    os << "\n";
    printBlock(*unit.ImplementationBlock, os, 2);
    os << ")\n";
    if (unit.InitBody) {
        os << ind(1) << "(init ";
        printStmt(unit.InitBody.get(), os, 1);
        os << ")\n";
    }
    os << ")\n";
}

void plang::printAst(const ProgramNode& program, std::ostream& os) {
    // Turbo Tier 4: a standalone unit file carries no real program at all --
    // print the unit form and return, rather than the placeholder Name/Block
    // parseUnitFile() filled in just so this ProgramNode still constructs.
    if (program.BareUnit) {
        printUnit(*program.BareUnit, os);
        return;
    }
    // Print any module definitions that precede the program.
    for (const auto* mod : program.Modules) {
        printModule(*mod, os, 0);
    }
    os << "(program " << program.Name;
    if (!program.Imports.empty()) {
        for (const auto& ic : program.Imports)
            os << " (import " << ic.ModuleName << ")";
    }
    // Turbo Tier 4, Cluster A item 1: a program's own top-level 'uses'
    // clause, printed the same '(uses A B ...)' shape printUnit already uses
    // for a unit's own InterfaceUses/ImplementationUses just above.
    if (!program.Uses.empty()) {
        os << " (uses";
        for (const auto& u : program.Uses) os << " " << u.Name;
        os << ")";
    }
    os << "\n";
    printBlock(*program.Block, os, 1);
    os << ")\n";
}
