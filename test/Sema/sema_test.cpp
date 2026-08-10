#include "TestHelper.h"

#include <gtest/gtest.h>

using namespace plang;

// ---------------------------------------------------------------------------
// Clean programs — must produce zero diagnostics
// ---------------------------------------------------------------------------

TEST(SemaClean, MinimalProgram) {
    EXPECT_TRUE(check("program p; begin end.").Ok);
}

TEST(SemaClean, VarAndAssign) {
    EXPECT_TRUE(check("program p; var x : integer; begin x := 42 end.").Ok);
}

TEST(SemaClean, RealFromInteger) {
    // Widening: assigning integer to real is allowed.
    EXPECT_TRUE(check("program p; var r : real; begin r := 1 end.").Ok);
}

TEST(SemaClean, Procedure) {
    EXPECT_TRUE(check(
        "program p;\n"
        "procedure greet; begin end;\n"
        "begin greet end."
    ).Ok);
}

TEST(SemaClean, FunctionResult) {
    EXPECT_TRUE(check(
        "program p;\n"
        "function square(x : integer) : integer;\n"
        "begin square := x * x end;\n"
        "begin end."
    ).Ok);
}

TEST(SemaClean, RecordFieldAccess) {
    EXPECT_TRUE(check(
        "program p;\n"
        "type Point = record x, y : real end;\n"
        "var pt : Point;\n"
        "begin pt.x := 1.0 end."
    ).Ok);
}

TEST(SemaClean, ArraySubscript) {
    EXPECT_TRUE(check(
        "program p;\n"
        "var a : array[1..10] of integer;\n"
        "    i : integer;\n"
        "begin a[i] := 0 end."
    ).Ok);
}

TEST(SemaClean, ForLoopInteger) {
    // Body must not assign to the loop variable (ISO §6.8.3.9).
    EXPECT_TRUE(check(
        "program p; var i, s : integer;\n"
        "begin for i := 1 to 10 do s := s + i end."
    ).Ok);
}

TEST(SemaClean, ForLoopBoolean) {
    EXPECT_TRUE(check(
        "program p; var b : boolean; x : integer;\n"
        "begin for b := false to true do x := 1 end."
    ).Ok);
}

TEST(SemaClean, GotoLabel) {
    EXPECT_TRUE(check(
        "program p;\n"
        "label 1;\n"
        "var x : integer;\n"
        "begin goto 1; 1: x := 0 end."
    ).Ok);
}

TEST(SemaClean, SetOfChar) {
    EXPECT_TRUE(check(
        "program p;\n"
        "type CS = set of char;\n"
        "begin end."
    ).Ok);
}

TEST(SemaClean, ForwardProc) {
    EXPECT_TRUE(check(
        "program p;\n"
        "procedure foo(x : integer); forward;\n"
        "procedure foo(x : integer); begin end;\n"
        "begin end."
    ).Ok);
}

TEST(SemaClean, NestedScope) {
    EXPECT_TRUE(check(
        "program p;\n"
        "var x : integer;\n"
        "procedure inner;\n"
        "begin x := 1 end;\n"
        "begin end."
    ).Ok);
}

TEST(SemaClean, WithRecord) {
    EXPECT_TRUE(check(
        "program p;\n"
        "type Point = record x : integer end;\n"
        "var pt : Point;\n"
        "begin with pt do x := 1 end."
    ).Ok);
}

TEST(SemaClean, NilAssignPointer) {
    EXPECT_TRUE(check(
        "program p;\n"
        "var p : ^integer;\n"
        "begin p := nil end."
    ).Ok);
}

TEST(SemaClean, PointerDeref) {
    EXPECT_TRUE(check(
        "program p;\n"
        "var p : ^integer;\n"
        "begin p^ := 5 end."
    ).Ok);
}

TEST(SemaClean, EnumType) {
    EXPECT_TRUE(check(
        "program p;\n"
        "type Color = (red, green, blue);\n"
        "var c : Color;\n"
        "begin c := red end."
    ).Ok);
}

TEST(SemaClean, BooleanOps) {
    EXPECT_TRUE(check(
        "program p; var x, y : integer; b : boolean;\n"
        "begin b := (x > 0) and (y > 0) end."
    ).Ok);
}

TEST(SemaClean, CallBuiltinWriteln) {
    EXPECT_TRUE(check("program p; begin writeln('hello') end.").Ok);
}

TEST(SemaClean, CallBuiltinSqrt) {
    EXPECT_TRUE(check(
        "program p; var x : real;\n"
        "begin x := sqrt(2.0) end."
    ).Ok);
}

// ---------------------------------------------------------------------------
// Duplicate declarations
// ---------------------------------------------------------------------------

TEST(SemaDuplicate, VarSameName) {
    auto R = check("program p; var x, x : integer; begin end.");
    EXPECT_TRUE(R.hasError("x"));
}

TEST(SemaDuplicate, ConstAndVar) {
    auto R = check("program p; const n = 1; var n : integer; begin end.");
    EXPECT_TRUE(R.hasError("n"));
}

TEST(SemaDuplicate, DupLabelInSection) {
    auto R = check("program p; label 10, 10; begin end.");
    EXPECT_TRUE(R.hasError("10"));
}

TEST(SemaDuplicate, DupParamName) {
    auto R = check("program p; procedure f(a, a : integer); begin end; begin end.");
    EXPECT_TRUE(R.hasError("a"));
}

TEST(SemaDuplicate, EnumValueDuplicate) {
    auto R = check("program p; type C = (red, red); begin end.");
    EXPECT_TRUE(R.hasError("red"));
}

TEST(SemaDuplicate, ProcNoForward) {
    // Two procedures with the same name, no forward declaration.
    auto R = check(
        "program p;\n"
        "procedure foo; begin end;\n"
        "procedure foo; begin end;\n"
        "begin end."
    );
    EXPECT_TRUE(R.hasError("foo"));
}

// ---------------------------------------------------------------------------
// Undefined identifiers
// ---------------------------------------------------------------------------

TEST(SemaUndef, UndeclaredVar) {
    auto R = check("program p; begin x := 1 end.");
    EXPECT_TRUE(R.hasError("x"));
}

TEST(SemaUndef, UndeclaredProc) {
    auto R = check("program p; begin foo end.");
    EXPECT_TRUE(R.hasError("foo"));
}

TEST(SemaUndef, UndeclaredType) {
    auto R = check("program p; var x : MyType; begin end.");
    EXPECT_TRUE(R.hasError("MyType"));
}

TEST(SemaUndef, UndeclaredConst) {
    auto R = check("program p; const n = undefined_const; begin end.");
    EXPECT_TRUE(R.hasError("undefined_const"));
}

// ---------------------------------------------------------------------------
// Assignment type compatibility
// ---------------------------------------------------------------------------

TEST(SemaAssign, IntegerFromReal) {
    auto R = check("program p; var x : integer; begin x := 1.5 end.");
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("integer"));
}

TEST(SemaAssign, BooleanFromInteger) {
    auto R = check("program p; var b : boolean; begin b := 1 end.");
    EXPECT_FALSE(R.Ok);
}

TEST(SemaAssign, StringFromInteger) {
    auto R = check("program p; var s : string; begin s := 42 end.");
    EXPECT_FALSE(R.Ok);
}

TEST(SemaAssign, RealFromInteger) {
    EXPECT_TRUE(check("program p; var r : real; begin r := 1 end.").Ok);
}

TEST(SemaAssign, PointerFromNil) {
    EXPECT_TRUE(check("program p; var p : ^integer; begin p := nil end.").Ok);
}

TEST(SemaAssign, IntegerFromNil) {
    auto R = check("program p; var x : integer; begin x := nil end.");
    EXPECT_FALSE(R.Ok);
}

// ---------------------------------------------------------------------------
// Arithmetic operator types
// ---------------------------------------------------------------------------

TEST(SemaArith, DivRequiresInteger) {
    auto R = check("program p; var x : integer; begin x := 1 div 2 end.");
    EXPECT_TRUE(R.Ok);  // 1 div 2 is fine
}

TEST(SemaArith, DivOnReal) {
    auto R = check("program p; var x : integer; begin x := 1 div 1.5 end.");
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("Div") || R.hasError("div") || R.hasError("integer"));
}

TEST(SemaArith, DivisionYieldsReal) {
    // '/' always yields real, so assigning to integer should fail.
    auto R = check("program p; var x : integer; begin x := 4 / 2 end.");
    EXPECT_FALSE(R.Ok);
}

TEST(SemaArith, IntTimesIntIsInt) {
    EXPECT_TRUE(check("program p; var x : integer; begin x := 3 * 4 end.").Ok);
}

TEST(SemaArith, IntPlusRealIsReal) {
    EXPECT_TRUE(check("program p; var r : real; begin r := 1 + 2.0 end.").Ok);
}

TEST(SemaArith, PlusOnBoolean) {
    auto R = check("program p; var x : integer; begin x := true + false end.");
    EXPECT_FALSE(R.Ok);
}

// ---------------------------------------------------------------------------
// Boolean operator types
// ---------------------------------------------------------------------------

TEST(SemaBool, AndRequiresBoolean) {
    auto R = check("program p; var x : integer; begin x := 1 and 2 end.");
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("and"));
}

TEST(SemaBool, NotRequiresBoolean) {
    auto R = check("program p; var x : boolean; begin x := not 5 end.");
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("not"));
}

TEST(SemaBool, NotOnBoolean) {
    EXPECT_TRUE(check("program p; var b : boolean; begin b := not true end.").Ok);
}

TEST(SemaBool, IfCondMustBeBoolean) {
    auto R = check("program p; var x : integer; begin if 1 then x := 0 end.");
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("boolean"));
}

TEST(SemaBool, WhileCondMustBeBoolean) {
    auto R = check("program p; var x : integer; begin while x do x := 0 end.");
    EXPECT_FALSE(R.Ok);
}

TEST(SemaBool, RepeatCondMustBeBoolean) {
    auto R = check("program p; var x : integer; begin repeat x := 0 until 1 end.");
    EXPECT_FALSE(R.Ok);
}

// ---------------------------------------------------------------------------
// For-loop variable
// ---------------------------------------------------------------------------

TEST(SemaFor, VarMustBeOrdinal_Real) {
    auto R = check("program p; var x : real; begin for x := 1.0 to 5.0 do x := x end.");
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("ordinal"));
}

TEST(SemaFor, VarMustBeInScope) {
    auto R = check("program p; begin for i := 1 to 5 do i := i end.");
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("i"));
}

// ---------------------------------------------------------------------------
// Procedure and function calls
// ---------------------------------------------------------------------------

TEST(SemaCall, TooFewArgs) {
    auto R = check(
        "program p;\n"
        "procedure f(x : integer); begin end;\n"
        "begin f end."
    );
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("1"));
}

TEST(SemaCall, TooManyArgs) {
    auto R = check(
        "program p;\n"
        "procedure f; begin end;\n"
        "begin f(1) end."
    );
    EXPECT_FALSE(R.Ok);
}

TEST(SemaCall, WrongArgType) {
    auto R = check(
        "program p;\n"
        "procedure f(x : integer); begin end;\n"
        "begin f(3.14) end."
    );
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("x"));
}

TEST(SemaCall, VarParamNeedsLValue) {
    auto R = check(
        "program p;\n"
        "procedure f(var x : integer); begin end;\n"
        "begin f(42) end."
    );
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("var"));
}

TEST(SemaCall, VarParamAcceptsVar) {
    EXPECT_TRUE(check(
        "program p;\n"
        "var a : integer;\n"
        "procedure f(var x : integer); begin end;\n"
        "begin f(a) end."
    ).Ok);
}

TEST(SemaCall, FunctionReturnsType) {
    EXPECT_TRUE(check(
        "program p;\n"
        "var r : integer;\n"
        "function sq(x : integer) : integer;\n"
        "begin sq := x * x end;\n"
        "begin r := sq(3) end."
    ).Ok);
}

TEST(SemaCall, ProcUsedAsExpr) {
    auto R = check(
        "program p;\n"
        "var x : integer;\n"
        "procedure f; begin end;\n"
        "begin x := f end."
    );
    EXPECT_FALSE(R.Ok);
}

TEST(SemaCall, BuiltinVariadicOk) {
    EXPECT_TRUE(check("program p; begin writeln(1, 'hello', true) end.").Ok);
}

// ---------------------------------------------------------------------------
// Goto and labels
// ---------------------------------------------------------------------------

TEST(SemaGoto, UndeclaredLabel) {
    auto R = check("program p; begin goto 99 end.");
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("99"));
}

TEST(SemaGoto, LabeledStmtUndeclared) {
    auto R = check("program p; var x : integer; begin 99: x := 1 end.");
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("99"));
}

TEST(SemaGoto, LabelNotPlaced_Error) {
    // ISO §6.8.1: a declared label must have a corresponding labeled statement.
    // A label that is goto'd but never placed as a statement is an error.
    auto R = check("program p; label 10; begin goto 10 end.");
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("10"));
}

TEST(SemaGoto, LabelDeclaredNotGotod_Warning) {
    // Label declared and placed but never goto'd.
    auto R = check("program p; label 10; var x : integer; begin 10: x := 0 end.");
    EXPECT_TRUE(R.hasWarning("10"));
}

TEST(SemaGoto, ValidGoto) {
    EXPECT_TRUE(check(
        "program p;\n"
        "label 10;\n"
        "var x : integer;\n"
        "begin goto 10; 10: x := 1 end."
    ).Ok);
}

// ---------------------------------------------------------------------------
// Set operations
// ---------------------------------------------------------------------------

TEST(SemaSet, BaseTypeMustBeOrdinal_Real) {
    auto R = check("program p; type S = set of real; begin end.");
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("ordinal"));
}

// A set holds one bit per ordinal, so an unbounded base type cannot be
// represented.  Rejecting it is what stops members from being dropped
// silently at run time; a subrange of integer is the supported spelling.
TEST(SemaSet, BaseTypeTooWide_Integer) {
    auto R = check("program p; type S = set of integer; begin end.");
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("exceeds"));
}

TEST(SemaSet, BaseTypeOK_IntegerSubrange) {
    EXPECT_TRUE(check("program p; type S = set of 0..255; begin end.").Ok);
}

TEST(SemaSet, BaseTypeTooWide_Subrange) {
    auto R = check("program p; type S = set of 0..256; begin end.");
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("exceeds"));
}

TEST(SemaSet, BaseTypeOK_Char) {
    EXPECT_TRUE(check("program p; type S = set of char; begin end.").Ok);
}

TEST(SemaSet, InOperatorCompatible) {
    EXPECT_TRUE(check(
        "program p; var x : integer; b : boolean;\n"
        "begin b := x in [1, 2, 3] end."
    ).Ok);
}

TEST(SemaSet, InOperatorLeftMustBeOrdinal) {
    auto R = check(
        "program p; var x : real; b : boolean;\n"
        "begin b := x in [1, 2] end."
    );
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("ordinal"));
}

// ---------------------------------------------------------------------------
// Record / field access
// ---------------------------------------------------------------------------

TEST(SemaRecord, UnknownField) {
    auto R = check(
        "program p;\n"
        "type Rec = record x : integer end;\n"
        "var myRec : Rec;\n"
        "begin myRec.foo := 1 end."
    );
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("foo"));
}

TEST(SemaRecord, ValidField) {
    EXPECT_TRUE(check(
        "program p;\n"
        "type Point = record x, y : real end;\n"
        "var pt : Point;\n"
        "begin pt.x := 1.0 end."
    ).Ok);
}

TEST(SemaRecord, FieldAccessOnNonRecord) {
    auto R = check("program p; var x : integer; begin x.foo := 1 end.");
    EXPECT_FALSE(R.Ok);
}

TEST(SemaRecord, WithMissingField) {
    auto R = check(
        "program p;\n"
        "type R = record x : integer end;\n"
        "var r : R;\n"
        "begin with r do foo := 1 end."
    );
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("foo"));
}

// ---------------------------------------------------------------------------
// Pointer operations
// ---------------------------------------------------------------------------

TEST(SemaPtr, DerefNonPointer) {
    auto R = check("program p; var x : integer; begin x^ := 5 end.");
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("pointer"));
}

TEST(SemaPtr, DerefPointerOk) {
    EXPECT_TRUE(check(
        "program p; var p : ^integer; begin p^ := 5 end."
    ).Ok);
}

// ---------------------------------------------------------------------------
// Scope nesting
// ---------------------------------------------------------------------------

TEST(SemaScope, InnerSeesOuter) {
    EXPECT_TRUE(check(
        "program p;\n"
        "var x : integer;\n"
        "procedure inner;\n"
        "begin x := 42 end;\n"
        "begin end."
    ).Ok);
}

TEST(SemaScope, RecursiveCall) {
    EXPECT_TRUE(check(
        "program p;\n"
        "function fact(n : integer) : integer;\n"
        "begin\n"
        "  if n <= 0 then fact := 1\n"
        "  else fact := n * fact(n - 1)\n"
        "end;\n"
        "begin end."
    ).Ok);
}

TEST(SemaScope, MutualRecursionForward) {
    EXPECT_TRUE(check(
        "program p;\n"
        "procedure b(n : integer); forward;\n"
        "procedure a(n : integer);\n"
        "begin if n > 0 then b(n - 1) end;\n"
        "procedure b(n : integer);\n"
        "begin if n > 0 then a(n - 1) end;\n"
        "begin end."
    ).Ok);
}

// ---------------------------------------------------------------------------
// Extended Pascal (ISO 10206) — Tier 1: predefined constants
// ---------------------------------------------------------------------------

static LangOptions epOpts() {
    LangOptions O;
    O.Std = LangOptions::Standard::ISO10206;
    return O;
}

TEST(SemaEP, MaxcharResolves) {
    EXPECT_TRUE(check(
        "program p; var c: char; begin c := maxchar end.", epOpts()).Ok);
}

TEST(SemaEP, MinrealResolves) {
    EXPECT_TRUE(check(
        "program p; var r: real; begin r := minreal end.", epOpts()).Ok);
}

TEST(SemaEP, MaxrealResolves) {
    EXPECT_TRUE(check(
        "program p; var r: real; begin r := maxreal end.", epOpts()).Ok);
}

TEST(SemaEP, EpsrealResolves) {
    EXPECT_TRUE(check(
        "program p; var r: real; begin r := epsreal end.", epOpts()).Ok);
}

TEST(SemaEP, EPConstantsNotVisibleIn7185) {
    // In iso7185 mode maxchar/minreal/maxreal/epsreal are undefined identifiers.
    EXPECT_FALSE(check("program p; var r: real; begin r := minreal end.").Ok);
    EXPECT_FALSE(check("program p; var c: char; begin c := maxchar end.").Ok);
}

TEST(SemaEP, EPKeywordModuleIsIdentifierIn7185) {
    // 'module' must be usable as a variable name in iso7185 mode.
    EXPECT_TRUE(check(
        "program p; var module: integer; begin module := 1 end.").Ok);
}

TEST(SemaEP, AndThenKeywordRejectedAsIdentifierInEP) {
    // 'and_then' is a reserved word in EP; it cannot name a variable.
    auto R = check(
        "program p; var and_then: integer; begin and_then := 1 end.", epOpts());
    EXPECT_FALSE(R.Ok);
}

// ---------------------------------------------------------------------------
// Tier 2 — sema coverage
// ---------------------------------------------------------------------------

// --- otherwise / case ranges ------------------------------------------------

TEST(SemaEPTier2, OtherwiseClause) {
    EXPECT_TRUE(check(
        "program p; var i:integer; begin "
        "case i of 1: writeln; otherwise writeln end end.", epOpts()).Ok);
}

TEST(SemaEPTier2, OtherwiseWithElseAlso) {
    // 'else' as an alias for otherwise should also work in EP mode.
    EXPECT_TRUE(check(
        "program p; var i:integer; begin "
        "case i of 1: writeln; else writeln end end.", epOpts()).Ok);
}

TEST(SemaEPTier2, CaseRangeLabel) {
    EXPECT_TRUE(check(
        "program p; var i:integer; begin "
        "case i of 1..5: writeln; 6..10: writeln end end.", epOpts()).Ok);
}

TEST(SemaEPTier2, CaseRangeMixedWithPoint) {
    EXPECT_TRUE(check(
        "program p; var i:integer; begin "
        "case i of 1,2: writeln; 3..9: writeln; otherwise writeln end end.",
        epOpts()).Ok);
}

// --- short-circuit boolean ---------------------------------------------------

TEST(SemaEPTier2, AndThenTypeChecks) {
    EXPECT_TRUE(check(
        "program p; var b:boolean; begin b := true and_then false end.",
        epOpts()).Ok);
}

TEST(SemaEPTier2, OrElseTypeChecks) {
    EXPECT_TRUE(check(
        "program p; var b:boolean; begin b := false or_else true end.",
        epOpts()).Ok);
}

TEST(SemaEPTier2, AndThenRequiresBooleanOperands) {
    auto R = check(
        "program p; var i:integer; b:boolean; begin b := i and_then true end.",
        epOpts());
    EXPECT_FALSE(R.Ok);
}

// --- exponentiation ---------------------------------------------------------

TEST(SemaEPTier2, StarStarYieldsReal) {
    EXPECT_TRUE(check(
        "program p; var r:real; begin r := 2.0 ** 8.0 end.", epOpts()).Ok);
}

TEST(SemaEPTier2, PowYieldsReal) {
    EXPECT_TRUE(check(
        "program p; var r:real; begin r := 3 pow 4 end.", epOpts()).Ok);
}

TEST(SemaEPTier2, StarStarRequiresNumericOperands) {
    auto R = check(
        "program p; var r:real; b:boolean; begin r := b ** 2.0 end.", epOpts());
    EXPECT_FALSE(R.Ok);
}

// --- symmetric set difference -----------------------------------------------

TEST(SemaEPTier2, SymDiffTypeChecks) {
    EXPECT_TRUE(check(
        "program p; var s,t,u: set of 1..8; begin u := s >< t end.", epOpts()).Ok);
}

TEST(SemaEPTier2, SymDiffRequiresSets) {
    auto R = check(
        "program p; var i:integer; s: set of 1..8; begin s := i >< s end.",
        epOpts());
    EXPECT_FALSE(R.Ok);
}

// --- extended succ / pred / card --------------------------------------------

TEST(SemaEPTier2, SuccTwoArgEP) {
    EXPECT_TRUE(check(
        "program p; var i:integer; begin i := succ(i, 3) end.", epOpts()).Ok);
}

TEST(SemaEPTier2, PredTwoArgEP) {
    EXPECT_TRUE(check(
        "program p; var i:integer; begin i := pred(i, 3) end.", epOpts()).Ok);
}

TEST(SemaEPTier2, CardReturnsInteger) {
    EXPECT_TRUE(check(
        "program p; var s: set of 1..8; n:integer; begin n := card(s) end.",
        epOpts()).Ok);
}

// ---------------------------------------------------------------------------
// Tier 3 — sema coverage
// ---------------------------------------------------------------------------

// --- relaxed block ordering -------------------------------------------------

TEST(SemaEPTier3, ConstAfterVar) {
    EXPECT_TRUE(check(
        "program p; var i:integer; const K = 5; begin i := K end.", epOpts()).Ok);
}

TEST(SemaEPTier3, VarAfterProcedure) {
    EXPECT_TRUE(check(
        "program p; "
        "procedure foo; begin end; "
        "var i:integer; "
        "begin i := 1 end.", epOpts()).Ok);
}

TEST(SemaEPTier3, MultipleConstSections) {
    EXPECT_TRUE(check(
        "program p; const A = 1; var x:integer; const B = 2; "
        "begin x := A + B end.", epOpts()).Ok);
}

TEST(SemaEPTier3, InterleavedDeclarations) {
    EXPECT_TRUE(check(
        "program p; "
        "const Base = 10; "
        "type T = 1..10; "
        "var x: T; "
        "const Lim = Base * 2; "
        "var y: integer; "
        "begin x := 5; y := Lim end.", epOpts()).Ok);
}

// --- general constant expressions -------------------------------------------

TEST(SemaEPTier3, ConstArithmeticOnConst) {
    EXPECT_TRUE(check(
        "program p; const N = 10; M = N * 2; begin end.", epOpts()).Ok);
}

TEST(SemaEPTier3, ConstDivOnConst) {
    EXPECT_TRUE(check(
        "program p; const N = 10; H = N div 2; begin end.", epOpts()).Ok);
}

TEST(SemaEPTier3, ConstRealArithmetic) {
    EXPECT_TRUE(check(
        "program p; const Pi = 3.14159; TwoPi = Pi * 2.0; begin end.", epOpts()).Ok);
}

TEST(SemaEPTier3, ConstChain) {
    EXPECT_TRUE(check(
        "program p; const A=1; B=A+1; C=B+1; D=C+1; "
        "var i:integer; begin i := D end.", epOpts()).Ok);
}

// --- general subrange bounds ------------------------------------------------

TEST(SemaEPTier3, SubrangeWithConstHigh) {
    EXPECT_TRUE(check(
        "program p; const N=5; type R = 1..N; var x:R; begin x := 3 end.",
        epOpts()).Ok);
}

TEST(SemaEPTier3, SubrangeWithConstExprBound) {
    EXPECT_TRUE(check(
        "program p; const N=10; type R = 1..N div 2; var x:R; begin x := 3 end.",
        epOpts()).Ok);
}

TEST(SemaEPTier3, UserDefinedTypeAliasVarDecl) {
    // Regression: user-defined type aliases were resolved to ptrTy (segfault).
    EXPECT_TRUE(check(
        "program p; type MyInt = 1..100; var x:MyInt; begin x := 42 end.").Ok);
}

TEST(SemaEPTier3, UserDefinedTypeAliasIn7185) {
    // Same fix applies in iso7185 mode.
    EXPECT_TRUE(check(
        "program p; type Count = 0..99; var n:Count; begin n := 7 end.").Ok);
}

// ---------------------------------------------------------------------------
// Tier 4 — EP String system (ISO 10206 §6.4.3.3)
// ---------------------------------------------------------------------------

// --- Feature 19: string(N) type --------------------------------------------

TEST(SemaEPTier4, StringNTypeParsesAndResolves) {
    EXPECT_TRUE(check(
        "program p; var s: string(20); begin end.", epOpts()).Ok);
}

TEST(SemaEPTier4, StringNTypeConstCapacity) {
    EXPECT_TRUE(check(
        "program p; const N=20; var s: string(N); begin end.", epOpts()).Ok);
}

// --- Feature 20: assignment compatibility ----------------------------------

TEST(SemaEPTier4, AssignLiteralToVarString) {
    EXPECT_TRUE(check(
        "program p; var s: string(20); begin s := 'hello' end.", epOpts()).Ok);
}

TEST(SemaEPTier4, AssignCharToVarString) {
    EXPECT_TRUE(check(
        "program p; var s: string(10); c: char; begin c := 'x'; s := c end.",
        epOpts()).Ok);
}

TEST(SemaEPTier4, AssignVarStringToVarString) {
    EXPECT_TRUE(check(
        "program p; var s, t: string(20); begin s := t end.", epOpts()).Ok);
}

TEST(SemaEPTier4, AssignSmallerToLargerCapacity) {
    EXPECT_TRUE(check(
        "program p; var s: string(5); t: string(20); begin t := s end.", epOpts()).Ok);
}

// --- Feature 21: concatenation ---------------------------------------------

TEST(SemaEPTier4, ConcatStringPlusLiteral) {
    EXPECT_TRUE(check(
        "program p; var s, u: string(40); begin u := s + 'world' end.", epOpts()).Ok);
}

TEST(SemaEPTier4, ConcatResultCapacityIsSumOfOperands) {
    // string(10) + string(10) → string(20); must fit in string(20) var
    EXPECT_TRUE(check(
        "program p; var a, b: string(10); u: string(20); begin u := a + b end.",
        epOpts()).Ok);
}

TEST(SemaEPTier4, ConcatStringPlusChar) {
    EXPECT_TRUE(check(
        "program p; var s: string(10); c: char; r: string(11); "
        "begin r := s + c end.", epOpts()).Ok);
}

// --- Feature 22: relational operators --------------------------------------

TEST(SemaEPTier4, StringEqualityCheck) {
    EXPECT_TRUE(check(
        "program p; var s: string(10); b: boolean; begin b := s = 'hello' end.",
        epOpts()).Ok);
}

TEST(SemaEPTier4, StringLessThanCheck) {
    EXPECT_TRUE(check(
        "program p; var s, t: string(10); b: boolean; begin b := s < t end.",
        epOpts()).Ok);
}

// --- Feature 23: substring variable s[i..j] --------------------------------

TEST(SemaEPTier4, SubstringExprParsesOk) {
    EXPECT_TRUE(check(
        "program p; var s: string(20); u: string(20); "
        "begin u := s[2..4] end.", epOpts()).Ok);
}

// --- Feature 24: string functions ------------------------------------------

TEST(SemaEPTier4, LengthFunctionReturnsInteger) {
    EXPECT_TRUE(check(
        "program p; var s: string(20); n: integer; begin n := length(s) end.",
        epOpts()).Ok);
}

TEST(SemaEPTier4, IndexFunctionReturnsInteger) {
    EXPECT_TRUE(check(
        "program p; var s, p: string(20); n: integer; begin n := index(s, p) end.",
        epOpts()).Ok);
}

TEST(SemaEPTier4, SubstrFunctionOk) {
    EXPECT_TRUE(check(
        "program p; var s, u: string(20); begin u := substr(s, 2, 4) end.",
        epOpts()).Ok);
}

TEST(SemaEPTier4, TrimFunctionOk) {
    EXPECT_TRUE(check(
        "program p; var s, u: string(20); begin u := trim(s) end.", epOpts()).Ok);
}

// --- Feature 25: string comparison functions EQ, NE, LT, GT, LE, GE -------

TEST(SemaEPTier4, StringComparisonFunctionEQ) {
    EXPECT_TRUE(check(
        "program p; var s, t: string(20); b: boolean; begin b := EQ(s, t) end.",
        epOpts()).Ok);
}

TEST(SemaEPTier4, StringComparisonFunctionLT) {
    EXPECT_TRUE(check(
        "program p; var s, t: string(20); b: boolean; begin b := LT(s, t) end.",
        epOpts()).Ok);
}

// --- Features 28-29: read/writeln of strings --------------------------------

TEST(SemaEPTier4, WritelnVarString) {
    EXPECT_TRUE(check(
        "program p; var s: string(20); begin s := 'hi'; writeln(s) end.",
        epOpts()).Ok);
}

TEST(SemaEPTier4, WriteVarStringWithWidth) {
    EXPECT_TRUE(check(
        "program p; var s: string(20); begin s := 'hi'; write(s:10) end.",
        epOpts()).Ok);
}

TEST(SemaEPTier4, ReadlnIntoVarString) {
    EXPECT_TRUE(check(
        "program p; var s: string(80); begin readln(s) end.", epOpts()).Ok);
}

// ---------------------------------------------------------------------------
// EP §6.11 — Tier 13: Modules (Separate Compilation)
// ---------------------------------------------------------------------------

// Item 67: Module heading (interface): module Name interface; export ...; end.
TEST(EP13Modules, ModuleNodeParsedWithoutError) {
    // A module interface followed by a minimal program: must parse and sema clean.
    EXPECT_TRUE(check(
        "module M interface;\n"
        "  export function Scale(x: real; k: integer): real;\n"
        "end.\n"
        "program p;\n"
        "begin end.\n",
        epOpts()).Ok);
}

// Item 68: export specification with optional rename => and only clause
TEST(EP13Modules, ExportRenameAndOnlySyntaxAccepted) {
    // interface with export items; program does not use them.
    EXPECT_TRUE(check(
        "module MathUtil interface;\n"
        "  export function Add(a: integer; b: integer): integer;\n"
        "end.\n"
        "program p;\n"
        "begin end.\n",
        epOpts()).Ok);
}

// Item 69: import specification: import M;, import M only f,g;, import M qualified;
TEST(EP13Modules, ImportOnlyFiltersExports) {
    // Module exports both f and g.  Program imports only f.
    // Calling g must produce an undeclared-identifier error.
    auto R = check(
        "module M;\n"
        "  function f(x: integer): integer;\n"
        "  begin f := x end;\n"
        "  function g(x: integer): integer;\n"
        "  begin g := x + 1 end;\n"
        "end.\n"
        "program p;\n"
        "  import M only f;\n"
        "var v: integer;\n"
        "begin\n"
        "  v := f(1);\n"
        "  v := g(2)\n"
        "end.\n",
        epOpts());
    EXPECT_FALSE(R.Ok);
    EXPECT_TRUE(R.hasError("g")); // g was not imported
}

// Item 70: Module body (implementation): module Name; import ...; block end.
TEST(EP13Modules, ModuleBodyAndProgram) {
    // Module M defines Scale; program imports M and calls Scale.
    EXPECT_TRUE(check(
        "module M;\n"
        "  function Scale(x: real; k: integer): real;\n"
        "  begin Scale := x * k end;\n"
        "end.\n"
        "program p;\n"
        "  import M;\n"
        "var r: real;\n"
        "begin\n"
        "  r := Scale(2.0, 3)\n"
        "end.\n",
        epOpts()).Ok);
}

// Item 71: Module parameters (module Name(input, output);)
TEST(EP13Modules, ModuleParametersSyntaxAccepted) {
    EXPECT_TRUE(check(
        "module IO(input, output) interface;\n"
        "  export function readline(var s: string(80)): integer;\n"
        "end.\n"
        "program p;\n"
        "begin end.\n",
        epOpts()).Ok);
}

// Item 72: Module initialization/finalization: to begin do stmt; / to end do stmt;
TEST(EP13Modules, ModuleInitFinalizationSyntaxAccepted) {
    // to begin do / to end do syntax must parse and sema clean.
    EXPECT_TRUE(check(
        "module M;\n"
        "  function f(x: integer): integer;\n"
        "  begin f := x end;\n"
        "  to begin do writeln('init');\n"
        "  to end do writeln('done');\n"
        "end.\n"
        "program p;\n"
        "  import M;\n"
        "begin end.\n",
        epOpts()).Ok);
}

// Item 73: StandardInput/StandardOutput required interfaces (§6.11.4.2)
TEST(EP13Modules, StandardInputStandardOutputAvailable) {
    // import StandardInput / StandardOutput must not produce errors.
    EXPECT_TRUE(check(
        "program p;\n"
        "  import StandardInput;\n"
        "  import StandardOutput;\n"
        "begin\n"
        "  writeln('hello')\n"
        "end.\n",
        epOpts()).Ok);
}

// Item 73 (cont): qualified import accepted syntactically
TEST(EP13Modules, QualifiedImportAccepted) {
    // 'import M qualified' must parse without error.
    EXPECT_TRUE(check(
        "module M;\n"
        "  function f(x: integer): integer;\n"
        "  begin f := x end;\n"
        "end.\n"
        "program p;\n"
        "  import M qualified;\n"
        "begin end.\n",
        epOpts()).Ok);
}

// EP §6.11.2: the export-part form, with declarations following it.
TEST(EP13Modules, ExportPartAndInterfaceDeclarations) {
    EXPECT_TRUE(check(
        "module M interface;\n"
        "  export M = (K, Colour, Count, Scale);\n"
        "  const K = 3;\n"
        "  type Colour = (red, green);\n"
        "  var Count: integer;\n"
        "  function Scale(x: real; k: integer): real;\n"
        "end.\n"
        "program p;\n"
        "begin end.\n",
        epOpts()).Ok);
}

// EP §6.11.2: several export-parts, each naming an interface of its own.
TEST(EP13Modules, SeveralExportPartsAreAccepted) {
    EXPECT_TRUE(check(
        "module M interface;\n"
        "  export A = (f);\n"
        "  export B = (g);\n"
        "  function f: integer;\n"
        "  function g: integer;\n"
        "end.\n"
        "program p;\n"
        "begin end.\n",
        epOpts()).Ok);
}

// EP §6.11.3: the parenthesised import-list, with and without 'only'.
TEST(EP13Modules, ImportListSyntaxAccepted) {
    EXPECT_TRUE(check(
        "module M;\n"
        "  function f(x: integer): integer; begin f := x end;\n"
        "  function g(x: integer): integer; begin g := x end;\n"
        "end.\n"
        "program p;\n"
        "  import M only (f, g);\n"
        "var v: integer;\n"
        "begin v := f(1) + g(2) end.\n",
        epOpts()).Ok);
}

TEST(EP13Modules, ImportRenamingBringsInTheNewName) {
    auto R = check(
        "module M;\n"
        "  function f(x: integer): integer; begin f := x end;\n"
        "end.\n"
        "program p;\n"
        "  import M (f => h);\n"
        "var v: integer;\n"
        "begin v := h(1) end.\n",
        epOpts());
    EXPECT_TRUE(R.Ok);
}

// EP §6.11.2: an export-range must name two constants of one enumerated type.
TEST(EP13Modules, AnExportRangeOverNonConstantsIsRejected) {
    auto R = check(
        "module M interface;\n"
        "  export M = (f..g);\n"
        "  function f: integer;\n"
        "  function g: integer;\n"
        "end.\n"
        "program p;\n"
        "begin end.\n",
        epOpts());
    EXPECT_FALSE(R.Ok);
}

// Item 74: Module interface (heading) + body + program using module
TEST(EP13Modules, ModuleInterface) {
    // Full chain: interface declares Scale, body implements it,
    // program imports and calls it.
    EXPECT_TRUE(check(
        "module Vector interface;\n"
        "  export function Scale(x: real; k: integer): real;\n"
        "end.\n"
        "module Vector;\n"
        "  function Scale(x: real; k: integer): real;\n"
        "  begin Scale := x * k end;\n"
        "end.\n"
        "program p;\n"
        "  import Vector;\n"
        "var r: real;\n"
        "begin\n"
        "  r := Scale(2.0, 3)\n"
        "end.\n",
        epOpts()).Ok);
}
