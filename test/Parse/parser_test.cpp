#include "plang/AST/Ast.h"
#include "plang/AST/AstPrinter.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/LangOptions.h"
#include "plang/Parse/Parser.h"
#include "plang/Lex/Scanner.h"
#include "plang/Basic/Token.h"

#include "llvm/Support/Casting.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>

using namespace plang;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// RAII wrapper that writes content to a mkstemp file and deletes it on
// destruction. The Scanner reads the whole file in its constructor, so the
// TempFile only needs to outlive that call.
class TempFile {
public:
    explicit TempFile(const std::string &Content) {
        char Tmpl[] = "/tmp/plang_parser_test_XXXXXX";
        int Fd = mkstemp(Tmpl);
        Path = Tmpl;
        write(Fd, Content.data(), Content.size());
        close(Fd);
    }
    ~TempFile() { std::remove(Path.c_str()); }
    const std::string &path() const { return Path; }
private:
    std::string Path;
};

// Shared diagnostics sink; reset by each parse() call.
// Error tests check that this is non-empty / that parse() returns nullptr.
static DiagnosticsEngine parseDiags;

// Parses a Pascal source string and returns the root ProgramNode, or
// nullptr if the input contains scan/parse errors (which are in parseDiags).
static std::unique_ptr<ProgramNode> parse(const std::string &Src,
                                           LangOptions Opts = {}) {
    parseDiags.clear();
    TempFile F(Src);
    static SourceManager SM;
    Scanner Scanner(SM, F.path(), parseDiags, Opts);
    Parser  Parser(std::move(Scanner), parseDiags, Opts);
    return Parser.parse();
}

/// Parse Src and return the full AST as a string via printAst().
static std::string astOf(const std::string &Src, LangOptions Opts = {}) {
    auto Prog = parse(Src, Opts);
    if (!Prog) return "<parse error>";
    std::ostringstream Os;
    printAst(*Prog, Os);
    return Os.str();
}

// Returns the name string from a NamedTypeNode, or "<complex>" for other types.
static std::string namedType(const TypeNode *T) {
    if (auto *Nt = llvm::dyn_cast<NamedTypeNode>(T)) return Nt->Name;
    return "<complex>";
}

// Returns the target variable name from an AssignStmt whose target is a bare
// IdentExpr, or "<complex>" if it is a subscript, field access, or dereference.
static std::string assignTarget(const AssignStmt *A) {
    if (auto *Id = llvm::dyn_cast<IdentExpr>(A->Target.get())) return Id->Name;
    return "<complex>";
}

// ---------------------------------------------------------------------------
// Program structure
// ---------------------------------------------------------------------------

TEST(ParserProgram, MinimalProgram) {
    auto Ast = parse("program p; begin end.");
    ASSERT_NE(Ast, nullptr);
    EXPECT_EQ(Ast->Name, "p");
    ASSERT_NE(Ast->Block, nullptr);
    EXPECT_TRUE(Ast->Block->Vars.empty());
    EXPECT_TRUE(Ast->Block->Procs.empty());
    ASSERT_NE(Ast->Block->Body, nullptr);
    EXPECT_TRUE(Ast->Block->Body->Stmts.empty());
}

TEST(ParserProgram, PreservesName) {
    // Program name must retain original casing.
    auto Ast = parse("program MyProgram; begin end.");
    EXPECT_EQ(Ast->Name, "MyProgram");
}

TEST(ParserProgram, SingleFileParam) {
    // program graph1(output); — standard Pascal file parameter list with one entry.
    auto Ast = parse("program graph1(output); begin end.");
    EXPECT_EQ(Ast->Name, "graph1");
}

TEST(ParserProgram, MultipleFileParams) {
    // program rw(input, output); — two file parameters.
    auto Ast = parse("program rw(input, output); begin end.");
    EXPECT_EQ(Ast->Name, "rw");
}

// ---------------------------------------------------------------------------
// Constant definitions
// ---------------------------------------------------------------------------

TEST(ParserConstDecl, SingleIntConst) {
    auto Ast = parse("program p; const lim = 32; begin end.");
    ASSERT_EQ(Ast->Block->Consts.size(), 1u);
    const auto &Cd = Ast->Block->Consts[0];
    EXPECT_EQ(Cd.Name, "lim");
    auto *V = llvm::dyn_cast<IntLitExpr>(Cd.Value.get());
    ASSERT_NE(V, nullptr);
    EXPECT_EQ(V->Value, 32);
}

TEST(ParserConstDecl, SingleRealConst) {
    auto Ast = parse("program p; const d = 0.0625; begin end.");
    ASSERT_EQ(Ast->Block->Consts.size(), 1u);
    const auto &Cd = Ast->Block->Consts[0];
    EXPECT_EQ(Cd.Name, "d");
    auto *V = llvm::dyn_cast<RealLitExpr>(Cd.Value.get());
    ASSERT_NE(V, nullptr);
    EXPECT_DOUBLE_EQ(V->Value, 0.0625);
}

TEST(ParserConstDecl, MultipleConsts) {
    // Multiple definitions under one 'const' keyword.
    auto Ast = parse("program p; const a = 1; b = 2; c = 3; begin end.");
    ASSERT_EQ(Ast->Block->Consts.size(), 3u);
    EXPECT_EQ(Ast->Block->Consts[0].Name, "a");
    EXPECT_EQ(Ast->Block->Consts[1].Name, "b");
    EXPECT_EQ(Ast->Block->Consts[2].Name, "c");
}

TEST(ParserConstDecl, MixedTypes) {
    auto Ast = parse("program p; const n = 10; x = 3.14; s = 'hi'; b = true; begin end.");
    ASSERT_EQ(Ast->Block->Consts.size(), 4u);
    EXPECT_NE(llvm::dyn_cast<IntLitExpr>   (Ast->Block->Consts[0].Value.get()), nullptr);
    EXPECT_NE(llvm::dyn_cast<RealLitExpr>  (Ast->Block->Consts[1].Value.get()), nullptr);
    EXPECT_NE(llvm::dyn_cast<StringLitExpr>(Ast->Block->Consts[2].Value.get()), nullptr);
    EXPECT_NE(llvm::dyn_cast<BoolLitExpr>  (Ast->Block->Consts[3].Value.get()), nullptr);
}

TEST(ParserConstDecl, ConstBeforeVar) {
    // Const section must precede var section.
    auto Ast = parse("program p; const n = 1; var x : integer; begin end.");
    ASSERT_EQ(Ast->Block->Consts.size(), 1u);
    ASSERT_EQ(Ast->Block->Vars.size(),   1u);
}

TEST(ParserConstDecl, Graph1StyleConsts) {
    // Reproduces the const section from graph1.pas.
    auto Ast = parse(
        "program p;\n"
        "const d = 0.0625;\n"
        "      s = 32;\n"
        "      h = 34;\n"
        "      c = 6.28318;\n"
        "      lim = 32;\n"
        "begin end."
    );
    ASSERT_EQ(Ast->Block->Consts.size(), 5u);
    EXPECT_EQ(Ast->Block->Consts[0].Name, "d");
    EXPECT_EQ(Ast->Block->Consts[4].Name, "lim");
}

// ---------------------------------------------------------------------------
// Variable declarations
// ---------------------------------------------------------------------------

TEST(ParserVarDecl, SingleVariable) {
    auto Ast = parse("program p; var x : integer; begin end.");
    ASSERT_EQ(Ast->Block->Vars.size(), 1u);
    const auto &Vg = Ast->Block->Vars[0];
    ASSERT_EQ(Vg.Names.size(), 1u);
    EXPECT_EQ(Vg.Names[0], "x");
    EXPECT_EQ(namedType(Vg.Type.get()), "integer");
}

TEST(ParserVarDecl, MultipleNamesOneGroup) {
    // x, y, z : real — one VarGroup with three names.
    auto Ast = parse("program p; var x, y, z : real; begin end.");
    ASSERT_EQ(Ast->Block->Vars.size(), 1u);
    const auto &Vg = Ast->Block->Vars[0];
    ASSERT_EQ(Vg.Names.size(), 3u);
    EXPECT_EQ(Vg.Names[0], "x");
    EXPECT_EQ(Vg.Names[1], "y");
    EXPECT_EQ(Vg.Names[2], "z");
    EXPECT_EQ(namedType(Vg.Type.get()), "real");
}

TEST(ParserVarDecl, MultipleGroups) {
    // Two separate type groups in one var section.
    auto Ast = parse("program p; var x : integer; y : real; begin end.");
    ASSERT_EQ(Ast->Block->Vars.size(), 2u);
    EXPECT_EQ(Ast->Block->Vars[0].Names[0], "x");
    EXPECT_EQ(namedType(Ast->Block->Vars[0].Type.get()), "integer");
    EXPECT_EQ(Ast->Block->Vars[1].Names[0], "y");
    EXPECT_EQ(namedType(Ast->Block->Vars[1].Type.get()), "real");
}

// One test per supported type keyword.
TEST(ParserVarDecl, TypeInteger) { EXPECT_EQ(namedType(parse("program p; var x : integer; begin end.")->Block->Vars[0].Type.get()), "integer"); }
TEST(ParserVarDecl, TypeReal)    { EXPECT_EQ(namedType(parse("program p; var x : real;    begin end.")->Block->Vars[0].Type.get()), "real");    }
TEST(ParserVarDecl, TypeBoolean) { EXPECT_EQ(namedType(parse("program p; var x : boolean; begin end.")->Block->Vars[0].Type.get()), "boolean"); }
TEST(ParserVarDecl, TypeString)  { EXPECT_EQ(namedType(parse("program p; var x : string;  begin end.")->Block->Vars[0].Type.get()), "string");  }

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

TEST(ParserStatements, EmptyCompound) {
    auto Ast = parse("program p; begin end.");
    EXPECT_TRUE(Ast->Block->Body->Stmts.empty());
}

TEST(ParserStatements, Assignment_IntLit) {
    auto Ast = parse("program p; var x : integer; begin x := 42 end.");
    auto &Stmts = Ast->Block->Body->Stmts;
    ASSERT_EQ(Stmts.size(), 1u);
    auto *A = llvm::dyn_cast<AssignStmt>(Stmts[0].get());
    ASSERT_NE(A, nullptr);
    EXPECT_EQ(assignTarget(A), "x");
    auto *V = llvm::dyn_cast<IntLitExpr>(A->Value.get());
    ASSERT_NE(V, nullptr);
    EXPECT_EQ(V->Value, 42);
}

TEST(ParserStatements, Assignment_RealLit) {
    auto Ast = parse("program p; var x : real; begin x := 3.14 end.");
    auto *A = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(A, nullptr);
    auto *V = llvm::dyn_cast<RealLitExpr>(A->Value.get());
    ASSERT_NE(V, nullptr);
    EXPECT_DOUBLE_EQ(V->Value, 3.14);
}

TEST(ParserStatements, Assignment_StringLit) {
    auto Ast = parse("program p; var s : string; begin s := 'hello' end.");
    auto *A = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(A, nullptr);
    auto *V = llvm::dyn_cast<StringLitExpr>(A->Value.get());
    ASSERT_NE(V, nullptr);
    EXPECT_EQ(V->Value, "hello");
}

TEST(ParserStatements, Assignment_BoolTrue) {
    auto Ast = parse("program p; var b : boolean; begin b := true end.");
    auto *A = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(A, nullptr);
    auto *V = llvm::dyn_cast<BoolLitExpr>(A->Value.get());
    ASSERT_NE(V, nullptr);
    EXPECT_TRUE(V->Value);
}

TEST(ParserStatements, Assignment_BoolFalse) {
    auto Ast = parse("program p; var b : boolean; begin b := false end.");
    auto *A = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(A, nullptr);
    auto *V = llvm::dyn_cast<BoolLitExpr>(A->Value.get());
    ASSERT_NE(V, nullptr);
    EXPECT_FALSE(V->Value);
}

TEST(ParserStatements, MultipleStatements) {
    auto Ast = parse("program p; var x, y : integer; begin x := 1; y := 2 end.");
    EXPECT_EQ(Ast->Block->Body->Stmts.size(), 2u);
}

TEST(ParserStatements, NestedCompound) {
    auto Ast = parse("program p; begin begin end end.");
    auto &Outer = Ast->Block->Body->Stmts;
    ASSERT_EQ(Outer.size(), 1u);
    auto *Inner = llvm::dyn_cast<CompoundStmt>(Outer[0].get());
    ASSERT_NE(Inner, nullptr);
    EXPECT_TRUE(Inner->Stmts.empty());
}

TEST(ParserStatements, TrailingSemicolonDropped) {
    // A semicolon before 'end' produces an empty statement that is discarded.
    auto Ast = parse("program p; var x : integer; begin x := 1; end.");
    EXPECT_EQ(Ast->Block->Body->Stmts.size(), 1u);
}

TEST(ParserStatements, CallStmt_NoArgs) {
    auto Ast = parse("program p; begin writeln end.");
    auto *C = llvm::dyn_cast<CallStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(C, nullptr);
    EXPECT_EQ(C->Name, "writeln");
    EXPECT_TRUE(C->Args.empty());
}

TEST(ParserStatements, CallStmt_WithArgs) {
    auto Ast = parse("program p; var x : integer; begin writeln(x, 42) end.");
    auto *C = llvm::dyn_cast<CallStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(C, nullptr);
    EXPECT_EQ(C->Name, "writeln");
    ASSERT_EQ(C->Args.size(), 2u);
    EXPECT_NE(llvm::dyn_cast<IdentExpr>(C->Args[0].get()), nullptr);
    EXPECT_NE(llvm::dyn_cast<IntLitExpr>(C->Args[1].get()), nullptr);
}

TEST(ParserStatements, IfThen) {
    auto Ast = parse("program p; var x : integer; begin if x > 0 then x := 1 end.");
    auto *S = llvm::dyn_cast<IfStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(S, nullptr);
    EXPECT_NE(S->Cond.get(), nullptr);
    EXPECT_NE(S->Then.get(), nullptr);
    EXPECT_EQ(S->Else.get(), nullptr);
}

TEST(ParserStatements, IfThenElse) {
    auto Ast = parse("program p; var x : integer; begin if x > 0 then x := 1 else x := 0 end.");
    auto *S = llvm::dyn_cast<IfStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(S, nullptr);
    ASSERT_NE(S->Else.get(), nullptr);
    auto *E = llvm::dyn_cast<AssignStmt>(S->Else.get());
    ASSERT_NE(E, nullptr);
    EXPECT_EQ(assignTarget(E), "x");
    auto *V = llvm::dyn_cast<IntLitExpr>(E->Value.get());
    ASSERT_NE(V, nullptr);
    EXPECT_EQ(V->Value, 0);
}

TEST(ParserStatements, DanglingElseBindsToInnerIf) {
    // if a then if b then x := 1 else x := 2
    // The 'else' must bind to the inner 'if', not the outer.
    auto Ast = parse(
        "program p; var a, b : boolean; var x : integer;\n"
        "begin if a then if b then x := 1 else x := 2 end."
    );
    auto *Outer = llvm::dyn_cast<IfStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(Outer, nullptr);
    EXPECT_EQ(Outer->Else.get(), nullptr);      // outer if has no else
    auto *Inner = llvm::dyn_cast<IfStmt>(Outer->Then.get());
    ASSERT_NE(Inner, nullptr);
    EXPECT_NE(Inner->Else.get(), nullptr);      // inner if has the else
}

TEST(ParserStatements, WhileDo) {
    auto Ast = parse("program p; var x : integer; begin while x > 0 do x := x - 1 end.");
    auto *S = llvm::dyn_cast<WhileStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(S, nullptr);
    EXPECT_NE(S->Cond.get(), nullptr);
    auto *Body = llvm::dyn_cast<AssignStmt>(S->Body.get());
    ASSERT_NE(Body, nullptr);
    EXPECT_EQ(assignTarget(Body), "x");
}

TEST(ParserStatements, ForTo) {
    auto Ast = parse("program p; var i : integer; begin for i := 1 to 10 do i := i + 1 end.");
    auto *F = llvm::dyn_cast<ForStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(F, nullptr);
    EXPECT_EQ(F->Var, "i");
    EXPECT_FALSE(F->Downto);
    auto *From = llvm::dyn_cast<IntLitExpr>(F->From.get());
    ASSERT_NE(From, nullptr);
    EXPECT_EQ(From->Value, 1);
    auto *Limit = llvm::dyn_cast<IntLitExpr>(F->Limit.get());
    ASSERT_NE(Limit, nullptr);
    EXPECT_EQ(Limit->Value, 10);
    EXPECT_NE(llvm::dyn_cast<AssignStmt>(F->Body.get()), nullptr);
}

TEST(ParserStatements, ForDownto) {
    auto Ast = parse("program p; var i : integer; begin for i := 10 downto 1 do i := i - 1 end.");
    auto *F = llvm::dyn_cast<ForStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(F, nullptr);
    EXPECT_TRUE(F->Downto);
}

TEST(ParserStatements, ForWithCompoundBody) {
    auto Ast = parse(
        "program p; var i, s : integer;\n"
        "begin for i := 1 to 5 do begin s := s + i end end."
    );
    auto *F = llvm::dyn_cast<ForStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(F, nullptr);
    EXPECT_NE(llvm::dyn_cast<CompoundStmt>(F->Body.get()), nullptr);
}

TEST(ParserStatements, RepeatUntil) {
    auto Ast = parse("program p; var n : integer; begin repeat n := n - 1 until n = 0 end.");
    auto *R = llvm::dyn_cast<RepeatStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(R, nullptr);
    ASSERT_EQ(R->Stmts.size(), 1u);
    EXPECT_NE(llvm::dyn_cast<AssignStmt>(R->Stmts[0].get()), nullptr);
    auto *Cond = llvm::dyn_cast<BinaryExpr>(R->Cond.get());
    ASSERT_NE(Cond, nullptr);
    EXPECT_EQ(Cond->Op, TokenKind::Equal);
}

TEST(ParserStatements, RepeatMultipleStatements) {
    auto Ast = parse(
        "program p; var n, x : integer;\n"
        "begin repeat x := x + 1; n := n - 1 until n = 0 end."
    );
    auto *R = llvm::dyn_cast<RepeatStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(R, nullptr);
    EXPECT_EQ(R->Stmts.size(), 2u);
}

TEST(ParserStatements, RepeatTrailingSemicolon) {
    // A semicolon before 'until' produces an empty statement that is dropped.
    auto Ast = parse("program p; var n : integer; begin repeat n := n - 1; until n = 0 end.");
    auto *R = llvm::dyn_cast<RepeatStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(R, nullptr);
    EXPECT_EQ(R->Stmts.size(), 1u);
}

TEST(ParserStatements, WhileWithCompoundBody) {
    auto Ast = parse(
        "program p; var x, y : integer;\n"
        "begin while x > 0 do begin y := y + x; x := x - 1 end end."
    );
    auto *W = llvm::dyn_cast<WhileStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(W, nullptr);
    auto *Body = llvm::dyn_cast<CompoundStmt>(W->Body.get());
    ASSERT_NE(Body, nullptr);
    EXPECT_EQ(Body->Stmts.size(), 2u);
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

TEST(ParserExpressions, IdentifierRef) {
    auto Ast = parse("program p; var x, y : integer; begin y := x end.");
    auto *A = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(A, nullptr);
    auto *V = llvm::dyn_cast<IdentExpr>(A->Value.get());
    ASSERT_NE(V, nullptr);
    EXPECT_EQ(V->Name, "x");
}

TEST(ParserExpressions, BinaryAdd) {
    auto Ast = parse("program p; var x : integer; begin x := 1 + 2 end.");
    auto *A = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(A, nullptr);
    auto *B = llvm::dyn_cast<BinaryExpr>(A->Value.get());
    ASSERT_NE(B, nullptr);
    EXPECT_EQ(B->Op, TokenKind::Plus);
    EXPECT_NE(llvm::dyn_cast<IntLitExpr>(B->Left.get()),  nullptr);
    EXPECT_NE(llvm::dyn_cast<IntLitExpr>(B->Right.get()), nullptr);
}

TEST(ParserExpressions, MulBeforeAdd) {
    // 1 + 2 * 3 must parse as 1 + (2 * 3).
    auto Ast = parse("program p; var x : integer; begin x := 1 + 2 * 3 end.");
    auto *A = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(A, nullptr);
    auto *Add = llvm::dyn_cast<BinaryExpr>(A->Value.get());
    ASSERT_NE(Add, nullptr);
    EXPECT_EQ(Add->Op, TokenKind::Plus);
    auto *Mul = llvm::dyn_cast<BinaryExpr>(Add->Right.get());
    ASSERT_NE(Mul, nullptr);
    EXPECT_EQ(Mul->Op, TokenKind::Times);
}

TEST(ParserExpressions, ParenOverridesPrecedence) {
    // (1 + 2) * 3 must have Times at the root.
    auto Ast = parse("program p; var x : integer; begin x := (1 + 2) * 3 end.");
    auto *A = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(A, nullptr);
    auto *Mul = llvm::dyn_cast<BinaryExpr>(A->Value.get());
    ASSERT_NE(Mul, nullptr);
    EXPECT_EQ(Mul->Op, TokenKind::Times);
    auto *Add = llvm::dyn_cast<BinaryExpr>(Mul->Left.get());
    ASSERT_NE(Add, nullptr);
    EXPECT_EQ(Add->Op, TokenKind::Plus);
}

TEST(ParserExpressions, RelOpLowestPrecedence) {
    // 1 + 2 = 3 must parse as (1 + 2) = 3, with Equal at the root.
    auto Ast = parse("program p; var x : boolean; begin x := 1 + 2 = 3 end.");
    auto *A = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(A, nullptr);
    auto *Eq = llvm::dyn_cast<BinaryExpr>(A->Value.get());
    ASSERT_NE(Eq, nullptr);
    EXPECT_EQ(Eq->Op, TokenKind::Equal);
    auto *Add = llvm::dyn_cast<BinaryExpr>(Eq->Left.get());
    ASSERT_NE(Add, nullptr);
    EXPECT_EQ(Add->Op, TokenKind::Plus);
}

TEST(ParserExpressions, LeftAssociativity) {
    // 1 - 2 - 3 must parse as (1 - 2) - 3 (left-associative).
    auto Ast = parse("program p; var x : integer; begin x := 1 - 2 - 3 end.");
    auto *A = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(A, nullptr);
    auto *Outer = llvm::dyn_cast<BinaryExpr>(A->Value.get());
    ASSERT_NE(Outer, nullptr);
    EXPECT_EQ(Outer->Op, TokenKind::Minus);
    auto *Inner = llvm::dyn_cast<BinaryExpr>(Outer->Left.get());
    ASSERT_NE(Inner, nullptr);
    EXPECT_EQ(Inner->Op, TokenKind::Minus);
    auto *Rhs = llvm::dyn_cast<IntLitExpr>(Outer->Right.get());
    ASSERT_NE(Rhs, nullptr);
    EXPECT_EQ(Rhs->Value, 3);
}

TEST(ParserExpressions, UnaryMinus) {
    auto Ast = parse("program p; var x : integer; begin x := -1 end.");
    auto *A = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(A, nullptr);
    auto *U = llvm::dyn_cast<UnaryExpr>(A->Value.get());
    ASSERT_NE(U, nullptr);
    EXPECT_EQ(U->Op, TokenKind::Minus);
    EXPECT_NE(llvm::dyn_cast<IntLitExpr>(U->Operand.get()), nullptr);
}

TEST(ParserExpressions, UnaryNot) {
    auto Ast = parse("program p; var b : boolean; begin b := not true end.");
    auto *A = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(A, nullptr);
    auto *U = llvm::dyn_cast<UnaryExpr>(A->Value.get());
    ASSERT_NE(U, nullptr);
    EXPECT_EQ(U->Op, TokenKind::Not);
    EXPECT_NE(llvm::dyn_cast<BoolLitExpr>(U->Operand.get()), nullptr);
}

TEST(ParserExpressions, DoubleNot) {
    // not not true — right-recursive, should produce nested UnaryExprs.
    auto Ast = parse("program p; var b : boolean; begin b := not not true end.");
    auto *A = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(A, nullptr);
    auto *Outer = llvm::dyn_cast<UnaryExpr>(A->Value.get());
    ASSERT_NE(Outer, nullptr);
    EXPECT_EQ(Outer->Op, TokenKind::Not);
    auto *Inner = llvm::dyn_cast<UnaryExpr>(Outer->Operand.get());
    ASSERT_NE(Inner, nullptr);
    EXPECT_EQ(Inner->Op, TokenKind::Not);
}

TEST(ParserExpressions, FunctionCallExpr) {
    auto Ast = parse("program p; var x : integer; begin x := max(1, 2) end.");
    auto *A = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(A, nullptr);
    auto *C = llvm::dyn_cast<CallExpr>(A->Value.get());
    ASSERT_NE(C, nullptr);
    EXPECT_EQ(C->Name, "max");
    ASSERT_EQ(C->Args.size(), 2u);
    auto *Arg0 = llvm::dyn_cast<IntLitExpr>(C->Args[0].get());
    ASSERT_NE(Arg0, nullptr);
    EXPECT_EQ(Arg0->Value, 1);
    auto *Arg1 = llvm::dyn_cast<IntLitExpr>(C->Args[1].get());
    ASSERT_NE(Arg1, nullptr);
    EXPECT_EQ(Arg1->Value, 2);
}

TEST(ParserExpressions, AllRelops) {
    // Each relational operator must produce a BinaryExpr with the correct kind.
    struct Case { const char *Sym; TokenKind Kind; };
    for (auto [Sym, Kind] : {
        Case{"=",  TokenKind::Equal},
        Case{"<>", TokenKind::NotEqual},
        Case{"<",  TokenKind::LessThan},
        Case{"<=", TokenKind::LessThanOrEqual},
        Case{">",  TokenKind::GreaterThan},
        Case{">=", TokenKind::GreaterThanOrEqual},
    }) {
        auto Src = std::string("program p; var x : boolean; begin x := 1 ") + Sym + " 2 end.";
        auto Ast  = parse(Src);
        auto *A   = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
        ASSERT_NE(A, nullptr) << "sym=" << Sym;
        auto *B   = llvm::dyn_cast<BinaryExpr>(A->Value.get());
        ASSERT_NE(B, nullptr) << "sym=" << Sym;
        EXPECT_EQ(B->Op, Kind) << "sym=" << Sym;
    }
}

TEST(ParserExpressions, AllAddops) {
    struct Case { const char *Sym; TokenKind Kind; };
    for (auto [Sym, Kind] : {
        Case{"+",  TokenKind::Plus},
        Case{"-",  TokenKind::Minus},
        Case{"or", TokenKind::Or},
    }) {
        auto Src = std::string("program p; var x : integer; begin x := 1 ") + Sym + " 2 end.";
        auto Ast  = parse(Src);
        auto *A   = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
        ASSERT_NE(A, nullptr) << "sym=" << Sym;
        auto *B   = llvm::dyn_cast<BinaryExpr>(A->Value.get());
        ASSERT_NE(B, nullptr) << "sym=" << Sym;
        EXPECT_EQ(B->Op, Kind) << "sym=" << Sym;
    }
}

TEST(ParserExpressions, AllMulops) {
    struct Case { const char *Sym; TokenKind Kind; };
    for (auto [Sym, Kind] : {
        Case{"*",   TokenKind::Times},
        Case{"/",   TokenKind::Divide},
        Case{"div", TokenKind::Div},
        Case{"mod", TokenKind::Mod},
        Case{"and", TokenKind::And},
    }) {
        auto Src = std::string("program p; var x : integer; begin x := 1 ") + Sym + " 2 end.";
        auto Ast  = parse(Src);
        auto *A   = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
        ASSERT_NE(A, nullptr) << "sym=" << Sym;
        auto *B   = llvm::dyn_cast<BinaryExpr>(A->Value.get());
        ASSERT_NE(B, nullptr) << "sym=" << Sym;
        EXPECT_EQ(B->Op, Kind) << "sym=" << Sym;
    }
}

// ---------------------------------------------------------------------------
// Procedures and functions
// ---------------------------------------------------------------------------

TEST(ParserProcedures, SimpleProcedure) {
    auto Ast = parse("program p; procedure foo; begin end; begin end.");
    ASSERT_EQ(Ast->Block->Procs.size(), 1u);
    const auto &Proc = *Ast->Block->Procs[0];
    EXPECT_FALSE(Proc.IsFunction);
    EXPECT_EQ(Proc.Name, "foo");
    EXPECT_TRUE(Proc.Params.empty());
    EXPECT_EQ(Proc.ReturnType.get(), nullptr);
}

TEST(ParserProcedures, ProcedureWithParams) {
    auto Ast = parse("program p; procedure swap(a, b : integer); begin end; begin end.");
    const auto &Proc = *Ast->Block->Procs[0];
    ASSERT_EQ(Proc.Params.size(), 1u);
    const auto &Pg = Proc.Params[0];
    ASSERT_EQ(Pg.Names.size(), 2u);
    EXPECT_EQ(Pg.Names[0], "a");
    EXPECT_EQ(Pg.Names[1], "b");
    EXPECT_EQ(namedType(Pg.Type.get()), "integer");
}

TEST(ParserProcedures, ProcedureMultipleParamGroups) {
    auto Ast = parse("program p; procedure f(x : integer; y : real); begin end; begin end.");
    const auto &Proc = *Ast->Block->Procs[0];
    ASSERT_EQ(Proc.Params.size(), 2u);
    EXPECT_EQ(namedType(Proc.Params[0].Type.get()), "integer");
    EXPECT_EQ(namedType(Proc.Params[1].Type.get()), "real");
}

TEST(ParserProcedures, SimpleFunction) {
    auto Ast = parse(
        "program p;\n"
        "function square(x : integer) : integer;\n"
        "begin square := x * x end;\n"
        "begin end."
    );
    ASSERT_EQ(Ast->Block->Procs.size(), 1u);
    const auto &Fn = *Ast->Block->Procs[0];
    EXPECT_TRUE(Fn.IsFunction);
    EXPECT_EQ(Fn.Name, "square");
    EXPECT_EQ(namedType(Fn.ReturnType.get()), "integer");
    ASSERT_EQ(Fn.Params.size(), 1u);
    EXPECT_EQ(Fn.Params[0].Names[0], "x");
    EXPECT_EQ(namedType(Fn.Params[0].Type.get()), "integer");
}

TEST(ParserProcedures, FunctionBodyAssignedByName) {
    // Pascal functions return by assigning to the function name.
    auto Ast = parse(
        "program p;\n"
        "function double(x : integer) : integer;\n"
        "begin double := x * 2 end;\n"
        "begin end."
    );
    const auto &Fn = *Ast->Block->Procs[0];
    auto &Stmts = Fn.Body->Body->Stmts;
    ASSERT_EQ(Stmts.size(), 1u);
    auto *A = llvm::dyn_cast<AssignStmt>(Stmts[0].get());
    ASSERT_NE(A, nullptr);
    EXPECT_EQ(assignTarget(A), "double");
}

TEST(ParserProcedures, NestedProcedure) {
    auto Ast = parse(
        "program p;\n"
        "procedure outer;\n"
        "  procedure inner;\n"
        "  begin end;\n"
        "begin end;\n"
        "begin end."
    );
    ASSERT_EQ(Ast->Block->Procs.size(), 1u);
    const auto &Outer = *Ast->Block->Procs[0];
    ASSERT_EQ(Outer.Body->Procs.size(), 1u);
    EXPECT_EQ(Outer.Body->Procs[0]->Name, "inner");
}

TEST(ParserProcedures, ProcedureWithLocalVars) {
    auto Ast = parse(
        "program p;\n"
        "procedure f;\n"
        "  var tmp : integer;\n"
        "begin end;\n"
        "begin end."
    );
    const auto &Proc = *Ast->Block->Procs[0];
    ASSERT_EQ(Proc.Body->Vars.size(), 1u);
    EXPECT_EQ(Proc.Body->Vars[0].Names[0], "tmp");
    EXPECT_EQ(namedType(Proc.Body->Vars[0].Type.get()), "integer");
}

// ---------------------------------------------------------------------------
// Type declarations
// ---------------------------------------------------------------------------

TEST(ParserTypeDecl, NamedTypeAlias) {
    auto Ast = parse("program p; type MyInt = integer; begin end.");
    ASSERT_EQ(Ast->Block->Types.size(), 1u);
    const auto &Td = Ast->Block->Types[0];
    EXPECT_EQ(Td.Name, "MyInt");
    EXPECT_EQ(namedType(Td.Type.get()), "integer");
}

TEST(ParserTypeDecl, MultipleTypeDefs) {
    auto Ast = parse("program p; type A = integer; B = real; begin end.");
    ASSERT_EQ(Ast->Block->Types.size(), 2u);
    EXPECT_EQ(Ast->Block->Types[0].Name, "A");
    EXPECT_EQ(Ast->Block->Types[1].Name, "B");
}

TEST(ParserTypeDecl, ArrayType) {
    auto Ast = parse("program p; type Vec = array[1..10] of real; begin end.");
    ASSERT_EQ(Ast->Block->Types.size(), 1u);
    auto *At = llvm::dyn_cast<ArrayTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(At, nullptr);
    EXPECT_EQ(namedType(At->Element.get()), "real");
    auto *Lo = llvm::dyn_cast<IntLitExpr>(At->Low.get());
    ASSERT_NE(Lo, nullptr);
    EXPECT_EQ(Lo->Value, 1);
    auto *Hi = llvm::dyn_cast<IntLitExpr>(At->High.get());
    ASSERT_NE(Hi, nullptr);
    EXPECT_EQ(Hi->Value, 10);
}

TEST(ParserTypeDecl, ArrayOfUserType) {
    // Array element type can be a user-defined type name.
    auto Ast = parse("program p; type Grid = array[0..99] of MyType; begin end.");
    auto *At = llvm::dyn_cast<ArrayTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(At, nullptr);
    EXPECT_EQ(namedType(At->Element.get()), "MyType");
}

TEST(ParserTypeDecl, RecordType) {
    auto Ast = parse(
        "program p;\n"
        "type Point = record x : real; y : real end;\n"
        "begin end."
    );
    ASSERT_EQ(Ast->Block->Types.size(), 1u);
    auto *Rt = llvm::dyn_cast<RecordTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(Rt, nullptr);
    ASSERT_EQ(Rt->Fields.size(), 2u);
    EXPECT_EQ(Rt->Fields[0].Names[0], "x");
    EXPECT_EQ(namedType(Rt->Fields[0].Type.get()), "real");
    EXPECT_EQ(Rt->Fields[1].Names[0], "y");
    EXPECT_EQ(namedType(Rt->Fields[1].Type.get()), "real");
}

TEST(ParserTypeDecl, RecordMultipleNamesPerField) {
    auto Ast = parse(
        "program p;\n"
        "type Rect = record left, right : integer; top, bottom : integer end;\n"
        "begin end."
    );
    auto *Rt = llvm::dyn_cast<RecordTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(Rt, nullptr);
    ASSERT_EQ(Rt->Fields.size(), 2u);
    ASSERT_EQ(Rt->Fields[0].Names.size(), 2u);
    EXPECT_EQ(Rt->Fields[0].Names[0], "left");
    EXPECT_EQ(Rt->Fields[0].Names[1], "right");
}

TEST(ParserTypeDecl, RecordTrailingSemicolon) {
    // A semicolon after the last field before 'end' is valid.
    auto Ast = parse(
        "program p;\n"
        "type P = record x : real; y : real; end;\n"
        "begin end."
    );
    auto *Rt = llvm::dyn_cast<RecordTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(Rt, nullptr);
    EXPECT_EQ(Rt->Fields.size(), 2u);
}

TEST(ParserTypeDecl, PointerType) {
    auto Ast = parse("program p; type IntPtr = ^integer; begin end.");
    auto *Pt = llvm::dyn_cast<PointerTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(Pt, nullptr);
    EXPECT_EQ(namedType(Pt->Base.get()), "integer");
}

TEST(ParserTypeDecl, PointerToRecord) {
    auto Ast = parse(
        "program p;\n"
        "type Node = record value : integer; next : ^Node end;\n"
        "begin end."
    );
    auto *Rt = llvm::dyn_cast<RecordTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(Rt, nullptr);
    ASSERT_EQ(Rt->Fields.size(), 2u);
    auto *Pt = llvm::dyn_cast<PointerTypeNode>(Rt->Fields[1].Type.get());
    ASSERT_NE(Pt, nullptr);
    EXPECT_EQ(namedType(Pt->Base.get()), "Node");
}

TEST(ParserTypeDecl, VarWithArrayType) {
    // Variables can be declared with an inline array type.
    auto Ast = parse("program p; var a : array[1..5] of integer; begin end.");
    ASSERT_EQ(Ast->Block->Vars.size(), 1u);
    auto *At = llvm::dyn_cast<ArrayTypeNode>(Ast->Block->Vars[0].Type.get());
    ASSERT_NE(At, nullptr);
    EXPECT_EQ(namedType(At->Element.get()), "integer");
}

// ---------------------------------------------------------------------------
// Composite expressions (subscript, field access, dereference)
// ---------------------------------------------------------------------------

TEST(ParserExpressions, ArraySubscript) {
    auto Ast = parse("program p; var a : array[1..5] of integer; var x : integer; begin x := a[1] end.");
    auto *Assign = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(Assign, nullptr);
    auto *Idx = llvm::dyn_cast<IndexExpr>(Assign->Value.get());
    ASSERT_NE(Idx, nullptr);
    EXPECT_NE(llvm::dyn_cast<IdentExpr>(Idx->Array.get()), nullptr);
    auto *I = llvm::dyn_cast<IntLitExpr>(Idx->Index.get());
    ASSERT_NE(I, nullptr);
    EXPECT_EQ(I->Value, 1);
}

TEST(ParserExpressions, FieldAccess) {
    auto Ast = parse("program p; var x : integer; begin x := p.field end.");
    auto *Assign = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(Assign, nullptr);
    auto *Fe = llvm::dyn_cast<FieldExpr>(Assign->Value.get());
    ASSERT_NE(Fe, nullptr);
    EXPECT_EQ(Fe->Field, "field");
    EXPECT_NE(llvm::dyn_cast<IdentExpr>(Fe->Record.get()), nullptr);
}

TEST(ParserExpressions, PointerDeref) {
    auto Ast = parse("program p; var x : integer; begin x := ptr^ end.");
    auto *Assign = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(Assign, nullptr);
    auto *De = llvm::dyn_cast<DerefExpr>(Assign->Value.get());
    ASSERT_NE(De, nullptr);
    EXPECT_NE(llvm::dyn_cast<IdentExpr>(De->Pointer.get()), nullptr);
}

TEST(ParserExpressions, NilLiteral) {
    auto Ast = parse("program p; var p : integer; begin p := nil end.");
    auto *Assign = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(Assign, nullptr);
    EXPECT_NE(llvm::dyn_cast<NilExpr>(Assign->Value.get()), nullptr);
}

TEST(ParserExpressions, ChainedPostfix) {
    // a[i].field^  — subscript, then field access, then deref
    auto Ast = parse("program p; var x : integer; begin x := a[i].f^ end.");
    auto *Assign = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(Assign, nullptr);
    auto *De = llvm::dyn_cast<DerefExpr>(Assign->Value.get());
    ASSERT_NE(De, nullptr);
    auto *Fe = llvm::dyn_cast<FieldExpr>(De->Pointer.get());
    ASSERT_NE(Fe, nullptr);
    EXPECT_EQ(Fe->Field, "f");
    EXPECT_NE(llvm::dyn_cast<IndexExpr>(Fe->Record.get()), nullptr);
}

TEST(ParserStatements, AssignToArrayElement) {
    auto Ast = parse("program p; var a : array[1..5] of integer; begin a[1] := 42 end.");
    auto *Assign = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(Assign, nullptr);
    auto *Idx = llvm::dyn_cast<IndexExpr>(Assign->Target.get());
    ASSERT_NE(Idx, nullptr);
    EXPECT_NE(llvm::dyn_cast<IdentExpr>(Idx->Array.get()), nullptr);
}

TEST(ParserStatements, AssignToField) {
    auto Ast = parse("program p; begin r.x := 1 end.");
    auto *Assign = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(Assign, nullptr);
    auto *Fe = llvm::dyn_cast<FieldExpr>(Assign->Target.get());
    ASSERT_NE(Fe, nullptr);
    EXPECT_EQ(Fe->Field, "x");
}

TEST(ParserStatements, AssignToDeref) {
    auto Ast = parse("program p; begin ptr^ := 99 end.");
    auto *Assign = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(Assign, nullptr);
    EXPECT_NE(llvm::dyn_cast<DerefExpr>(Assign->Target.get()), nullptr);
}

TEST(ParserStatements, WithStatement) {
    auto Ast = parse("program p; begin with rec do rec.x := 1 end.");
    auto *W = llvm::dyn_cast<WithStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(W, nullptr);
    ASSERT_EQ(W->Records.size(), 1u);
    EXPECT_NE(llvm::dyn_cast<IdentExpr>(W->Records[0].get()), nullptr);
    EXPECT_NE(W->Body.get(), nullptr);
}

TEST(ParserStatements, WithMultipleRecords) {
    auto Ast = parse("program p; begin with r1, r2 do r1.x := 0 end.");
    auto *W = llvm::dyn_cast<WithStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(W, nullptr);
    EXPECT_EQ(W->Records.size(), 2u);
}

// ---------------------------------------------------------------------------
// Error cases
// ---------------------------------------------------------------------------

TEST(ParserErrors, MissingProgramKeyword) {
    EXPECT_EQ(parse("p; begin end."), nullptr);
}

// ---------------------------------------------------------------------------
// Enumerated, subrange, set, file, packed types
// ---------------------------------------------------------------------------

TEST(ParserNewTypes, EnumType) {
    auto Ast = parse("program p; type Color = (red, green, blue); begin end.");
    auto *Et = llvm::dyn_cast<EnumTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(Et, nullptr);
    ASSERT_EQ(Et->Values.size(), 3u);
    EXPECT_EQ(Et->Values[0], "red");
    EXPECT_EQ(Et->Values[2], "blue");
}

TEST(ParserNewTypes, SubrangeIntType) {
    auto Ast = parse("program p; type Digit = 0..9; begin end.");
    auto *Sr = llvm::dyn_cast<SubrangeTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(Sr, nullptr);
    auto *Lo = llvm::dyn_cast<IntLitExpr>(Sr->Low.get());
    ASSERT_NE(Lo, nullptr);
    EXPECT_EQ(Lo->Value, 0);
    auto *Hi = llvm::dyn_cast<IntLitExpr>(Sr->High.get());
    ASSERT_NE(Hi, nullptr);
    EXPECT_EQ(Hi->Value, 9);
}

TEST(ParserNewTypes, SubrangeIdentType) {
    // Both bounds are named constants.
    auto Ast = parse("program p; type Small = minVal..maxVal; begin end.");
    auto *Sr = llvm::dyn_cast<SubrangeTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(Sr, nullptr);
    auto *Lo = llvm::dyn_cast<IdentExpr>(Sr->Low.get());
    ASSERT_NE(Lo, nullptr);
    EXPECT_EQ(Lo->Name, "minVal");
}

TEST(ParserNewTypes, SetType) {
    auto Ast = parse("program p; type CharSet = set of char; begin end.");
    auto *St = llvm::dyn_cast<SetTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(St, nullptr);
    EXPECT_FALSE(St->Packed);
    EXPECT_EQ(namedType(St->Base.get()), "char");
}

TEST(ParserNewTypes, PackedSetType) {
    auto Ast = parse("program p; type BS = packed set of boolean; begin end.");
    auto *St = llvm::dyn_cast<SetTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(St, nullptr);
    EXPECT_TRUE(St->Packed);
}

TEST(ParserNewTypes, FileOfType) {
    auto Ast = parse("program p; type IntFile = file of integer; begin end.");
    auto *Ft = llvm::dyn_cast<FileTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(Ft, nullptr);
    ASSERT_NE(Ft->Element, nullptr);
    EXPECT_EQ(namedType(Ft->Element.get()), "integer");
}

TEST(ParserNewTypes, UntypedFile) {
    auto Ast = parse("program p; type F = file; begin end.");
    auto *Ft = llvm::dyn_cast<FileTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(Ft, nullptr);
    EXPECT_EQ(Ft->Element.get(), nullptr);
}

TEST(ParserNewTypes, PackedArray) {
    auto Ast = parse("program p; type Str = packed array[1..80] of char; begin end.");
    auto *At = llvm::dyn_cast<ArrayTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(At, nullptr);
    EXPECT_TRUE(At->Packed);
    EXPECT_EQ(namedType(At->Element.get()), "char");
}

TEST(ParserNewTypes, PackedRecord) {
    auto Ast = parse("program p; type R = packed record x : integer end; begin end.");
    auto *Rt = llvm::dyn_cast<RecordTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(Rt, nullptr);
    EXPECT_TRUE(Rt->Packed);
}

TEST(ParserNewTypes, RecordVariantPart) {
    auto Ast = parse(
        "program p;\n"
        "type Shape = record\n"
        "  case kind : integer of\n"
        "    1: (radius : real);\n"
        "    2: (width, height : real)\n"
        "end;\n"
        "begin end."
    );
    auto *Rt = llvm::dyn_cast<RecordTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(Rt, nullptr);
    EXPECT_TRUE(Rt->Fields.empty());
    ASSERT_NE(Rt->Variant.get(), nullptr);
    const auto &Vp = *Rt->Variant;
    EXPECT_EQ(Vp.TagField, "kind");
    EXPECT_EQ(namedType(Vp.TagType.get()), "integer");
    ASSERT_EQ(Vp.Cases.size(), 2u);
    EXPECT_EQ(Vp.Cases[0].Fields[0].Names[0], "radius");
    ASSERT_EQ(Vp.Cases[1].Fields[0].Names.size(), 2u);
    EXPECT_EQ(Vp.Cases[1].Fields[0].Names[1], "height");
}

TEST(ParserNewTypes, RecordFixedPlusVariant) {
    auto Ast = parse(
        "program p;\n"
        "type Tagged = record\n"
        "  name : string;\n"
        "  case t : boolean of\n"
        "    true: (x : integer);\n"
        "    false: (y : real)\n"
        "end;\n"
        "begin end."
    );
    auto *Rt = llvm::dyn_cast<RecordTypeNode>(Ast->Block->Types[0].Type.get());
    ASSERT_NE(Rt, nullptr);
    ASSERT_EQ(Rt->Fields.size(), 1u);
    EXPECT_EQ(Rt->Fields[0].Names[0], "name");
    ASSERT_NE(Rt->Variant.get(), nullptr);
    EXPECT_EQ(Rt->Variant->Cases.size(), 2u);
}

TEST(ParserNewTypes, CharType) {
    auto Ast = parse("program p; var c : char; begin end.");
    EXPECT_EQ(namedType(Ast->Block->Vars[0].Type.get()), "char");
}

// ---------------------------------------------------------------------------
// var parameters and forward declarations
// ---------------------------------------------------------------------------

TEST(ParserNewFeatures, VarParameter) {
    auto Ast = parse("program p; procedure swap(var a, b : integer); begin end; begin end.");
    const auto &Pg = Ast->Block->Procs[0]->Params[0];
    EXPECT_TRUE(Pg.IsVar);
    ASSERT_EQ(Pg.Names.size(), 2u);
    EXPECT_EQ(Pg.Names[0], "a");
}

TEST(ParserNewFeatures, ValueParameter) {
    auto Ast = parse("program p; procedure f(x : integer); begin end; begin end.");
    EXPECT_FALSE(Ast->Block->Procs[0]->Params[0].IsVar);
}

TEST(ParserNewFeatures, MixedVarAndValueParams) {
    auto Ast = parse(
        "program p;\n"
        "procedure f(x : integer; var y : real);\n"
        "begin end;\n"
        "begin end."
    );
    const auto &Prms = Ast->Block->Procs[0]->Params;
    ASSERT_EQ(Prms.size(), 2u);
    EXPECT_FALSE(Prms[0].IsVar);
    EXPECT_TRUE(Prms[1].IsVar);
}

TEST(ParserNewFeatures, ForwardDeclaration) {
    auto Ast = parse(
        "program p;\n"
        "procedure foo(x : integer); forward;\n"
        "procedure foo(x : integer);\n"
        "begin end;\n"
        "begin end."
    );
    ASSERT_EQ(Ast->Block->Procs.size(), 2u);
    EXPECT_TRUE(Ast->Block->Procs[0]->IsForward);
    EXPECT_EQ(Ast->Block->Procs[0]->Body.get(), nullptr);
    EXPECT_FALSE(Ast->Block->Procs[1]->IsForward);
    EXPECT_NE(Ast->Block->Procs[1]->Body.get(), nullptr);
}

TEST(ParserNewFeatures, FunctionForward) {
    auto Ast = parse(
        "program p;\n"
        "function max(a, b : integer) : integer; forward;\n"
        "function max(a, b : integer) : integer;\n"
        "begin max := a end;\n"
        "begin end."
    );
    EXPECT_TRUE(Ast->Block->Procs[0]->IsForward);
    EXPECT_TRUE(Ast->Block->Procs[0]->IsFunction);
}

// ---------------------------------------------------------------------------
// Labels and goto
// ---------------------------------------------------------------------------

TEST(ParserNewFeatures, LabelDeclaration) {
    auto Ast = parse("program p; label 10, 20; begin end.");
    ASSERT_EQ(Ast->Block->Labels.size(), 2u);
    EXPECT_EQ(Ast->Block->Labels[0], "10");
    EXPECT_EQ(Ast->Block->Labels[1], "20");
}

TEST(ParserNewFeatures, IdentifierLabel) {
    auto Ast = parse("program p; label done; begin end.");
    ASSERT_EQ(Ast->Block->Labels.size(), 1u);
    EXPECT_EQ(Ast->Block->Labels[0], "done");
}

TEST(ParserNewFeatures, GotoStatement) {
    auto Ast = parse("program p; label 99; begin goto 99 end.");
    auto *G = llvm::dyn_cast<GotoStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(G, nullptr);
    EXPECT_EQ(G->Label, "99");
}

TEST(ParserNewFeatures, IntegerLabeledStatement) {
    auto Ast = parse("program p; var x : integer; begin 99: x := 1 end.");
    auto *Ls = llvm::dyn_cast<LabeledStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(Ls, nullptr);
    EXPECT_EQ(Ls->Label, "99");
    EXPECT_NE(llvm::dyn_cast<AssignStmt>(Ls->Stmt.get()), nullptr);
}

TEST(ParserNewFeatures, IdentifierLabeledStatement) {
    auto Ast = parse("program p; var x : integer; begin done: x := 0 end.");
    auto *Ls = llvm::dyn_cast<LabeledStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(Ls, nullptr);
    EXPECT_EQ(Ls->Label, "done");
}

// ---------------------------------------------------------------------------
// Set literals and 'in' operator
// ---------------------------------------------------------------------------

TEST(ParserNewFeatures, EmptySetLiteral) {
    auto Ast = parse("program p; var s : integer; begin s := 0; if 1 in [] then s := 1 end.");
    // The if's condition uses the 'in' operator — just verify it parses.
    EXPECT_NE(Ast->Block->Body, nullptr);
}

TEST(ParserNewFeatures, SetLiteralSingletons) {
    auto Ast = parse("program p; var x : integer; begin x := 0; if x in [1, 2, 3] then x := 1 end.");
    auto *Ifs = llvm::dyn_cast<IfStmt>(Ast->Block->Body->Stmts[1].get());
    ASSERT_NE(Ifs, nullptr);
    auto *Cond = llvm::dyn_cast<BinaryExpr>(Ifs->Cond.get());
    ASSERT_NE(Cond, nullptr);
    EXPECT_EQ(Cond->Op, TokenKind::In);
    auto *Rhs = llvm::dyn_cast<SetLiteralExpr>(Cond->Right.get());
    ASSERT_NE(Rhs, nullptr);
    ASSERT_EQ(Rhs->Elements.size(), 3u);
}

TEST(ParserNewFeatures, SetLiteralWithRange) {
    auto Ast = parse("program p; var x : integer; begin if x in [1..10] then x := 0 end.");
    auto *Ifs = llvm::dyn_cast<IfStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(Ifs, nullptr);
    auto *Cond = llvm::dyn_cast<BinaryExpr>(Ifs->Cond.get());
    ASSERT_NE(Cond, nullptr);
    EXPECT_EQ(Cond->Op, TokenKind::In);
    auto *Rhs = llvm::dyn_cast<SetLiteralExpr>(Cond->Right.get());
    ASSERT_NE(Rhs, nullptr);
    ASSERT_EQ(Rhs->Elements.size(), 1u);
    EXPECT_NE(llvm::dyn_cast<SetRangeExpr>(Rhs->Elements[0].get()), nullptr);
}

TEST(ParserNewFeatures, InOperatorPrecedence) {
    // '1 + 2 in [3]' must parse as '(1 + 2) in [3]' (in is relop, lowest prec).
    auto Ast = parse("program p; var b : boolean; begin b := 1 + 2 in [3] end.");
    auto *A   = llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get());
    ASSERT_NE(A, nullptr);
    auto *Bin = llvm::dyn_cast<BinaryExpr>(A->Value.get());
    ASSERT_NE(Bin, nullptr);
    EXPECT_EQ(Bin->Op, TokenKind::In);
    EXPECT_NE(llvm::dyn_cast<BinaryExpr>(Bin->Left.get()), nullptr);
}

// ---------------------------------------------------------------------------
// Errors — new keywords
// ---------------------------------------------------------------------------

TEST(ParserErrors, MissingProgramName) {
    EXPECT_EQ(parse("program ; begin end."), nullptr);
}

TEST(ParserErrors, MissingHeaderSemicolon) {
    EXPECT_EQ(parse("program p begin end."), nullptr);
}

TEST(ParserErrors, MissingEnd) {
    EXPECT_EQ(parse("program p; begin"), nullptr);
}

TEST(ParserErrors, MissingFinalDot) {
    EXPECT_EQ(parse("program p; begin end"), nullptr);
}

TEST(ParserErrors, BadExpressionToken) {
    EXPECT_EQ(parse("program p; var x : integer; begin x := end end."), nullptr);
}

TEST(ParserErrors, IntegerOutOfRange) {
    // 10^19 > INT64_MAX (9223372036854775807) — must be rejected by the parser.
    EXPECT_EQ(parse("program p; var x : integer; begin x := 10000000000000000000 end."), nullptr);
}

TEST(ParserErrors, DeeplyNestedParensReportOneDiagnosticNotACrash) {
    // Issue #13: parseFactor's LeftParen case recurses through parseExpression
    // -> parseSimpleExpr -> parseTerm -> parsePower -> parseFactor with no
    // depth limit, so before this test existed, deeply nested parens exhausted
    // the real C++ stack instead of failing cleanly.  1000 levels is well past
    // the 500-deep ceiling (MaxExprDepth, ParseExpr.cpp) while staying fast to
    // build and parse in-process; the actual crash threshold (~20,000) is
    // exercised end-to-end, under the real compiler's real stack, by
    // ParserRobustness.DeeplyNestedParenthesesDoNotCrashTheCompiler in
    // driver_test.cpp.
    std::string Src = "program p; var x : integer; begin x := ";
    Src += std::string(1000, '(');
    Src += "1";
    Src += std::string(1000, ')');
    Src += " end.";
    EXPECT_EQ(parse(Src), nullptr);
    // The depth-limit diagnostic fires first, and the diagnostic count stays
    // small and independent of nesting depth: ExprDepthLimitHit suppresses
    // the "expected )" cascade that unwinding 500 stacked '(' frames would
    // otherwise produce on the way out -- one per frame, i.e. ~500 of them
    // without the suppression, not the handful seen here.  What is left is
    // the ordinary, bounded fallout of one expression failing to consume its
    // input: the enclosing compound-statement, program, and end-of-file check
    // each report once that they did not see what they expected either.
    ASSERT_FALSE(parseDiags.diagnostics().empty());
    EXPECT_NE(parseDiags.diagnostics().front().Message.find("nested too deeply"),
              std::string::npos) << parseDiags.diagnostics().front().Message;
    EXPECT_LE(parseDiags.diagnostics().size(), 5u);
}

TEST(ParserErrors, InvalidTypePosition) {
    // A number where a type expression is expected is a syntax error.
    EXPECT_EQ(parse("program p; var x : 42; begin end."), nullptr);
}

TEST(ParserErrors, MissingFunctionReturnTypeIsAcceptedForSemaToJudge) {
    // 'function f;' is how ISO §6.6.1 writes the defining occurrence of a
    // function declared 'forward' — the result type belongs to the
    // declaration and is not repeated.  Whether one is in scope is a question
    // about names, so the parser hands the heading on and Sema decides; see
    // ForwardDecl.FunctionWithoutAResultTypeAndNoForwardDeclaration.
    EXPECT_NE(parse("program p; function f; begin end; begin end."), nullptr);
}

TEST(ParserErrors, MissingThen) {
    EXPECT_EQ(parse("program p; var x : integer; begin if x > 0 x := 1 end."), nullptr);
}

TEST(ParserErrors, MissingDo) {
    EXPECT_EQ(parse("program p; var x : integer; begin while x > 0 x := x - 1 end."), nullptr);
}

// ---------------------------------------------------------------------------
// Integration
// ---------------------------------------------------------------------------

TEST(ParserIntegration, SmallProgram) {
    auto Ast = parse(
        "program calc;\n"
        "var x, y, result : integer;\n"
        "function max(a, b : integer) : integer;\n"
        "begin\n"
        "  if a > b then max := a else max := b\n"
        "end;\n"
        "begin\n"
        "  x := 10;\n"
        "  y := 20;\n"
        "  result := max(x, y)\n"
        "end."
    );
    EXPECT_EQ(Ast->Name, "calc");
    ASSERT_EQ(Ast->Block->Vars.size(), 1u);
    EXPECT_EQ(Ast->Block->Vars[0].Names.size(), 3u);
    ASSERT_EQ(Ast->Block->Procs.size(), 1u);
    EXPECT_EQ(Ast->Block->Procs[0]->Name, "max");
    EXPECT_TRUE(Ast->Block->Procs[0]->IsFunction);
    ASSERT_EQ(Ast->Block->Body->Stmts.size(), 3u);
    EXPECT_NE(llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[0].get()), nullptr);
    EXPECT_NE(llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[1].get()), nullptr);
    EXPECT_NE(llvm::dyn_cast<AssignStmt>(Ast->Block->Body->Stmts[2].get()), nullptr);
}

// ---------------------------------------------------------------------------
// AstPrinter — coverage for previously-missing node types
// ---------------------------------------------------------------------------

static LangOptions epOpts() {
    LangOptions O;
    O.Std = LangOptions::Standard::ISO10206;
    return O;
}

TEST(AstPrinter, CaseStmtPointLabels) {
    auto S = astOf(
        "program p; var i:integer; begin "
        "  case i of 1: writeln; 2: writeln end "
        "end.");
    EXPECT_NE(S.find("(case i"), std::string::npos);
    EXPECT_NE(S.find("(arm (1)"), std::string::npos);
    EXPECT_NE(S.find("(arm (2)"), std::string::npos);
}

TEST(AstPrinter, CaseStmtRangeLabels) {
    auto S = astOf(
        "program p; var i:integer; begin "
        "  case i of 1..3: writeln; otherwise writeln end "
        "end.", epOpts());
    EXPECT_NE(S.find("(case i"), std::string::npos);
    EXPECT_NE(S.find("1..3"),    std::string::npos);
    EXPECT_NE(S.find("(otherwise"), std::string::npos);
}

TEST(AstPrinter, CaseStmtMultipleLabelsAndOtherwise) {
    auto S = astOf(
        "program p; var i:integer; begin "
        "  case i of 5,7: writeln; otherwise writeln end "
        "end.", epOpts());
    // Two point labels in one arm printed space-separated inside the arm list.
    EXPECT_NE(S.find("(arm (5 7)"), std::string::npos);
    EXPECT_NE(S.find("(otherwise"),  std::string::npos);
}

TEST(AstPrinter, WriteParamWithWidth) {
    auto S = astOf(
        "program p; var i:integer; begin write(i:8) end.");
    EXPECT_NE(S.find("(write-param i :8)"), std::string::npos);
}

TEST(AstPrinter, WriteParamWithWidthAndDecimals) {
    auto S = astOf(
        "program p; var r:real; begin write(r:10:2) end.");
    EXPECT_NE(S.find("(write-param r :10 :2)"), std::string::npos);
}

TEST(AstPrinter, WriteParamNoSpecifier) {
    // A plain expression passed to write should not be wrapped in write-param.
    auto S = astOf(
        "program p; var i:integer; begin write(i) end.");
    EXPECT_EQ(S.find("write-param"), std::string::npos);
    EXPECT_NE(S.find("(call write i)"), std::string::npos);
}

TEST(AstPrinter, EPOperatorsInExpr) {
    auto S = astOf(
        "program p; var b:boolean; begin b := true and_then false end.",
        epOpts());
    EXPECT_NE(S.find("(and_then true false)"), std::string::npos);
}

TEST(AstPrinter, ExponentiationOperator) {
    auto S = astOf(
        "program p; var r:real; begin r := 2.0 ** 8.0 end.",
        epOpts());
    EXPECT_NE(S.find("(** 2"), std::string::npos);
}

// Every node the parser can build has to reach the dump, or a reader takes a
// blank where a construct was for a tree that never had one.  These five were
// each added to the language without being added here, and each printed
// nothing for as long as it had existed.
TEST(AstPrinter, PrintsTheSchemaInstanceItWasGiven) {
    auto S = astOf(
        "program p;\n"
        "type poly(n: integer) = record c: array[0..n] of real end;\n"
        "var q: poly(2);\n"
        "begin end.", epOpts());
    EXPECT_NE(S.find("(schema poly 2)"), std::string::npos) << S;
}

TEST(AstPrinter, PrintsATypeInquiry) {
    auto S = astOf(
        "program p; var a: integer; b: type of a; begin end.", epOpts());
    EXPECT_NE(S.find("(type-of a)"), std::string::npos) << S;
}

TEST(AstPrinter, PrintsAConformantArrayParameter) {
    auto S = astOf(
        "program p;\n"
        "procedure q(a: array[lo..hi: integer] of integer); begin end;\n"
        "begin end.", epOpts());
    EXPECT_NE(S.find("(conformant-array (lo hi integer) integer)"),
              std::string::npos) << S;
}

TEST(AstPrinter, PrintsAForInLoop) {
    auto S = astOf(
        "program p; var c: char; s: set of char;\n"
        "begin for c in s do end.", epOpts());
    EXPECT_NE(S.find("(for-in c s"), std::string::npos) << S;
}

TEST(AstPrinter, PrintsAStructuredValue) {
    auto S = astOf(
        "program p; type pt = record x, y: integer end; var v: pt;\n"
        "begin v := pt[x: 1; y: 2] end.", epOpts());
    EXPECT_NE(S.find("(value pt (x : 1) (y : 2))"), std::string::npos) << S;
}
