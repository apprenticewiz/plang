/// codegen_test.cpp — what the generated code does when it runs
///
/// Compile a program, link it, run it, and check what it printed.  These are
/// the cases where the standard says what a construct means and the only way
/// to see whether plang agrees is to watch it happen: set operations, runtime
/// checks, file handling, real formatting, storage and scope.  A handful read
/// the LLVM IR instead, where the thing being asserted is that a particular
/// instruction was or was not emitted.
///
/// Every expected value here is also an answer the optimizer has to preserve,
/// which is why PLANG_TEST_EXTRA_FLAGS re-runs the lot at -O1 through -O3.

#include "DriverHarness.h"

#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// Regression tests — compile + run (full pipeline)
// ---------------------------------------------------------------------------

// Regression: user-defined type aliases were resolved to ptrTy in codegen,
// causing a segfault at runtime.  The fix adds a typeAliases map that routes
// named types through llvmTypeOfNode() instead of the ptrTy fallback.

TEST(Regression, UserDefinedSubrangeTypeAlias7185) {
    auto R = compileAndRun(
        "program p;\n"
        "type Count = 1..10;\n"
        "var n: Count;\n"
        "begin n := 7; writeln(n) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n");
}

TEST(Regression, UserDefinedSubrangeTypeAliasEP) {
    auto R = compileAndRun(
        "program p;\n"
        "const Max = 10;\n"
        "type SmallInt = 1..Max;\n"
        "var x: SmallInt;\n"
        "begin x := 3; writeln(x) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n");
}

TEST(Regression, UserDefinedSubrangeTypeAliasAssignAndRead) {
    // Exercises both assignment to and reading from an aliased subrange var.
    auto R = compileAndRun(
        "program p;\n"
        "type Pct = 0..100;\n"
        "var a, b: Pct;\n"
        "begin\n"
        "  a := 40;\n"
        "  b := 60;\n"
        "  writeln(a + b)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "100\n");
}

// ---------------------------------------------------------------------------
// IR-level tests — verify generated LLVM IR for high-risk codegen paths.
//
// These catch regressions in the Sema→Codegen type-annotation pipeline that
// runtime tests cannot: a wrong function call (e.g. store ptr instead of
// plang_str_from_cstr) may not segfault but silently produces garbage output.
// ---------------------------------------------------------------------------

// --- EP VarString assignment ------------------------------------------------

TEST(IRCodeGen, VarStringAssignLiteral) {
    // s := 'hello' must use plang_str_from_cstr, not a bare store of a ptr.
    auto R = compileAndEmitIR(
        "program p; var s: string(20); begin s := 'hello' end.\n",
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"plang_str_from_cstr"}))
        << "expected plang_str_from_cstr for literal→VarString\n" << R.IR;
    // Guard against the pre-fix bug: storing a raw ptr into the struct.
    EXPECT_TRUE(irContainsNone(R.IR, {"store ptr @"}))
        << "must not store a raw string ptr into a VarString alloca\n" << R.IR;
}

TEST(IRCodeGen, VarStringAssignChar) {
    // s := c  (char variable) must route through plang_str_from_char.
    auto R = compileAndEmitIR(
        "program p; var s: string(10); c: char;\n"
        "begin c := 'x'; s := c end.\n",
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"plang_str_from_char"}))
        << "expected plang_str_from_char for char→VarString\n" << R.IR;
}

TEST(IRCodeGen, VarStringAssignVarString) {
    // s := t  (VarString to VarString) must use plang_str_assign.
    auto R = compileAndEmitIR(
        "program p; var s, t: string(20); begin t := 'hi'; s := t end.\n",
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"plang_str_assign"}))
        << "expected plang_str_assign for VarString→VarString\n" << R.IR;
}

// --- EP string concatenation ------------------------------------------------

TEST(IRCodeGen, VarStringConcatStrings) {
    // a + b  (both VarString) must call plang_str_concat.
    auto R = compileAndEmitIR(
        "program p; var a, b, u: string(20);\n"
        "begin a := 'foo'; b := 'bar'; u := a + b end.\n",
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"plang_str_concat"}))
        << "expected plang_str_concat for VarString+VarString\n" << R.IR;
    EXPECT_TRUE(irContainsNone(R.IR, {"plang_str_concat_cstr", "plang_str_concat_char"}))
        << "must not use cstr or char variant for VarString+VarString\n" << R.IR;
}

TEST(IRCodeGen, VarStringConcatLiteral) {
    // s + 'literal'  must call plang_str_concat_cstr, not plang_str_concat.
    auto R = compileAndEmitIR(
        "program p; var s, u: string(40);\n"
        "begin s := 'Hello'; u := s + ', World' end.\n",
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"plang_str_concat"}))
        << "expected a plang_str_concat variant\n" << R.IR;
}

// --- Short-circuit boolean operators ----------------------------------------

TEST(IRCodeGen, AndThenProducesPhi) {
    // and_then must generate a phi node (not a plain 'and' instruction).
    // If it were eager, there'd be no basic-block split and no phi.
    auto R = compileAndEmitIR(
        "program p; var a, b, c: boolean;\n"
        "begin c := a and_then b end.\n",
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"phi i1"}))
        << "and_then must produce a phi node for short-circuit\n" << R.IR;
    EXPECT_TRUE(irContainsNone(R.IR, {" and i1 "}))
        << "and_then must not lower to a plain 'and' (eager evaluation)\n" << R.IR;
}

TEST(IRCodeGen, OrElseProducesPhi) {
    auto R = compileAndEmitIR(
        "program p; var a, b, c: boolean;\n"
        "begin c := a or_else b end.\n",
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"phi i1"}))
        << "or_else must produce a phi node for short-circuit\n" << R.IR;
    EXPECT_TRUE(irContainsNone(R.IR, {" or i1 "}))
        << "or_else must not lower to a plain 'or'\n" << R.IR;
}

// --- EP string literal materialization --------------------------------------

TEST(IRCodeGen, EPStringLiteralMaterialisesAsStruct) {
    // In EP mode a multi-char string literal used in an expression (not just
    // passed to writeln) must be materialized as a VarString struct alloca,
    // not left as a bare global char array pointer.  This guards the invariant:
    //   exprIsVarStr(e) → emitExpr(e) returns ptr to struct.
    auto R = compileAndEmitIR(
        "program p; var s: string(20); b: boolean;\n"
        "begin b := s = 'hello' end.\n",   // comparison forces literal materialization
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    // plang_str_from_cstr must be called to wrap the literal in a struct.
    EXPECT_TRUE(irContainsAll(R.IR, {"plang_str_from_cstr", "plang_str_eq"}))
        << "EP string literal must be materialized before comparison\n" << R.IR;
}

TEST(IRCodeGen, ISO7185StringLiteralStaysAsPtr) {
    // In iso7185 mode a string literal passed to writeln stays as a bare ptr
    // (no materialization).  There must be no plang_str_from_cstr call.
    auto R = compileAndEmitIR(
        "program p; begin writeln('hello') end.\n");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsNone(R.IR, {"plang_str_from_cstr"}))
        << "iso7185 literal must not be materialized as a struct\n" << R.IR;
    EXPECT_TRUE(irContainsAll(R.IR, {"plang_writeln_str"}))
        << "iso7185 literal must go through plang_writeln_str\n" << R.IR;
}

// --- Case range labels ------------------------------------------------------

TEST(IRCodeGen, CaseRangeUsesIfElseChain) {
    // A case with lo..hi range labels must emit icmp sge + icmp sle pairs,
    // not a switch instruction (switch cannot encode ranges).
    auto R = compileAndEmitIR(
        "program p; var i: integer;\n"
        "begin case i of 1..5: writeln; 6..10: writeln end end.\n",
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"icmp sge", "icmp sle"}))
        << "range case labels must produce sge/sle comparisons\n" << R.IR;
    EXPECT_TRUE(irContainsNone(R.IR, {"switch i64"}))
        << "range case labels must not use a switch instruction\n" << R.IR;
}

// ---------------------------------------------------------------------------
// Compile-and-run tests for codegen paths previously untested
// ---------------------------------------------------------------------------

// --- Arrays -----------------------------------------------------------------

TEST(CodegenArray, OneBased) {
    // Pascal arrays are 1-based by default; indexing must subtract the lower bound.
    auto R = compileAndRun(
        "program p;\n"
        "var a: array [1..5] of integer; i: integer;\n"
        "begin\n"
        "  for i := 1 to 5 do a[i] := i * 10;\n"
        "  for i := 1 to 5 do writeln(a[i])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n20\n30\n40\n50\n");
}

TEST(CodegenArray, ArbitraryLowerBound) {
    // a[-2..2] has five elements; writing all must not corrupt adjacent memory.
    auto R = compileAndRun(
        "program p;\n"
        "var a: array [-2..2] of integer; i: integer;\n"
        "begin\n"
        "  for i := -2 to 2 do a[i] := i;\n"
        "  for i := -2 to 2 do writeln(a[i])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-2\n-1\n0\n1\n2\n");
}

TEST(CodegenArray, LastElementNoCorruption) {
    // Regression: a[5] in array[1..5] used to overwrite the loop variable.
    auto R = compileAndRun(
        "program p;\n"
        "var a: array [1..5] of integer; i: integer;\n"
        "begin\n"
        "  for i := 1 to 5 do a[i] := i * 10;\n"
        "  i := 99;\n"
        "  writeln(a[5]);\n"
        "  writeln(i)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "50\n99\n");
}

TEST(CodegenArray, CharArray) {
    auto R = compileAndRun(
        "program p;\n"
        "var a: array [1..3] of char;\n"
        "begin\n"
        "  a[1] := 'A'; a[2] := 'B'; a[3] := 'C';\n"
        "  writeln(a[1]); writeln(a[2]); writeln(a[3])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "A\nB\nC\n");
}

// --- Records ----------------------------------------------------------------

TEST(CodegenRecord, DirectFieldAccess) {
    auto R = compileAndRun(
        "program p;\n"
        "type Point = record x, y: integer end;\n"
        "var pt: Point;\n"
        "begin\n"
        "  pt.x := 10; pt.y := 20;\n"
        "  writeln(pt.x); writeln(pt.y)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n20\n");
}

TEST(CodegenRecord, ThreeFields) {
    // Each field must land at a distinct struct element.
    auto R = compileAndRun(
        "program p;\n"
        "type Triplet = record a, b, c: integer end;\n"
        "var rec: Triplet;\n"
        "begin\n"
        "  rec.a := 11; rec.b := 22; rec.c := 33;\n"
        "  writeln(rec.a); writeln(rec.b); writeln(rec.c)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11\n22\n33\n");
}

TEST(CodegenRecord, WithStatement) {
    auto R = compileAndRun(
        "program p;\n"
        "type Pair = record x, y: integer end;\n"
        "var pt: Pair;\n"
        "begin\n"
        "  pt.x := 5; pt.y := 7;\n"
        "  with pt do begin x := x + 1; writeln(x); writeln(y) end\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "6\n7\n");
}

// --- Pointers ---------------------------------------------------------------

TEST(CodegenPointer, NewAndDeref) {
    // Regression: new(p) was allocating only 8 bytes regardless of struct size,
    // and p^.field always loaded/stored from offset 0.
    auto R = compileAndRun(
        "program p;\n"
        "type Rec = record a, b, c: integer end;\n"
        "var r: ^Rec;\n"
        "begin\n"
        "  new(r);\n"
        "  r^.a := 11; r^.b := 22; r^.c := 33;\n"
        "  writeln(r^.a); writeln(r^.b); writeln(r^.c);\n"
        "  dispose(r)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11\n22\n33\n");
}

TEST(CodegenPointer, IntegerPointer) {
    auto R = compileAndRun(
        "program p;\n"
        "var q: ^integer;\n"
        "begin\n"
        "  new(q);\n"
        "  q^ := 42;\n"
        "  writeln(q^);\n"
        "  dispose(q)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(CodegenPointer, IndirectFieldMutation) {
    // Mutate a field through a pointer, then read it back through a second pointer
    // aliasing the same heap object — exercises that field GEPs use the correct
    // struct type when the record is accessed via a pointer variable.
    auto R = compileAndRun(
        "program p;\n"
        "type Pair = record first, second: integer end;\n"
        "var q: ^Pair;\n"
        "begin\n"
        "  new(q);\n"
        "  q^.first := 100; q^.second := 200;\n"
        "  writeln(q^.first);\n"
        "  writeln(q^.second);\n"
        "  dispose(q)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "100\n200\n");
}

TEST(CodegenPointer, SelfReferentialType) {
    // ISO §6.4.4: pointer base-type may be the enclosing record itself.
    // Previously failed with an IR verification error (undefined reference
    // to the unresolved forward type).
    auto R = compileAndRun(
        "program p;\n"
        "type Node = record val: integer; next: ^Node end;\n"
        "var n: ^Node;\n"
        "begin\n"
        "  new(n); n^.val := 42; n^.next := nil;\n"
        "  writeln(n^.val);\n"
        "  dispose(n)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(CodegenPointer, ForwardPointerReference) {
    // ISO §6.4.4: PNode declared before Node in the same type-definition-part.
    auto R = compileAndRun(
        "program p;\n"
        "type PNode = ^Node;\n"
        "     Node  = record val: integer end;\n"
        "var n: PNode;\n"
        "begin\n"
        "  new(n); n^.val := 99; writeln(n^.val); dispose(n)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "99\n");
}

// ---------------------------------------------------------------------------
// Tests derived from the /tmp bug-catching files found during code review.
// These are the exact programs that originally demonstrated each bug.
// ---------------------------------------------------------------------------

TEST(BugReport, ArrayOneBased_Original) {
    // The original reproducer from the code review: array[1..5] must produce
    // 10 20 30 40 50, not 10 20 30 40 <corrupted-loop-var>.
    auto R = compileAndRun(
        "program tarray;\n"
        "var a: array [1..5] of integer; i: integer;\n"
        "begin\n"
        "  for i := 1 to 5 do a[i] := i * 10;\n"
        "  for i := 1 to 5 do writeln(a[i])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n20\n30\n40\n50\n");
}

TEST(BugReport, RecordPointerDirectType) {
    // Original ptr_bug.pas: three fields via ^Rec must be independent.
    auto R = compileAndRun(
        "program tptr;\n"
        "type Rec = record a, b, c: integer end;\n"
        "var p: ^Rec;\n"
        "begin\n"
        "  new(p);\n"
        "  p^.a := 11; p^.b := 22; p^.c := 33;\n"
        "  writeln(p^.a); writeln(p^.b); writeln(p^.c)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11\n22\n33\n");
}

TEST(BugReport, RecordPointerViaTypeAlias) {
    // t_ptr.pas style: pointer accessed through a named type alias (recptr = ^rec).
    // Previously all three fields resolved to offset 0.
    auto R = compileAndRun(
        "program tptr;\n"
        "type\n"
        "  trec = record\n"
        "    a: integer;\n"
        "    b: integer;\n"
        "    c: integer\n"
        "  end;\n"
        "  trecptr = ^trec;\n"
        "var p: trecptr;\n"
        "begin\n"
        "  new(p);\n"
        "  p^.a := 11; p^.b := 22; p^.c := 33;\n"
        "  writeln(p^.a); writeln(p^.b); writeln(p^.c);\n"
        "  dispose(p)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11\n22\n33\n");
}

TEST(BugReport, SelfReferentialLinkedList) {
    // The original ll.pas from the code review: type Node = record ... next: ^Node end.
    // Previously produced an IR verification failure; now compiles and runs.
    auto R = compileAndRun(
        "program p;\n"
        "type ListNode = record val: integer; next: ^ListNode end;\n"
        "var head, tail: ^ListNode;\n"
        "begin\n"
        "  new(head); head^.val := 1; head^.next := nil;\n"
        "  new(tail); tail^.val := 2; tail^.next := nil;\n"
        "  head^.next := tail;\n"
        "  writeln(head^.val);\n"
        "  writeln(head^.next^.val)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1\n2\n");
}

TEST(BugReport, NestedProcedureNoOuterAccess) {
    // Nested procedure that does NOT access outer variables.
    // Previously the call was silently dropped; now it must execute correctly.
    auto R = compileAndRun(
        "program tnested;\n"
        "procedure outer;\n"
        "  var x: integer;\n"
        "  procedure inner;\n"
        "  begin writeln('inner called') end;\n"
        "begin\n"
        "  x := 42;\n"
        "  inner;\n"
        "  writeln(x)\n"
        "end;\n"
        "begin outer end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("inner called"), std::string::npos)
        << "nested call must execute; stdout: " << R.Stdout;
    EXPECT_NE(R.Stdout.find("42"), std::string::npos)
        << "outer must complete; stdout: " << R.Stdout;
}

TEST(BugReport, NestedProcedureWithOuterAccess) {
    // Nested procedure that reads and mutates an outer variable via static link.
    // Previously this caused an IR verification failure (cross-function ref).
    auto R = compileAndRun(
        "program tnested;\n"
        "var g: integer;\n"
        "procedure outer;\n"
        "var x: integer;\n"
        "  procedure inner;\n"
        "  begin x := x + 100; writeln('inner ran, x=', x) end;\n"
        "begin\n"
        "  x := 1; inner; writeln('outer x=', x)\n"
        "end;\n"
        "begin g := 0; outer; writeln('done') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("inner ran"), std::string::npos)
        << "nested proc must execute; stdout: " << R.Stdout;
    EXPECT_NE(R.Stdout.find("outer x=101"), std::string::npos)
        << "outer must see mutation by inner; stdout: " << R.Stdout;
}

TEST(BugReport, FileIO) {
    // t_file.pas: write to a text file, reset, and read back char by char.
    auto R = compileAndRun(
        "program tfile;\n"
        "var f: text; ch: char;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  writeln(f, 'hello');\n"
        "  reset(f);\n"
        "  while not eof(f) do begin read(f, ch); write(ch) end;\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // The space is the line marker: §6.4.3.5 gives f^ that value where a line
    // ends, so reading character by character never yields the newline itself.
    EXPECT_EQ(R.Stdout, "hello \n");
}

// ---------------------------------------------------------------------------
// Internal (unnamed) file regression tests.
//
// rewrite/reset with no file name used to alias the file variable to
// stdout/stdin, so writes landed on the terminal and the read-back saw an
// empty stdin.  BugReport.FileIO above could not catch it: echoing the file
// contents produces the same bytes either way.
// ---------------------------------------------------------------------------

TEST(CodegenFiles, InternalFileRoundTripsValues) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: text; a, b: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  writeln(f, 42);\n"
        "  writeln(f, 77);\n"
        "  reset(f);\n"
        "  read(f, a);\n"
        "  read(f, b);\n"
        "  writeln('a=', a, ' b=', b)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // Exact match: the written values must not also appear on stdout.
    EXPECT_EQ(R.Stdout, "a=42 b=77\n");
}

TEST(CodegenFiles, InternalFileWritesDoNotReachStdout) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: text;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  writeln(f, 'secret');\n"
        "  writeln('done')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "done\n");
    EXPECT_EQ(R.Stdout.find("secret"), std::string::npos)
        << "internal file contents leaked to stdout";
}

TEST(CodegenFiles, InternalFileRewriteTruncates) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: text; a: integer;\n"
        "begin\n"
        "  rewrite(f); writeln(f, 11);\n"
        "  rewrite(f); writeln(f, 22);\n"
        "  reset(f); read(f, a);\n"
        "  writeln('a=', a)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "a=22\n");
}

TEST(CodegenFiles, NamedFileRoundTripsValues) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: text; a: integer;\n"
        "begin\n"
        "  rewrite(f, '/tmp/plang_named_regtest.txt');\n"
        "  writeln(f, 123);\n"
        "  close(f);\n"
        "  reset(f, '/tmp/plang_named_regtest.txt');\n"
        "  read(f, a);\n"
        "  close(f);\n"
        "  writeln('a=', a)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "a=123\n");
    std::remove("/tmp/plang_named_regtest.txt");
}

// ---------------------------------------------------------------------------
// read/readln into string(N).
//
// These used to dispatch to the integer reader, which parsed nothing and
// stored 0 over the string's length field, leaving the variable empty.
// ---------------------------------------------------------------------------

TEST(CodegenStrings, ReadlnIntoString) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(20);\n"
        "begin readln(s); writeln('[', s, ']') end.\n",
        kEP, "hello world\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hello world]\n");
}

TEST(CodegenStrings, ReadlnAdvancesAcrossMixedTypes) {
    auto R = compileAndRun(
        "program p;\n"
        "var s, t: string(20); n: integer;\n"
        "begin\n"
        "  readln(s); readln(t); readln(n);\n"
        "  writeln('s=', s, ' t=', t, ' n=', n)\n"
        "end.\n",
        kEP, "first line\nsecond\n99\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "s=first line t=second n=99\n");
}

TEST(CodegenStrings, ReadlnTruncatesToCapacity) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(5);\n"
        "begin readln(s); writeln('[', s, '] len=', length(s)) end.\n",
        kEP, "abcdefghij\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[abcde] len=5\n");
}

// ---------------------------------------------------------------------------
// readln argument handling.
//
// readln used to call the line-consuming reader once per argument, so every
// variable after the first came from a different line, and a leading file
// variable was ignored entirely for the value reads.
// ---------------------------------------------------------------------------

TEST(CodegenStrings, ReadlnTakesAllArgumentsFromOneLine) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: integer;\n"
        "begin readln(a, b); writeln('a=', a, ' b=', b) end.\n",
        "", "1 2\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "a=1 b=2\n");
}

TEST(CodegenFiles, ReadlnFromFileVariableUsesThatFile) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: text; a, b: integer;\n"
        "begin\n"
        "  rewrite(f); writeln(f, 7, ' ', 8); reset(f);\n"
        "  readln(f, a, b);\n"
        "  writeln('a=', a, ' b=', b)\n"
        "end.\n",
        "", "111 222\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // Values must come from the file, not from the stdin we also supplied.
    EXPECT_EQ(R.Stdout, "a=7 b=8\n");
}

TEST(CodegenFiles, StandardFileParametersResolve) {
    // program p(input, output) used to reach codegen with no storage for
    // either name and abort with an internal error.
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var n: integer;\n"
        "begin\n"
        "  writeln(output, 'via output');\n"
        "  readln(input, n);\n"
        "  writeln('n=', n);\n"
        "  if eof(input) then writeln('eof') else writeln('noeof')\n"
        "end.\n",
        "", "55\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "via output\nn=55\neof\n");
}

TEST(CodegenFiles, RewriteOnOutputKeepsStdout) {
    auto R = compileAndRun(
        "program p(output);\n"
        "begin rewrite(output); writeln(output, 'still stdout') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "still stdout\n");
}

TEST(CodegenIO, ReadCharReadsVerbatimIncludingSpace) {
    // ISO §6.9.2: read(ch) for a char variable reads the next character
    // including spaces; it must NOT skip whitespace as scanf(" %c") would.
    auto R = compileAndRun(
        "program p;\n"
        "var a, b, c: char;\n"
        "begin read(a); read(b); read(c);\n"
        "  writeln(ord(a), ' ', ord(b), ' ', ord(c)) end.\n",
        "", "A B");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // ord('A')=65, ord(' ')=32, ord('B')=66
    EXPECT_EQ(R.Stdout, "65 32 66\n");
}

TEST(CodegenIO, ReadCharReadsNewlineVerbatim) {
    // A newline between two chars must be read as the second character,
    // not silently skipped.
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: char;\n"
        "begin read(a); read(b);\n"
        "  writeln(ord(a), ' ', ord(b)) end.\n",
        "", "X\nY");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // ord('X')=88, ord('\n')=10
    EXPECT_EQ(R.Stdout, "88 10\n");
}

TEST(CodegenStrings, ReadLeavesLineTerminator) {
    // read (unlike readln) must stop at the terminator without consuming it.
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(20);\n"
        "begin\n"
        "  read(s);\n"
        "  if eoln then writeln('eoln') else writeln('noeoln');\n"
        "  writeln('[', s, ']')\n"
        "end.\n",
        kEP, "abc\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "eoln\n[abc]\n");
}

// ---------------------------------------------------------------------------
// Nested procedure regression tests (three shapes that previously produced
// LLVM IR verification failures or silently wrong output).
// ---------------------------------------------------------------------------

TEST(CodegenNested, ThreeLevels) {
    // L3 reaching L1's variable via two levels of static link.
    auto R = compileAndRun(
        "program p;\n"
        "procedure L1;\n"
        "  var a: integer;\n"
        "  procedure L2;\n"
        "    procedure L3;\n"
        "    begin a := 99; writeln('L3 set a=', a) end;\n"
        "  begin L3 end;\n"
        "begin\n"
        "  a := 1;\n"
        "  L2;\n"
        "  writeln('L1 a=', a)\n"
        "end;\n"
        "begin L1 end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("L3 set a=99"), std::string::npos) << R.Stdout;
    EXPECT_NE(R.Stdout.find("L1 a=99"),     std::string::npos) << R.Stdout;
}

TEST(CodegenNested, RecursionInsideNestedProc) {
    // A nested procedure calling itself recursively (accesses outer depth var).
    auto R = compileAndRun(
        "program p;\n"
        "procedure outer;\n"
        "  var depth: integer;\n"
        "  procedure inner;\n"
        "  begin\n"
        "    depth := depth + 1;\n"
        "    writeln(depth);\n"
        "    if depth < 3 then inner\n"
        "  end;\n"
        "begin\n"
        "  depth := 0;\n"
        "  inner\n"
        "end;\n"
        "begin outer end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("1"), std::string::npos) << R.Stdout;
    EXPECT_NE(R.Stdout.find("3"), std::string::npos) << R.Stdout;
}

TEST(CodegenNested, SiblingCallsViaOuter) {
    // Two sibling nested procedures sharing an outer variable (array).
    auto R = compileAndRun(
        "program p;\n"
        "procedure outer;\n"
        "  var arr: array [1..3] of integer;\n"
        "  procedure fill;\n"
        "  var i: integer;\n"
        "  begin for i := 1 to 3 do arr[i] := i * 10 end;\n"
        "  procedure show;\n"
        "  var i: integer;\n"
        "  begin for i := 1 to 3 do writeln(arr[i]) end;\n"
        "begin\n"
        "  fill;\n"
        "  show\n"
        "end;\n"
        "begin outer end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n20\n30\n");
}

// ---------------------------------------------------------------------------
// Forward declaration regression test.
// ---------------------------------------------------------------------------

TEST(Regression, ForwardDeclarationSignatureMismatch) {
    // A mismatch between forward-declaration and implementation parameter types
    // must be caught by Sema, not escape to produce an IR verification failure.
    auto R = compileAndRun(
        "program p;\n"
        "procedure foo(x: integer); forward;\n"
        "procedure foo(x: real);\n"
        "begin writeln(x) end;\n"
        "begin foo(1) end.\n");
    EXPECT_NE(R.ExitCode, 0)
        << "forward-decl param type mismatch must be rejected";
}

// ---------------------------------------------------------------------------
// File variable struct-size regression test.
// ---------------------------------------------------------------------------

TEST(Regression, FileVariableDoesNotCorruptAdjacentStack) {
    // PascalFile { FILE*, int } is 16 bytes; codegen previously allocated only
    // 8 (alloca ptr), so plang_reset writing F->Buf at offset 8 corrupted the
    // adjacent stack slot.  Guard variables on either side must be untouched.
    auto R = compileAndRun(
        "program p;\n"
        "var guard1: integer; f: text; guard2: integer;\n"
        "begin\n"
        "  guard1 := 111111; guard2 := 222222;\n"
        "  rewrite(f);\n"
        "  writeln(f, 'hello');\n"
        "  reset(f);\n"
        "  writeln(guard1);\n"
        "  writeln(guard2)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("111111"), std::string::npos)
        << "guard1 corrupted; stdout: " << R.Stdout;
    EXPECT_NE(R.Stdout.find("222222"), std::string::npos)
        << "guard2 corrupted; stdout: " << R.Stdout;
}

// ---------------------------------------------------------------------------
// Program-level variables must have exactly one storage location.  main() used
// to alloca its own copy of every global, so a procedure writing a global and
// the program body reading it referred to different memory.
// ---------------------------------------------------------------------------

TEST(CodegenGlobals, ProcedureWriteIsVisibleToProgramBody) {
    auto R = compileAndRun(
        "program p;\n"
        "var g: integer;\n"
        "procedure setIt;\n"
        "begin g := 42 end;\n"
        "begin\n"
        "  g := 0;\n"
        "  setIt;\n"
        "  writeln('g=', g)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("g=42"), std::string::npos)
        << "global write from procedure lost; stdout: " << R.Stdout;
}

TEST(CodegenGlobals, ProgramBodyWriteIsVisibleToProcedure) {
    auto R = compileAndRun(
        "program p;\n"
        "var g: integer;\n"
        "procedure showIt;\n"
        "begin writeln('seen=', g) end;\n"
        "begin\n"
        "  g := 7;\n"
        "  showIt\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("seen=7"), std::string::npos)
        << "program-body write not seen by procedure; stdout: " << R.Stdout;
}

TEST(CodegenGlobals, NestedProcedureSharesGlobalWithProgramBody) {
    auto R = compileAndRun(
        "program p;\n"
        "var g: integer;\n"
        "procedure outer;\n"
        "  procedure inner;\n"
        "  begin g := g + 100 end;\n"
        "begin inner end;\n"
        "begin\n"
        "  g := 1;\n"
        "  outer;\n"
        "  writeln('g=', g)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("g=101"), std::string::npos)
        << "global not shared with nested procedure; stdout: " << R.Stdout;
}

TEST(CodegenGlobals, StringGlobalIsInitializedAndShared) {
    // main no longer allocas globals, so plang_str_init must still run against
    // the global storage for string(N) program variables.
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(20); n: integer;\n"
        "procedure fill;\n"
        "begin s := 'hello'; n := length(s) end;\n"
        "begin\n"
        "  n := 0;\n"
        "  fill;\n"
        "  writeln('s=', s);\n"
        "  writeln('n=', n)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("s=hello"), std::string::npos) << R.Stdout;
    EXPECT_NE(R.Stdout.find("n=5"),     std::string::npos) << R.Stdout;
}

// ---------------------------------------------------------------------------
// ISO set operators.
//
// The union/difference/intersection lowering and the runtime helpers both
// existed, but Sema rejected the operators before codegen ever ran, and the
// inclusion relations fell through to a signed compare on the bitmask.
// ---------------------------------------------------------------------------

TEST(CodegenSets, ArithmeticOperators) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b, c: set of 1..10; i: integer;\n"
        "procedure show(s: set of 1..10);\n"
        "  var j: integer;\n"
        "begin\n"
        "  for j := 1 to 10 do if j in s then write(j);\n"
        "  writeln\n"
        "end;\n"
        "begin\n"
        "  a := [1,2,3,4]; b := [3,4,5,6];\n"
        "  c := a + b; show(c);\n"
        "  c := a * b; show(c);\n"
        "  c := a - b; show(c)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "123456\n34\n12\n");
}

TEST(CodegenSets, InclusionRelations) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: set of 1..10;\n"
        "begin\n"
        "  a := [1,2,3]; b := [1,2,3,4,5];\n"
        "  writeln(a <= b, ' ', b <= a, ' ', b >= a, ' ', a = a, ' ', a <> b)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false true true true\n");
}

// Sets hold one bit per ordinal across the full base-type width.  These cover
// ordinals above 63, which an earlier uint64 representation dropped silently.

TEST(CodegenSets, CharacterSetMembership) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of char;\n"
        "begin\n"
        "  s := ['a', 'b', 'z'];\n"
        "  writeln('a' in s, ' ', 'z' in s, ' ', 'q' in s)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true true false\n");
}

TEST(CodegenSets, CharacterRangeIdiom) {
    auto R = compileAndRun(
        "program p;\n"
        "var ch: char; n: integer;\n"
        "begin\n"
        "  n := 0;\n"
        "  for ch := 'a' to 'z' do\n"
        "    if ch in ['a', 'e', 'i', 'o', 'u'] then n := n + 1;\n"
        "  writeln(n)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n");
}

TEST(CodegenSets, SubrangeRangeConstructorAboveWordWidth) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of 0..255; i, n: integer;\n"
        "begin\n"
        "  s := [10..20, 200..210];\n"
        "  n := 0;\n"
        "  for i := 0 to 255 do if i in s then n := n + 1;\n"
        "  writeln(n, ' ', 205 in s, ' ', 100 in s)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "22 true false\n");
}

TEST(CodegenSets, OperatorsAboveWordWidth) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: set of 0..255;\n"
        "begin\n"
        "  a := [64, 100, 200]; b := [100, 200, 255];\n"
        "  writeln(card(a + b), ' ', card(a * b), ' ', card(a - b));\n"
        "  writeln(64 in (a + b), ' ', 64 in (a * b), ' ', 255 in (a - b))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4 2 1\ntrue false false\n");
}

TEST(CodegenSets, CardinalityAtFullWidth) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of char;\n"
        "begin\n"
        "  s := [chr(0)..chr(255)];\n"
        "  writeln(card(s))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "256\n");
}

// The loop variable takes the set's element type, so a set of char yields
// characters rather than their ordinals.
TEST(CodegenSets, ForInOverCharacterSet) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of char; c: char;\n"
        "begin\n"
        "  s := ['a', 'b', 'z'];\n"
        "  for c in s do write(c);\n"
        "  writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "abz\n");
}

TEST(CodegenSets, ForInAboveWordWidth) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of 0..255; i: integer;\n"
        "begin\n"
        "  s := [3, 70, 200];\n"
        "  for i in s do write(i, ' ');\n"
        "  writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3 70 200 \n");
}

TEST(CodegenSets, SymmetricDifferenceAboveWordWidth) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b, c: set of 0..255; i: integer;\n"
        "begin\n"
        "  a := [1, 100]; b := [100, 250];\n"
        "  c := a >< b;\n"
        "  for i in c do write(i, ' ');\n"
        "  writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 250 \n");
}

// Sets cross procedure boundaries by value at their full width.
TEST(CodegenSets, PassedAsParameterAboveWordWidth) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of char;\n"
        "function count(t: set of char): integer;\n"
        "begin count := card(t) end;\n"
        "begin\n"
        "  s := ['a'..'e', 'x'];\n"
        "  writeln(count(s))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "6\n");
}

TEST(SemaSets, MixedOperandsRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of 1..10; i: integer;\n"
        "begin s := [1]; i := 3; i := s + i end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("cannot mix set and non-set"), std::string::npos)
        << R.Stderr;
}

TEST(SemaSets, OrderingOperatorsRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of 1..10;\n"
        "begin s := [1]; if s < s then writeln('x') end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("not defined for sets"), std::string::npos) << R.Stderr;
}

// ISO §6.8.2.2: a parameterless function-identifier in an expression is a
// call.  Codegen used to treat the bare name as an out-of-scope variable and
// emit a reference to a global "g_pick" that nothing defines, so the program
// failed at link time rather than running.
TEST(CodegenProcs, ParameterlessFunctionIdentifierIsACall) {
    auto R = compileAndRun(
        "program p;\n"
        "var g: integer;\n"
        "function pick: integer;\n"
        "begin pick := 7 end;\n"
        "procedure show(v: integer); begin writeln(v) end;\n"
        "begin g := pick; writeln(g); show(pick); writeln(pick + 1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n7\n8\n");
}

// The call still needs the static link, so it cannot bypass the call path.
TEST(CodegenProcs, ParameterlessNestedFunctionSeesEnclosingLocals) {
    auto R = compileAndRun(
        "program p;\n"
        "function outer(n: integer): integer;\n"
        "  var acc: integer;\n"
        "  function bump: integer;\n"
        "  begin bump := acc + 1 end;\n"
        "begin acc := n; outer := bump + bump end;\n"
        "begin writeln(outer(10)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "22\n");
}

// ---------------------------------------------------------------------------
// Sets over a base type that reaches below zero (ISO §6.4.3.4).  Bit 0 of the
// mask stands for the base type's lower bound rather than for ordinal 0, so
// every ordinal has to be rebased on the way in and back on the way out.
// ---------------------------------------------------------------------------

TEST(CodegenSets, NegativeBaseMembershipAndCardinality) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of -5..10;\n"
        "begin\n"
        "  s := [-5, -1, 0, 3];\n"
        "  writeln(card(s), ' ', -5 in s, ' ', -1 in s, ' ', -2 in s, ' ', 3 in s)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4 true true false true\n");
}

TEST(CodegenSets, NegativeBaseRangeConstructor) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of -5..10; i: integer;\n"
        "begin\n"
        "  s := [-3 .. 1];\n"
        "  for i in s do write(i, ' ');\n"
        "  writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-3 -2 -1 0 1 \n");
}

TEST(CodegenSets, NegativeBaseOperators) {
    auto R = compileAndRun(
        "program p;\n"
        "type r = -8..8; s = set of r;\n"
        "var a, b: s; i: r;\n"
        "begin\n"
        "  a := [-8, -1, 2]; b := [-1, 2, 8];\n"
        "  for i in a + b do write(i, ' '); writeln;\n"
        "  for i in a * b do write(i, ' '); writeln;\n"
        "  for i in a - b do write(i, ' '); writeln;\n"
        "  for i in a >< b do write(i, ' '); writeln;\n"
        "  writeln(a <= a + b, ' ', a = b)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-8 -1 2 8 \n-1 2 \n-8 \n-8 8 \ntrue false\n");
}

// The window is 256 ordinals wide wherever it starts, so a base type ending
// below zero is as representable as one starting at zero.
TEST(CodegenSets, NegativeBaseAtFullWidth) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of -256..-1;\n"
        "begin\n"
        "  s := [-256 .. -1];\n"
        "  writeln(card(s), ' ', -256 in s, ' ', -1 in s)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "256 true true\n");
}

TEST(SemaSets, BaseSpanningMoreThanTheLimitRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of -300..0;\n"
        "begin s := [] end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("exceeds the 256-element limit"), std::string::npos)
        << R.Stderr;
}

// ---------------------------------------------------------------------------
// A set-constructor has no type of its own; the context supplies one, which is
// what tells codegen where to put ordinal -1.
// ---------------------------------------------------------------------------

TEST(CodegenSets, ConstructorTakesItsTypeFromTheContext) {
    auto R = compileAndRun(
        "program p;\n"
        "type r = -5..10; s = set of r;\n"
        "var g: s; i: r;\n"
        "function pick(n: integer): s;\n"
        "begin pick := [-4, 6] end;\n"
        "procedure show(v: s);\n"
        "  var k: r;\n"
        "begin for k in v do write(k, ' '); writeln end;\n"
        "begin\n"
        "  show([-2, 0, 4]);\n"           // argument
        "  show(pick(0));\n"              // function result
        "  g := [-1] + [3];\n"            // constructor on both sides of '+'
        "  show(g);\n"
        "  show(g + [-5]);\n"             // one loose operand, one typed
        "  i := -1;\n"
        "  writeln(i in [-5 .. -1])\n"    // 'in' takes its window from i
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-2 0 4 \n-4 6 \n-1 3 \n-5 -1 3 \ntrue\n");
}

// With no context at all the constructor still has to hold its own elements,
// so it reads a window off them.
TEST(CodegenSets, ConstructorWithoutContextSpansItsOwnElements) {
    auto R = compileAndRun(
        "program p;\n"
        "begin\n"
        "  writeln(card([-4, -2, 7]), ' ', -4 in [-4, -2, 7], ' ', -3 in [-4, -2, 7])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3 true false\n");
}

// Set types used to be interned by their element type's *name*, and every
// anonymous subrange is named "subrange", so these two were the same type.
// They are two, and being two they are based at different ordinals: bit 0 of
// the first stands for -5 and bit 0 of the second for 0.
//
// ISO §6.4.5 c) makes them compatible all the same, their base types being
// subranges of the one host type, so a value crosses between them and has to
// be moved five places on the way.  It used to be carried across unmoved, and
// {1} arrived as {-4}.
TEST(SemaSets, AValueCrossingSetWindowsKeepsItsMembers) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type a = set of -5..10; b = set of 0..10;\n"
        "var x: a; y: b; i: integer;\n"
        "begin\n"
        "  y := [1, 3];\n"
        "  x := y;\n"
        "  for i := -5 to 10 do if i in x then write(i:3);\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "  1  3\n");
}

// The operands of a set operator meet in one window too, and a comparison
// takes the lower of the two origins, where neither operand loses a bit.
TEST(SemaSets, OperandsOfDifferentWindowsMeetInOne) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type a = set of -5..10; b = set of 0..10;\n"
        "var x: a; y: b; i: integer;\n"
        "begin\n"
        "  x := [-3]; y := [1, 3];\n"
        "  for i := -5 to 10 do if i in (x + y) then write(i:3);\n"
        "  writeln;\n"
        "  if x = y then writeln('same') else writeln('differ');\n"
        "  x := [1]; y := [1];\n"
        "  if x = y then writeln('same') else writeln('differ')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, " -3  1  3\ndiffer\nsame\n");
}

// A value parameter is a window crossing as much as an assignment is, and the
// callee's window is the one recorded for its parameter.
TEST(SemaSets, ASetArgumentArrivesInTheParametersWindow) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type a = set of -5..10; b = set of 0..10;\n"
        "var y: b;\n"
        "procedure show(s: a);\n"
        "var j: integer;\n"
        "begin\n"
        "  for j := -5 to 10 do if j in s then write(j:3);\n"
        "  writeln\n"
        "end;\n"
        "begin y := [1, 3]; show(y) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "  1  3\n");
}

// ---------------------------------------------------------------------------
// Constant bounds
//
// constBound used to answer with the expression's own address when it could
// not fold, which reads as an ordinary bound at any call site that forgets to
// test for it.  Two forgot, so `array[1..n]` over a variable `n` was accepted,
// lowered to [0 x i64], and segfaulted on the first subscript — index checks
// could not fire because the bounds they compare against were pointer bits.
// An inverted constant range reached the same [0 x i64] by a different route.
// ---------------------------------------------------------------------------

TEST(ConstBound, ArrayOverAVariableBoundIsRejected) {
    auto R = compileAndEmitIR(
        "program p;\n"
        "var n: integer;\n"
        "    a: array[1..n] of integer;\n"
        "begin end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("not a constant expression"), std::string::npos)
        << R.Stderr;
    // The whole point: no zero-element object escapes to be indexed.
    EXPECT_EQ(R.IR.find("[0 x "), std::string::npos) << R.IR;
}

TEST(ConstBound, BothVariableBoundsAreReported) {
    auto R = compileAndEmitIR(
        "program p;\n"
        "var lo, hi: integer;\n"
        "    a: array[lo..hi] of integer;\n"
        "begin end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("lower bound"), std::string::npos) << R.Stderr;
    EXPECT_NE(R.Stderr.find("upper bound"), std::string::npos) << R.Stderr;
}

TEST(ConstBound, ArrayBoundDiagnosticSuggestsASchemaType) {
    auto R = compileAndEmitIR(
        "program p;\nvar n: integer;\n"
        "    a: array[1..n] of integer;\nbegin end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("schema type"), std::string::npos) << R.Stderr;
}

TEST(ConstBound, SubrangeOverAVariableBoundIsRejected) {
    // A type can only see a variable from an enclosing scope, so the
    // declaration has to be nested to be written at all.
    auto R = compileAndEmitIR(
        "program p;\n"
        "var n: integer;\n"
        "procedure q;\n"
        "type s = 1..n;\n"
        "var x: s;\n"
        "begin end;\n"
        "begin end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("upper bound of subrange"), std::string::npos)
        << R.Stderr;
}

TEST(ConstBound, InvertedConstantRangeIsRejected) {
    auto R = compileAndEmitIR(
        "program p;\nvar a: array[5..1] of integer;\nbegin end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("exceeds upper bound"), std::string::npos)
        << R.Stderr;
    EXPECT_EQ(R.IR.find("[0 x "), std::string::npos) << R.IR;
}

TEST(ConstBound, InvertedBoundsReadBackAsWritten) {
    auto Chars = compileAndEmitIR(
        "program p;\nvar a: array['z'..'a'] of integer;\nbegin end.\n");
    EXPECT_FALSE(Chars.Ok);
    EXPECT_NE(Chars.Stderr.find("'z' exceeds upper bound 'a'"),
              std::string::npos) << Chars.Stderr;

    auto Enums = compileAndEmitIR(
        "program p;\ntype c = (r, g, b);\n"
        "var a: array[b..r] of integer;\nbegin end.\n");
    EXPECT_FALSE(Enums.Ok);
    EXPECT_NE(Enums.Stderr.find("b exceeds upper bound r"), std::string::npos)
        << Enums.Stderr;
}

TEST(ConstBound, ConstantFoldedBoundsStillWork) {
    auto R = compileAndRun(
        "program p;\n"
        "const k = 4;\n"
        "var a: array[1..k*2] of integer; i: integer;\n"
        "begin for i := 1 to 8 do a[i] := i * i; writeln(a[8]) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "64\n");
}

TEST(ConstBound, NegativeAndCharAndEnumBoundsStillWork) {
    auto R = compileAndRun(
        "program p;\n"
        "type c = (red, green, blue);\n"
        "var a: array[-3..-1] of integer;\n"
        "    b: array['a'..'e'] of integer;\n"
        "    d: array[red..blue] of integer;\n"
        "begin\n"
        "  a[-2] := 7; b['c'] := 8; d[green] := 9;\n"
        "  writeln(a[-2] + b['c'] + d[green])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "24\n");
}

TEST(ConstBound, SchemaTypesStillTakeARunTimeLength) {
    // The diagnostic points here, so this had better keep working.
    auto R = compileAndRun(
        "program p;\n"
        "type vec(n: integer) = array[1..n] of integer;\n"
        "var v: vec(4); i: integer;\n"
        "begin for i := 1 to 4 do v[i] := i * 10; writeln(v[4]) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "40\n");
}

TEST(ConstBound, StringCapacityMustBeConstantAndPositive) {
    auto Var = compileAndEmitIR(
        "program p;\nvar n: integer; s: string(n);\nbegin end.\n",
        "-std=iso10206");
    EXPECT_FALSE(Var.Ok);
    EXPECT_NE(Var.Stderr.find("constant expression"), std::string::npos)
        << Var.Stderr;

    // This silently became string(255), so the declared capacity meant nothing.
    auto Zero = compileAndEmitIR(
        "program p;\nvar s: string(0);\nbegin end.\n", "-std=iso10206");
    EXPECT_FALSE(Zero.Ok);
    EXPECT_NE(Zero.Stderr.find("must be positive"), std::string::npos)
        << Zero.Stderr;
}

TEST(ConstBound, EmptySetLiteralRangeIsStillLegal) {
    // ISO §6.7.1: in a set, a range whose first value exceeds its second
    // denotes no values.  Only *types* need an ascending range.
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of 1..10;\n"
        "begin s := [5..1]; if s = [] then writeln('empty') "
        "else writeln('not empty') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "empty\n");
}

// ---------------------------------------------------------------------------
// Optimization pipeline
//
// -O used to reach only llc, so the LLVM middle end never ran and every
// variable stayed a stack slot.  Turning it on also made the missing data
// layout matter: without one, LLVM's defaults align i64 to four bytes and lay
// records out differently from the machine the backend targets, so the two
// disagreed about where a field was.
// ---------------------------------------------------------------------------

TEST(CodegenOpt, ModuleCarriesTheTargetDataLayout) {
    auto R = compileAndEmitIR("program p;\nbegin end.\n");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_NE(R.IR.find("target datalayout"), std::string::npos) << R.IR;
    // The default layout omits this, and that is exactly the difference that
    // moved a record field out from under the backend.
    EXPECT_NE(R.IR.find("i64:64"), std::string::npos) << R.IR;
}

TEST(CodegenOpt, OptimizationPromotesLocalsOutOfMemory) {
    const char *Src =
        "program p;\n"
        "function work(n: integer): integer;\n"
        "var i, acc: integer;\n"
        "begin acc := 0; for i := 1 to n do acc := acc + i; work := acc end;\n"
        "begin writeln(work(10)) end.\n";

    auto Unopt = compileAndEmitIR(Src, "-O0");
    auto Opt   = compileAndEmitIR(Src, "-O2");
    ASSERT_TRUE(Unopt.Ok) << Unopt.Stderr;
    ASSERT_TRUE(Opt.Ok) << Opt.Stderr;

    auto count = [](const std::string &S, const std::string &Needle) {
        size_t N = 0;
        for (size_t P = S.find(Needle); P != std::string::npos;
             P = S.find(Needle, P + 1)) ++N;
        return N;
    };
    EXPECT_GT(count(Unopt.IR, "alloca"), 0u);
    EXPECT_EQ(count(Opt.IR, "alloca"), 0u) << Opt.IR;
}

TEST(CodegenOpt, OptimizedRecordFieldsSurviveARuntimeRoundTrip) {
    // The layout disagreement showed up here first: the runtime filled a
    // TimeStamp and the optimized read of a later field came back wrong.
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin\n"
        "  GetTimeStamp(t);\n"
        "  if t.DateValid then writeln('date ok') else writeln('date bad');\n"
        "  if t.TimeValid then writeln('time ok') else writeln('time bad')\n"
        "end.\n",
        "-std=iso10206 -O2");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "date ok\ntime ok\n");
}

TEST(CodegenOpt, EveryLevelComputesTheSameAnswer) {
    const char *Src =
        "program p;\n"
        "var i, j, acc: integer;\n"
        "begin\n"
        "  acc := 0;\n"
        "  for i := 1 to 40 do for j := 1 to 40 do acc := acc + (i mod 7) * (j mod 3);\n"
        "  writeln(acc)\n"
        "end.\n";
    const std::string Expected = compileAndRun(Src, "-O0").Stdout;
    ASSERT_FALSE(Expected.empty());
    for (const char *Flag : {"-O1", "-O2", "-O3"}) {
        auto R = compileAndRun(Src, Flag);
        EXPECT_EQ(R.ExitCode, 0) << Flag << ": " << R.Stderr;
        EXPECT_EQ(R.Stdout, Expected) << Flag;
    }
}

// ---------------------------------------------------------------------------
// Type identity
//
// Structural types are interned so that two spellings of one type are one
// object, and nominal types (enumerations, records) are one object per
// declaration.  Both halves used to be keyed on the display name, which is not
// an identity: every anonymous record is described the same way, so records
// were all mutually compatible, and pointers to distinct enumerations were the
// same type.  Naming a type hid all of it, which is why these tests spell the
// types out.
// ---------------------------------------------------------------------------

TEST(SemaTypeIdentity, TextIsOneTypeEverywhere) {
    // ISO §6.4.3.5: text is predefined, so a text argument matches a text
    // parameter.  This used to fail with "expected 'text', got 'text'".
    auto R = compileAndRun(
        "program p;\n"
        "var f: text;\n"
        "procedure w(var t: text);\n"
        "begin writeln(t, 'ok') end;\n"
        "begin rewrite(f); w(f) end.\n");
    EXPECT_EQ(R.ExitCode, 0) << R.Stderr;
}

TEST(SemaTypeIdentity, StructuralFileTypesMatchAcrossSpellings) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "procedure w(var g: file of integer);\n"
        "begin rewrite(g); write(g, 7) end;\n"
        "begin w(f) end.\n");
    EXPECT_EQ(R.ExitCode, 0) << R.Stderr;
}

TEST(SemaTypeIdentity, StringCapacityTypesMatchAcrossSpellings) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(20);\n"
        "procedure w(var t: string(20));\n"
        "begin t := 'ok' end;\n"
        "begin w(s); writeln(s) end.\n",
        "-std=iso10206");
    EXPECT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "ok\n");
}

TEST(SemaTypeIdentity, PointersToDistinctEnumsAreDistinct) {
    // Both pointees were described "(enum)", so the pointer cache returned one
    // type for both and this assignment was accepted.
    auto R = compileAndRun(
        "program p;\n"
        "type c1 = (red, green); c2 = (blue, white);\n"
        "var a: ^c1; b: ^c2;\n"
        "begin new(a); new(b); a := b end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("'^c1'"), std::string::npos) << R.Stderr;
    EXPECT_NE(R.Stderr.find("'^c2'"), std::string::npos) << R.Stderr;
}

TEST(SemaTypeIdentity, PointersToDistinctAnonRecordsAreDistinct) {
    auto R = compileAndRun(
        "program p;\n"
        "type p1 = ^record a: integer end;\n"
        "     p2 = ^record b, c, d: integer end;\n"
        "var x: p1; y: p2;\n"
        "begin new(x); new(y); x := y end.\n");
    EXPECT_NE(R.ExitCode, 0) << R.Stderr;
}

TEST(SemaTypeIdentity, DistinctEnumTypesAreNotCompatible) {
    // Every enumeration was named "(enum)", so every one was compatible with
    // every other (ISO §6.4.2.3 makes each definition a distinct type).
    auto R = compileAndRun(
        "program p;\n"
        "type c1 = (aa, bb); c2 = (dd, ee);\n"
        "var u: c1; v: c2;\n"
        "begin u := aa; v := dd; u := v end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("'c1'"), std::string::npos) << R.Stderr;
    EXPECT_NE(R.Stderr.find("'c2'"), std::string::npos) << R.Stderr;
}

TEST(SemaTypeIdentity, DistinctRecordTypesAreNotCompatible) {
    // Likewise every record was named "(record)", so records of entirely
    // different shapes were assignable to each other.
    auto R = compileAndRun(
        "program p;\n"
        "type a = record x: integer end;\n"
        "     b = record y: real; z: char end;\n"
        "var s: a; t: b;\n"
        "begin s := t end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("'a'"), std::string::npos) << R.Stderr;
    EXPECT_NE(R.Stderr.find("'b'"), std::string::npos) << R.Stderr;
}

TEST(SemaTypeIdentity, SubrangesOverDistinctEnumsAreDistinct) {
    // The subrange cache keyed on the base's name, and both bases were
    // "(enum)" with the same ordinal bounds, so the two collided.
    auto R = compileAndRun(
        "program p;\n"
        "type c1 = (aa, bb, cc); c2 = (dd, ee, ff);\n"
        "     s1 = aa..cc; s2 = dd..ff;\n"
        "var u: s1; v: s2;\n"
        "begin u := aa; v := dd; u := v end.\n");
    EXPECT_NE(R.ExitCode, 0) << R.Stderr;
}

TEST(SemaTypeIdentity, SameRecordTypeStillAssignable) {
    auto R = compileAndRun(
        "program p;\n"
        "type a = record x: integer end;\n"
        "var s, t: a;\n"
        "begin t.x := 9; s := t; writeln(s.x) end.\n");
    EXPECT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "9\n");
}

TEST(SemaTypeIdentity, TypeAliasKeepsTheAliasedTypesIdentity) {
    // `b = a` must not rename a's type, and a value of one must still pass as
    // the other, since they are the same type.
    auto R = compileAndRun(
        "program p;\n"
        "type a = record x: integer end; b = a;\n"
        "var s: a; t: b;\n"
        "procedure w(var q: a);\n"
        "begin q.x := 4 end;\n"
        "begin w(t); s := t; writeln(s.x) end.\n");
    EXPECT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4\n");
}

TEST(SemaTypeIdentity, ForwardPointerIsTheSameTypeAsALaterMention) {
    // A forward `^node` is interned under a placeholder; if resolving it did
    // not re-file it, the `^node` in the var declaration would be a second,
    // incompatible pointer type.
    auto R = compileAndRun(
        "program p;\n"
        "type pnode = ^node;\n"
        "     node = record v: integer; next: ^node end;\n"
        "var head: pnode; other: ^node;\n"
        "begin\n"
        "  new(head); head^.v := 3; head^.next := nil;\n"
        "  other := head; writeln(other^.v)\n"
        "end.\n");
    EXPECT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n");
}

TEST(SemaTypeIdentity, InlineRecordParamExplainsWhyItDoesNotMatch) {
    // ISO §6.6.3.3 wants the same type, and two spellings of an anonymous
    // record are two types.  The point of the test is the explanation: the
    // message used to be "expected 'record (a)', got 'record (a)'".
    auto R = compileAndRun(
        "program p;\n"
        "var r: record a: integer end;\n"
        "procedure w(var q: record a: integer end);\n"
        "begin q.a := 5 end;\n"
        "begin w(r) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("two distinct types"), std::string::npos) << R.Stderr;
}

TEST(SemaDiagnostics, OperatorsUseSourceSpelling) {
    // Diagnostics used to print the TokenKind enumerator ("Plus").
    auto R = compileAndRun(
        "program p;\n"
        "var b: boolean; i: integer;\n"
        "begin b := true; i := 1; i := b + i end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("operator '+'"), std::string::npos) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("operator 'Plus'"), std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// ISO runtime checks.
//
// Exit status 70 is what the runtime reporters use; see plang_sys.cpp.
// ---------------------------------------------------------------------------

TEST(RuntimeChecks, DivisionByZeroReports) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: integer;\n"
        "begin a := 10; b := 0; writeln('before'); writeln(a div b) end.\n");
    EXPECT_EQ(R.ExitCode, 70);
    // Output preceding the fault must still be visible.
    EXPECT_EQ(R.Stdout, "before\n");
    EXPECT_NE(R.Stderr.find("div by zero"), std::string::npos) << R.Stderr;
}

TEST(RuntimeChecks, ModByZeroReports) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: integer;\n"
        "begin a := 10; b := 0; writeln(a mod b) end.\n");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("non-positive divisor 0"), std::string::npos)
        << R.Stderr;
}

TEST(RuntimeChecks, ModByNegativeReports) {
    // ISO §6.7.2.2 defines mod only for a positive divisor; a negative one has
    // no defined result, so accepting it would mean inventing one.
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: integer;\n"
        "begin a := 10; b := -3; writeln(a mod b) end.\n");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("non-positive divisor -3"), std::string::npos)
        << R.Stderr;
}

TEST(RuntimeChecks, UnmatchedCaseReports) {
    auto R = compileAndRun(
        "program p;\n"
        "var i: integer;\n"
        "begin i := 99; case i of 1: writeln('one'); 2: writeln('two') end end.\n");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("case value 99 matches no label"), std::string::npos)
        << R.Stderr;
}

TEST(RuntimeChecks, MatchedCaseStillRuns) {
    auto R = compileAndRun(
        "program p;\n"
        "var i: integer;\n"
        "begin i := 2; case i of 1: writeln('one'); 2: writeln('two') end end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "two\n");
}

TEST(RuntimeChecks, ArrayIndexOutOfBoundsReports) {
    auto R = compileAndRun(
        "program p;\n"
        "var a: array[1..5] of integer; i: integer;\n"
        "begin\n"
        "  for i := 1 to 5 do a[i] := i;\n"
        "  i := 9; writeln(a[i])\n"
        "end.\n");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("array index 9 out of bounds 1..5"), std::string::npos)
        << R.Stderr;
}

TEST(RuntimeChecks, InBoundsIndexingUnaffected) {
    auto R = compileAndRun(
        "program p;\n"
        "var a: array[1..5] of integer; i: integer;\n"
        "begin\n"
        "  for i := 1 to 5 do a[i] := i * 10;\n"
        "  writeln(a[1] + a[5])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "60\n");
}

TEST(RuntimeChecks, SubrangeAssignmentReports) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: 1..10; i: integer;\n"
        "begin i := 500; s := i; writeln(s) end.\n");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("value 500 out of range 1..10"), std::string::npos)
        << R.Stderr;
}

TEST(RuntimeChecks, InRangeSubrangeAssignmentUnaffected) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: 1..10; i: integer;\n"
        "begin i := 7; s := i; writeln(s) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n");
}

TEST(RuntimeChecks, NoRangeChecksFlagOmitsThem) {
    // Division by zero stays checked; only the bounds checks are optional.
    auto R = compileAndRun(
        "program p;\n"
        "var s: 1..10; i: integer;\n"
        "begin i := 500; s := i; writeln('no check') end.\n",
        "-fno-range-checks");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "no check\n");
}

TEST(RuntimeChecks, DivisionByZeroCheckedEvenWithoutRangeChecks) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: integer;\n"
        "begin a := 1; b := 0; writeln(a div b) end.\n",
        "-fno-range-checks");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("div by zero"), std::string::npos) << R.Stderr;
}

TEST(RuntimeChecks, NilDerefReports) {
    auto R = compileAndRun(
        "program p;\n"
        "type pi = ^integer;\n"
        "var q: pi;\n"
        "begin q := nil; writeln(q^) end.\n");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("dereference of nil"), std::string::npos) << R.Stderr;
}

// The nil check had been grouped with the bounds checks, so asking for the
// bounds tests to be left out of a release build silently took this with it
// and a nil dereference became a signal with no line number.  Turning off the
// checks on indexing says nothing about wanting that.
TEST(RuntimeChecks, NilDerefCheckedEvenWithoutRangeChecks) {
    auto R = compileAndRun(
        "program p;\n"
        "type pi = ^integer;\n"
        "var q: pi;\n"
        "begin q := nil; writeln(q^) end.\n",
        "-fno-range-checks");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("dereference of nil"), std::string::npos) << R.Stderr;
}

TEST(RuntimeChecks, NoNilChecksFlagOmitsIt) {
    // Without the check the dereference reaches the hardware, so this is a
    // signal rather than a diagnostic — which is what the flag asks for.
    auto R = compileAndRun(
        "program p;\n"
        "type pi = ^integer;\n"
        "var q: pi;\n"
        "begin q := nil; writeln(q^) end.\n",
        "-fno-nil-checks");
    EXPECT_NE(R.ExitCode, 70);
    EXPECT_EQ(R.Stderr.find("dereference of nil"), std::string::npos) << R.Stderr;
}

TEST(RuntimeChecks, NoRangeChecksLeavesBoundsOutButKeepsNil) {
    // Both flags in one compilation: the bounds test is gone, the nil test
    // stays.  Written together because the two were one flag until 0.1.2.
    auto R = compileAndRun(
        "program p;\n"
        "type pi = ^integer;\n"
        "var a: array[1..5] of integer; i: integer; q: pi;\n"
        "begin\n"
        "  i := 9; a[i] := 1; writeln('past the bound');\n"
        "  q := nil; writeln(q^)\n"
        "end.\n",
        "-fno-range-checks");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stdout.find("past the bound"), std::string::npos) << R.Stdout;
    EXPECT_NE(R.Stderr.find("dereference of nil"), std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// ISO §6.2.2.10 — a required identifier may be redeclared
//
// Codegen used to dispatch these on the spelling of the name alone, before
// anything had asked which declaration the name denoted here, so the required
// procedure or function won wherever the two were spelled alike: the program's
// own body was compiled and never called.
// ---------------------------------------------------------------------------

TEST(RedeclaredRequired, UserFunctionWins) {
    auto R = compileAndRun(
        "program p(output);\n"
        "function abs(x: integer): integer;\n"
        "begin abs := 999 end;\n"
        "begin writeln(abs(-3)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "999\n");
}

TEST(RedeclaredRequired, RequiredFunctionStillReachedWhenNotRedeclared) {
    auto R = compileAndRun(
        "program p(output);\n"
        "begin writeln(abs(-3)); writeln(round(2.6)); writeln(trunc(2.6)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n3\n2\n");
}

TEST(RedeclaredRequired, UserFunctionWinsInsideItsScopeOnly) {
    // The inner abs is the program's; the one called before it is declared, in
    // a procedure of its own, is still the required one.  Which declaration a
    // name denotes is a question about where the call is written, and dispatch
    // on spelling could not ask it.
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure outer;\n"
        "begin writeln(abs(-7)) end;\n"
        "procedure inner;\n"
        "  function abs(x: integer): integer;\n"
        "  begin abs := 999 end;\n"
        "begin writeln(abs(-7)) end;\n"
        "begin outer; inner end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n999\n");
}

TEST(RedeclaredRequired, UserProcedureWins) {
    // A required procedure taking different arguments used to be reached with
    // none of them, which the IR verifier caught as a null operand.
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure pack(n: integer);\n"
        "begin writeln('mine ', n:1) end;\n"
        "begin pack(7) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "mine 7\n");
}

// A routine the program declares used to be mangled `plang_<name>`, the
// namespace the runtime's own entry points live in, so `page` here collided
// with the runtime's `plang_page` and the link failed.  User code is mangled
// `pas_` now; see the mangling note in CodegenImpl.h.
TEST(RedeclaredRequired, UserProcedureWinsOverRuntimeSymbolName) {
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure page(n: integer);\n"
        "begin writeln('mine ', n:1) end;\n"
        "begin page(7) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "mine 7\n");
}

// Every runtime entry point a program is at all likely to name for itself, in
// one program, so that the mangling namespaces cannot quietly grow back into
// each other.  Each of these was a duplicate-symbol error before 0.1.3 in some
// program or other — which one depended on what else the program used, since
// the twin only reaches the link when its translation unit is pulled out of
// the archive for another reason.
TEST(RedeclaredRequired, UserRoutinesMayBeNamedAfterRuntimeSymbols) {
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure close;   begin write('a') end;\n"
        "procedure reset;   begin write('b') end;\n"
        "procedure rewrite; begin write('c') end;\n"
        "procedure halt;    begin write('d') end;\n"
        "procedure page;    begin write('e') end;\n"
        "function  round: integer;  begin round := 1 end;\n"
        "function  trunc: integer;  begin trunc := 2 end;\n"
        "function  sqrt: integer;   begin sqrt  := 3 end;\n"
        "function  sin: integer;    begin sin   := 4 end;\n"
        "function  ln: integer;     begin ln    := 5 end;\n"
        "begin\n"
        "  close; reset; rewrite; halt; page;\n"
        "  writeln(round:1, trunc:1, sqrt:1, sin:1, ln:1)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "abcde12345\n");
}

// A global is mangled from its own prefix, and had the same problem.
TEST(RedeclaredRequired, UserGlobalsMayBeNamedAfterRuntimeSymbols) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var date, time, position, binding: integer;\n"
        "begin\n"
        "  date := 1; time := 2; position := 3; binding := 4;\n"
        "  writeln(date + time + position + binding)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n");
}

TEST(RedeclaredRequired, UserVariableNamedAfterRequiredFunction) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var odd: integer;\n"
        "begin odd := 42; writeln(odd) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

// ---------------------------------------------------------------------------
// The Pascal Acceptance Test (test/Acceptance) is a whole standard program,
// and each of these is one thing it found.  They are kept apart from it
// because a 3000-line program that stops at the first of eight errors says
// only that something is wrong.
// ---------------------------------------------------------------------------

// ISO §6.8.3.9 forbids a threat to "the variable denoted by the
// control-variable".  The scan compared the *spelling* of every assignment
// target in every procedure of the block against the loop's variable, so a
// procedure with a local of its own name was reported as assigning to a
// variable it cannot see.  `i` being the usual name for both, this rejected
// most programs of any size: 254 of the acceptance test's 292 errors.
TEST(ForThreat, ALocalOfTheSameNameIsADifferentVariable) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "procedure helper;\n"
        "var i: integer;\n"
        "begin i := 99 end;\n"
        "begin\n"
        "  for i := 1 to 3 do helper;\n"
        "  writeln('done')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "done\n");
}

TEST(ForThreat, AProcedureWithNoLocalStillThreatens) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "procedure threat;\n"
        "begin i := 99 end;\n"
        "begin for i := 1 to 3 do writeln(i) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("control variable 'i'") != std::string::npos)
        << R.Stderr;
}

// The declaration part contains the procedures nested inside it too.
TEST(ForThreat, ANestedProcedureThreatensAsWell) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "procedure outer;\n"
        "procedure nested;\n"
        "begin i := 99 end;\n"
        "begin nested end;\n"
        "begin for i := 1 to 3 do writeln(i) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("control variable 'i'") != std::string::npos)
        << R.Stderr;
}

// ISO §6.1.6: labels "shall be distinguished by their apparent integral
// values", and "the spelling of a label shall be its apparent integral value".
TEST(Labels, LeadingZeroesNameTheSameLabel) {
    auto R = compileAndRun(
        "program p(output);\n"
        "label 003;\n"
        "begin\n"
        "  goto 3;\n"
        "  writeln('skipped');\n"
        "  03: writeln('reached')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "reached\n");
}

TEST(Labels, OutsideZeroToNineThousandNineHundredNinetyNine) {
    auto R = compileAndRun(
        "program p(output);\n"
        "label 10000;\n"
        "begin 10000: writeln('x') end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("outside the range 0 to 9999") != std::string::npos)
        << R.Stderr;
}

// A subrange takes its values from its host type (ISO §6.4.2.4), so a subrange
// of integer holds integers.  '+', '-' and '*' accepted them and div and mod
// did not.
TEST(Subranges, DivAndModTakeThem) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var a, b: 0..100; c: -100..100;\n"
        "begin\n"
        "  a := 78; b := 43; c := -50;\n"
        "  writeln(a div b:1, ' ', a mod b:1, ' ', c div 7:1)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 35 -7\n");
}

TEST(Subranges, DivStillWantsAnInteger) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var r: real;\n"
        "begin r := 1.0; writeln(r div 2) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("requires integer operands") != std::string::npos)
        << R.Stderr;
}

// ISO §6.4.5 b) makes two subranges of one host type compatible whatever
// bounds each was written with; §6.4.6 c) leaves whether the value fits to be
// reported when the assignment happens.  Requiring the bounds to agree made
// this a type error.
TEST(Subranges, OneIsAssignableToAnotherOfDifferentBounds) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var a: 1..10; b: 1..100;\n"
        "begin b := 5; a := b; writeln(a:1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n");
}

TEST(Subranges, AValueOutsideTheDestinationIsStillReported) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var a: 1..10; b: 1..100;\n"
        "begin b := 50; a := b; writeln(a:1) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("out of range") != std::string::npos) << R.Stderr;
}

// Relaxing the bounds must not reach arrays: two array types of different
// extents are not one type, and assigning between them would write past the
// shorter.
TEST(Subranges, ArraysOfDifferentExtentsAreStillDistinct) {
    auto R = compileAndRun(
        "program p;\n"
        "var a: array[1..10] of integer; b: array[1..5] of integer;\n"
        "begin a := b end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("cannot assign") != std::string::npos) << R.Stderr;
}

// ISO §6.4.2.2: maxint is a constant, so it may stand wherever one may.
// Codegen knew its value and Sema did not, so a bound written with it was
// rejected as though the name were a variable.
TEST(Constants, MaxintIsAConstantExpression) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var v: -maxint..maxint;\n"
        "begin v := -5; writeln(v:1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-5\n");
}

// ISO §6.1.9: '@' is the alternative for '^' and the two are not to be
// distinguished, so a pointer type and a dereference may be written either way
// — including both ways for the one variable.
TEST(LexicalAlternatives, AtSignDenotesAPointer) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type iptr = @integer;\n"
        "var q: iptr; r: ^integer;\n"
        "begin\n"
        "  new(q); q@ := 42;\n"
        "  new(r); r^ := 7;\n"
        "  writeln(q@:1, ' ', q^:1, ' ', r@:1)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42 42 7\n");
}

// ISO §6.8.2.2 asks that "the function-block associated with the
// function-identifier of an assignment-statement shall contain the
// assignment-statement" — contain, not be.  A function declared inside another
// is contained in it, so it may assign the outer one's result.
TEST(FunctionResult, ANestedFunctionMayAssignTheEnclosingResult) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var n: integer;\n"
        "function outer: integer;\n"
        "var i: integer;\n"
        "  function inner: integer;\n"
        "  begin\n"
        "    inner := 12;\n"
        "    outer := 37\n"
        "  end;\n"
        "begin i := inner; writeln('inner ', i:1) end;\n"
        "begin n := outer; writeln('outer ', n:1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "inner 12\nouter 37\n");
}

// The assignment written in the nested function is the one that satisfies the
// outer function, so it must not still be reported as never assigning.
TEST(FunctionResult, TheNestedAssignmentSatisfiesTheOuterFunction) {
    auto R = compileAndRun(
        "program p(output);\n"
        "function outer: integer;\n"
        "var i: integer;\n"
        "  function inner: integer;\n"
        "  begin inner := 1; outer := 5 end;\n"
        "begin i := inner end;\n"
        "begin writeln(outer:1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n");
}

// A function that assigns nothing anywhere is still reported.
TEST(FunctionResult, OneThatNeverAssignsIsStillReported) {
    auto R = compileAndRun(
        "program p(output);\n"
        "function f: integer;\n"
        "begin end;\n"
        "begin writeln(f) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("does not assign to its result") != std::string::npos)
        << R.Stderr;
}

// A local of the function's name denotes the local, the nearer declaration
// winning, so assigning it is not assigning the result.
TEST(FunctionResult, ALocalOfTheSameNameIsNotTheResult) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var n: integer;\n"
        "function f: integer;\n"
        "  procedure g;\n"
        "  var f: integer;\n"
        "  begin f := 1; writeln('local ', f:1) end;\n"
        "begin g; f := 2 end;\n"
        "begin n := f; writeln('result ', n:1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "local 1\nresult 2\n");
}

TEST(Constants, MaxcharIsOneToo) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var c: 'a'..maxchar;\n"
        "begin c := 'q'; writeln(c) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "q\n");
}

// ---------------------------------------------------------------------------
// EP §6.4.3.4 — writing the TimeStamp flags
//
// write dispatched on the LLVM type alone.  TimeStamp holds DateValid and
// TimeValid as i8 so the record matches its C counterpart byte for byte, and
// at that width a boolean is indistinguishable from a char, so both were
// written as character 1 — which shows up as nothing at all.
// ---------------------------------------------------------------------------

TEST(WriteBoolean, TimeStampFlags) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin\n"
        "  GetTimeStamp(t);\n"
        "  writeln('DateValid=', t.DateValid, ' TimeValid=', t.TimeValid)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "DateValid=true TimeValid=true\n");
}

TEST(WriteBoolean, TimeStampFlagWithAFieldWidth) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin GetTimeStamp(t); writeln('[', t.DateValid:7, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[   true]\n");
}

TEST(WriteBoolean, CharsAreStillWrittenAsChars) {
    // The two share an LLVM type, so the fix has to keep them apart.
    auto R = compileAndRun(
        "program p;\n"
        "var c: char;\n"
        "begin c := 'A'; writeln(c, ' ', c:3) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "A   A\n");
}

TEST(WriteBoolean, OrdinaryBooleansAreUnaffected) {
    auto R = compileAndRun(
        "program p;\n"
        "type r = record flag: boolean end;\n"
        "var b: boolean; x: r;\n"
        "begin b := true; x.flag := false;\n"
        "  writeln(b, ' ', x.flag, ' ', b:6) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false   true\n");
}

// ---------------------------------------------------------------------------
// ISO §6.6.1 — forward declarations
//
// A 'forward' declaration exists so that a procedure can be called before its
// body appears, which is what makes mutual recursion expressible.  Neither
// half of that worked: the call site, reaching a name with no function behind
// it yet, invented a declaration from the shape of the argument list, and the
// definition then found the name taken and was renamed to plang_b.1, leaving
// 'undefined symbol: plang_b' at link time.  Separately, the standard spelling
// of the defining occurrence — the name alone, repeating neither the parameter
// list nor the result type — was rejected outright.
// ---------------------------------------------------------------------------

TEST(ForwardDecl, MutualRecursionLinks) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure b(n: integer); forward;\n"
        "procedure a(n: integer); begin if n > 0 then b(n - 1) end;\n"
        "procedure b(n: integer); begin write(n); a(n) end;\n"
        "begin a(3); writeln('.') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "210.\n");
}

TEST(ForwardDecl, MutuallyRecursiveFunctions) {
    auto R = compileAndRun(
        "program p;\n"
        "function isodd(n: integer): boolean; forward;\n"
        "function iseven(n: integer): boolean;\n"
        "begin if n = 0 then iseven := true else iseven := isodd(n - 1) end;\n"
        "function isodd(n: integer): boolean;\n"
        "begin if n = 0 then isodd := false else isodd := iseven(n - 1) end;\n"
        "begin writeln(iseven(10), ' ', isodd(7)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true true\n");
}

TEST(ForwardDecl, AVarParameterSurvivesTheForwardCall) {
    // The invented declaration used to be built from the argument's type, so a
    // var parameter came out as i64 where the definition wanted ptr.
    auto R = compileAndRun(
        "program p;\n"
        "var v: integer;\n"
        "procedure b(var x: integer); forward;\n"
        "procedure a(var x: integer); begin b(x) end;\n"
        "procedure b(var x: integer); begin x := 42 end;\n"
        "begin v := 0; a(v); writeln(v) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(ForwardDecl, NestedInsideAProcedure) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure outer;\n"
        "  var k: integer;\n"
        "  procedure b(n: integer); forward;\n"
        "  procedure a(n: integer); begin if n > 0 then b(n - 1) end;\n"
        "  procedure b(n: integer); begin k := k + 1; a(n) end;\n"
        "begin k := 0; a(4); writeln(k) end;\n"
        "begin outer end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4\n");
}

TEST(ForwardDecl, IsoFormOmitsTheParameterList) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure b(n: integer); forward;\n"
        "procedure a(n: integer); begin if n > 0 then b(n - 1) end;\n"
        "procedure b;\n"
        "begin write(n); a(n) end;\n"
        "begin a(3); writeln('.') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "210.\n");
}

TEST(ForwardDecl, IsoFormOmitsTheResultTypeToo) {
    auto R = compileAndRun(
        "program p;\n"
        "function g(n: integer): integer; forward;\n"
        "function f(n: integer): integer; begin f := g(n) + 1 end;\n"
        "function g;\n"
        "begin g := n * 2 end;\n"
        "begin writeln(f(5)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11\n");
}

TEST(ForwardDecl, IsoFormWithAVarParameter) {
    auto R = compileAndRun(
        "program p;\n"
        "var v: integer;\n"
        "procedure b(var x: integer); forward;\n"
        "procedure a(var x: integer); begin b(x) end;\n"
        "procedure b;\n"
        "begin x := 42 end;\n"
        "begin v := 0; a(v); writeln(v) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(ForwardDecl, RepeatingTheHeadingIsStillAccepted) {
    // Not what ISO §6.6.1 asks for, but what most Pascals allow and what this
    // compiler has always taken.
    auto R = compileAndRun(
        "program p;\n"
        "function g(n: integer): integer; forward;\n"
        "function g(n: integer): integer;\n"
        "begin g := n * 2 end;\n"
        "begin writeln(g(21)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(ForwardDecl, ARepeatedHeadingThatDisagreesIsRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure b(n: integer); forward;\n"
        "procedure b(n: real);\n"
        "begin writeln(n:0:1) end;\n"
        "begin b(1.0) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("does not match forward declaration"),
              std::string::npos) << R.Stderr;
}

TEST(ForwardDecl, AParameterlessProcedureIsNotTreatedAsForward) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure q; begin writeln('q') end;\n"
        "begin q end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "q\n");
}

TEST(ForwardDecl, FunctionWithoutAResultTypeAndNoForwardDeclaration) {
    // The parser now carries this heading through, since only Sema can tell it
    // from the ISO defining occurrence above.  It must still be rejected, and
    // with a diagnostic rather than an internal error from codegen.
    auto R = compileAndRun(
        "program p;\n"
        "function f;\n"
        "begin f := 1 end;\n"
        "begin writeln(f) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("has no result type"), std::string::npos) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("internal error"), std::string::npos) << R.Stderr;
}

TEST(ForwardDecl, ConformantArrayParameterThroughAForwardCall) {
    auto R = compileAndRun(
        "program p;\n"
        "var arr: array[1..3] of integer;\n"
        "function total(var a: array[lo..hi: integer] of integer): integer;"
        " forward;\n"
        "function go(var a: array[lo..hi: integer] of integer): integer;\n"
        "begin go := total(a) end;\n"
        "function total;\n"
        "var i, s: integer;\n"
        "begin s := 0; for i := lo to hi do s := s + a[i]; total := s end;\n"
        "begin arr[1] := 1; arr[2] := 2; arr[3] := 3; writeln(go(arr)) end.\n",
        kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "6\n");
}

// ---------------------------------------------------------------------------
// ISO §6.4.2.2 ordinal comparison.  Every ordinal is ordered by its ordinal
// number, which is never negative — but LLVM reads an i1 holding 'true' as -1
// under a signed compare, so booleans came out reversed and a boolean for-loop
// wrapped instead of terminating.
// ---------------------------------------------------------------------------

TEST(OrdinalCompare, BooleansOrderFalseBeforeTrue) {
    auto R = compileAndRun(
        "program p;\n"
        "var t, f: boolean;\n"
        "begin t := true; f := false;\n"
        " writeln(f < t, ' ', f <= t, ' ', t > f, ' ', t >= f);\n"
        " writeln(t < f, ' ', t <= f, ' ', f > t, ' ', f >= t) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true true true true\nfalse false false false\n");
}

TEST(OrdinalCompare, CharsAboveTheSignedRangeCompareHigh) {
    auto R = compileAndRun(
        "program p;\n"
        "var c: char;\n"
        "begin c := chr(200); writeln(c > 'a', ' ', c < 'a') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false\n");
}

TEST(OrdinalCompare, EnumerationsKeepDeclarationOrder) {
    auto R = compileAndRun(
        "program p;\n"
        "type col = (red, green, blue);\n"
        "var a, b: col;\n"
        "begin a := red; b := blue; writeln(a < b, ' ', b < a) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false\n");
}

TEST(OrdinalCompare, NegativeIntegersStillCompareSigned) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: integer;\n"
        "begin a := -5; b := 3; writeln(a < b, ' ', a > b) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false\n");
}

TEST(ForLoop, OverBooleanRunsTwiceAndTerminates) {
    auto R = compileAndRun(
        "program p;\n"
        "var b: boolean;\n"
        "begin for b := false to true do write(b, ' '); writeln('done') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "false true done\n");
}

TEST(ForLoop, OverCharSubrangeVisitsEachLetter) {
    auto R = compileAndRun(
        "program p;\n"
        "var c: char;\n"
        "begin for c := 'a' to 'e' do write(c); writeln end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "abcde\n");
}

TEST(ForLoop, ToMaxintTerminates) {
    // ISO §6.8.3.9: the control variable is never advanced past the final
    // value.  Incrementing first and comparing afterwards overflows here and
    // the loop never ends.
    auto R = compileAndRun(
        "program p;\n"
        "var i, n: integer;\n"
        "begin n := 0; for i := maxint - 2 to maxint do n := n + 1;\n"
        " writeln(n) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n");
}

TEST(ForLoop, DowntoFromMinintTerminates) {
    auto R = compileAndRun(
        "program p;\n"
        "var i, n: integer;\n"
        "begin n := 0; for i := -maxint + 1 downto -maxint do n := n + 1;\n"
        " writeln(n) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n");
}

TEST(ForLoop, EmptyRangeRunsZeroTimes) {
    auto R = compileAndRun(
        "program p;\n"
        "var i, n: integer;\n"
        "begin n := 0; for i := 5 to 1 do n := n + 1; writeln(n) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0\n");
}

// ---------------------------------------------------------------------------
// Integer-to-real widening on assignment.  The destination decides whether a
// conversion is needed, so reading the type off the source stored the integer
// bit pattern into every double that was not a bare variable.
// ---------------------------------------------------------------------------

TEST(IntToReal, WidensIntoAnArrayElement) {
    auto R = compileAndRun(
        "program p;\n"
        "var a: array[1..3] of real;\n"
        "begin a[1] := 5; writeln(a[1]:0:4) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5.0000\n");
}

TEST(IntToReal, WidensIntoARecordField) {
    auto R = compileAndRun(
        "program p;\n"
        "type r = record x: real end;\n"
        "var q: r;\n"
        "begin q.x := 5; writeln(q.x:0:4) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5.0000\n");
}

TEST(IntToReal, WidensIntoANestedFieldOfAnArrayElement) {
    auto R = compileAndRun(
        "program p;\n"
        "type r = record x: real end;\n"
        "var a: array[1..2] of r;\n"
        "begin a[2].x := 7; writeln(a[2].x:0:4) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7.0000\n");
}

TEST(IntToReal, WidensThroughAPointerDereference) {
    auto R = compileAndRun(
        "program p;\n"
        "type pr = ^real;\n"
        "var p: pr;\n"
        "begin new(p); p^ := 9; writeln(p^:0:4); dispose(p) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "9.0000\n");
}

TEST(IntToReal, WidensInsideAWithStatement) {
    auto R = compileAndRun(
        "program p;\n"
        "type r = record x: real end;\n"
        "var q: r;\n"
        "begin with q do x := 3; writeln(q.x:0:4) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3.0000\n");
}

TEST(IntToReal, AccumulatesCorrectlyThroughAnArray) {
    auto R = compileAndRun(
        "program p;\n"
        "var a: array[1..3] of real; i: integer; s: real;\n"
        "begin for i := 1 to 3 do a[i] := i;\n"
        " s := 0; for i := 1 to 3 do s := s + a[i]; writeln(s:0:2) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "6.00\n");
}

// ---------------------------------------------------------------------------
// ISO §6.7.2.2: i mod j lies in [0, j).  C's remainder takes its sign from the
// dividend, so a negative left operand came back negative.
// ---------------------------------------------------------------------------

TEST(Modulo, NegativeDividendYieldsANonNegativeResult) {
    auto R = compileAndRun(
        "program p;\n"
        "begin writeln((-17) mod 5, ' ', 17 mod 5, ' ', (-15) mod 5,\n"
        " ' ', (-1) mod 5) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3 2 0 4\n");
}

TEST(Modulo, ConstantFoldingAgreesWithTheRuntime) {
    // A folded constant and a computed value must not disagree.
    auto R = compileAndRun(
        "program p;\n"
        "const c = (-17) mod 5;\n"
        "var a, b: integer;\n"
        "begin a := -17; b := 5; writeln(c, ' ', a mod b) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3 3\n");
}

TEST(Modulo, DivStaysTruncating) {
    // div is unchanged: ISO §6.7.2.2 truncates it toward zero.
    auto R = compileAndRun(
        "program p;\n"
        "begin writeln((-17) div 5, ' ', 17 div 5) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-3 3\n");
}

// ---------------------------------------------------------------------------
// ISO §6.10: 'input' names the same stdin a bare read reaches through getchar.
// Priming the file's lookahead used to consume a character that the bare path
// then never saw, so a program lost its first byte by naming input in the
// header.
// ---------------------------------------------------------------------------

TEST(StandardInput, HeaderParameterDoesNotConsumeTheFirstCharacter) {
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var i: integer;\n"
        "begin read(i); writeln(i) end.\n", "", "42\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(StandardInput, HeaderParameterDoesNotConsumeTheFirstChar) {
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var c: char;\n"
        "begin read(c); writeln(c) end.\n", "", "AB\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "A\n");
}

TEST(StandardInput, TestingEofFirstDoesNotConsumeAnything) {
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var i: integer;\n"
        "begin writeln(eof); read(i); writeln(i) end.\n", "", "42\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "false\n42\n");
}

TEST(StandardInput, ExplicitAndImplicitReadsShareOnePosition) {
    // read(input, x) goes through the file object and read(x) through the
    // stream directly; they have to agree on where the cursor is.
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var a, b: char;\n"
        "begin read(input, a); read(b); writeln(a, b) end.\n", "", "AB\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "AB\n");
}

TEST(StandardInput, EolnAndReadlnWalkTwoLines) {
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var c: char;\n"
        "begin while not eoln do begin read(c); write(c) end; readln;\n"
        " writeln;\n"
        " while not eoln do begin read(c); write(c) end; writeln end.\n",
        "", "ab\ncd\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "ab\ncd\n");
}

// ---------------------------------------------------------------------------
// ISO §6.2.2.3: a type or constant declared in a block is invisible outside it,
// and a variable declared alongside it sees the block's own declaration.
// ---------------------------------------------------------------------------

TEST(BlockScope, LocalTypeShadowsAnOuterTypeOfTheSameName) {
    auto R = compileAndRun(
        "program p;\n"
        "type t = char;\n"
        "var g: t;\n"
        "procedure q; type t = integer; var v: t;\n"
        "begin v := 65; writeln('inner ', v) end;\n"
        "begin g := 'Z'; writeln('outer ', g); q;\n"
        " writeln('outer ', g) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "outer Z\ninner 65\nouter Z\n");
}

TEST(BlockScope, LocalTypeShadowsInTheOtherDirection) {
    auto R = compileAndRun(
        "program p;\n"
        "type t = integer;\n"
        "var g: t;\n"
        "procedure q; type t = char; var v: t;\n"
        "begin v := 'Q'; writeln('inner ', v) end;\n"
        "begin g := 7; writeln('outer ', g); q end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "outer 7\ninner Q\n");
}

TEST(BlockScope, ALocalTypeDoesNotLeakToASibling) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure a; type t = char; var v: t;\n"
        "begin v := 'X'; writeln('a ', v) end;\n"
        "procedure b; type t = integer; var v: t;\n"
        "begin v := 9; writeln('b ', v) end;\n"
        "begin a; b; a end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "a X\nb 9\na X\n");
}

TEST(BlockScope, ALocalConstantShadowsAndDoesNotLeak) {
    auto R = compileAndRun(
        "program p;\n"
        "const c = 1;\n"
        "procedure q; const c = 2; begin writeln('inner ', c) end;\n"
        "begin writeln('outer ', c); q; writeln('outer ', c) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "outer 1\ninner 2\nouter 1\n");
}

TEST(BlockScope, ANestedProcedureSeesTheEnclosingProceduresType) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure a; type t = integer; var v: t;\n"
        " procedure b; var w: t; begin w := 3; writeln('b ', w) end;\n"
        "begin v := 1; writeln('a ', v); b end;\n"
        "begin a end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "a 1\nb 3\n");
}

TEST(BlockScope, ALocalRecordTypeShadowsAnOuterOne) {
    auto R = compileAndRun(
        "program p;\n"
        "type r = record x: integer end;\n"
        "var g: r;\n"
        "procedure q; type r = record y: char end; var v: r;\n"
        "begin v.y := 'K'; writeln('inner ', v.y) end;\n"
        "begin g.x := 4; writeln('outer ', g.x); q end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "outer 4\ninner K\n");
}

// ---------------------------------------------------------------------------
// ISO §6.9.3.1: a field width applies wherever the value is going, including
// a text file.  The formatted writers ignored the file and wrote to stdout.
// ---------------------------------------------------------------------------

TEST(FileWrite, FieldWidthsGoToTheFileNotStdout) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: text; c: char;\n"
        "begin rewrite(f, 'plang_fw_test.txt');\n"
        " write(f, 42:5); write(f, 'x':3); writeln(f, 1.5:8:2);\n"
        " close(f); reset(f, 'plang_fw_test.txt');\n"
        " while not eof(f) do begin read(f, c); write(c) end;\n"
        " close(f) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // The trailing character is a space, not the newline the writeln put in
    // the file: §6.4.3.5 gives f^ the value of a space at a line marker, so
    // reading a text file character by character never yields one.
    EXPECT_EQ(R.Stdout, "   42  x    1.50 ");
}

// ---------------------------------------------------------------------------
// ISO §6.6.6.4: succ and pred yield a value of the argument's type.
// ---------------------------------------------------------------------------

TEST(SuccPred, KeepsTheCharType) {
    auto R = compileAndRun(
        "program p;\n"
        "var c: char;\n"
        "begin c := succ('a'); writeln(c); c := pred('z'); writeln(c) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "b\ny\n");
}

TEST(SuccPred, KeepsTheEnumerationType) {
    auto R = compileAndRun(
        "program p;\n"
        "type col = (red, green, blue);\n"
        "var c: col;\n"
        "begin c := succ(red); writeln(ord(c));\n"
        " c := pred(blue); writeln(ord(c)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1\n1\n");
}

TEST(SuccPred, StaysIntegerForAnIntegerArgument) {
    auto R = compileAndRun(
        "program p;\n"
        "var i: integer;\n"
        "begin i := succ(5); writeln(i) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "6\n");
}

TEST(SuccPred, RejectsANonOrdinalArgument) {
    auto R = compileAndRun(
        "program p;\n"
        "var r: real;\n"
        "begin r := succ(1.5); writeln(r) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("requires an ordinal argument"), std::string::npos)
        << R.Stderr;
}

// ---------------------------------------------------------------------------
// ISO §6.7.2.5: pointers compare with = and <> against each other and against
// nil.  Rejecting that made every linked structure inexpressible.
// ---------------------------------------------------------------------------

TEST(PointerCompare, AgainstNil) {
    auto R = compileAndRun(
        "program p;\n"
        "type pi = ^integer;\n"
        "var q: pi;\n"
        "begin q := nil; writeln(q = nil, ' ', q <> nil);\n"
        " new(q); writeln(q = nil, ' ', q <> nil); dispose(q) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false\nfalse true\n");
}

TEST(PointerCompare, TwoPointersOfTheSameType) {
    auto R = compileAndRun(
        "program p;\n"
        "type pi = ^integer;\n"
        "var a, b: pi;\n"
        "begin new(a); b := a; writeln(a = b);\n"
        " new(b); writeln(a = b); dispose(a); dispose(b) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true\nfalse\n");
}

TEST(PointerCompare, WalksALinkedList) {
    auto R = compileAndRun(
        "program p;\n"
        "type pn = ^node; node = record v: integer; next: pn end;\n"
        "var head, q: pn; i: integer;\n"
        "begin head := nil;\n"
        " for i := 3 downto 1 do\n"
        "  begin new(q); q^.v := i; q^.next := head; head := q end;\n"
        " q := head;\n"
        " while q <> nil do begin write(q^.v, ' '); q := q^.next end;\n"
        " writeln end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 2 3 \n");
}

TEST(PointerCompare, OrderingOperatorsAreRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "type pi = ^integer;\n"
        "var a, b: pi;\n"
        "begin a := nil; b := nil; writeln(a < b) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("not defined for pointers"), std::string::npos)
        << R.Stderr;
}

TEST(PointerCompare, APointerAndAnIntegerAreStillIncomparable) {
    auto R = compileAndRun(
        "program p;\n"
        "type pi = ^integer;\n"
        "var a: pi; n: integer;\n"
        "begin a := nil; n := 0; writeln(a = n) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("cannot compare"), std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// ISO §6.9.3.4.1: the floating-point representation of a real
//
// A real written without a fraction length is written with an exponent, in a
// shape printf has no conversion for: the field width sets how many decimal
// places are written rather than how much padding goes in front, and a
// positive value still occupies its sign column so that a column of them lines
// up.  The runtime used C's %g and %e, so `writeln(1.0)` wrote `1`.
// ---------------------------------------------------------------------------

TEST(WriteReal, DefaultIsTheFloatingPointRepresentation) {
    auto R = compileAndRun(
        "program p(output);\n"
        "begin writeln(1.0); writeln(-2.5); writeln(0.0) end.\n", "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, " 1.00000000000000e+000\n"
                        "-2.50000000000000e+000\n"
                        " 0.00000000000000e+000\n");
}

// DecPlaces := ActWidth - ExpDigits - 5, so a wider field is written to more
// decimal places and fills exactly, rather than being padded with spaces.
TEST(WriteReal, WidthSetsTheNumberOfDecimalPlaces) {
    auto R = compileAndRun(
        "program p(output);\n"
        "begin writeln(1.0:20); writeln(1.0:12); writeln(1.0:9) end.\n",
        "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, " 1.000000000000e+000\n"
                        " 1.0000e+000\n"
                        " 1.0e+000\n");
}

// A width too small for the representation is raised to the smallest it fits
// in, which is ExpDigits + 6.
TEST(WriteReal, TooNarrowAWidthIsRaisedToTheMinimum) {
    auto R = compileAndRun(
        "program p(output); begin writeln(1.0:1) end.\n", "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, " 1.0e+000\n");
}

// Three exponent digits cover the whole range a double reaches, so no value is
// written in a shape other than the advertised one.
TEST(WriteReal, TheExponentIsAlwaysThreeDigits) {
    auto R = compileAndRun(
        "program p(output);\n"
        "begin writeln(1.0e308); writeln(1.0e-10) end.\n", "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, " 1.00000000000000e+308\n"
                        " 1.00000000000000e-010\n");
}

// §6.9.3.4.2: naming a fraction length asks for the fixed-point form instead,
// which is the one form that was already right.
TEST(WriteReal, AFractionLengthGivesTheFixedPointForm) {
    auto R = compileAndRun(
        "program p(output);\n"
        "begin writeln(3.14159:8:3); writeln(2.5:1:1) end.\n", "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "   3.142\n2.5\n");
}

// The sign character is '-' only when the value is negative and has not
// rounded away, so a small negative written to few places is not '-0'.
TEST(WriteReal, ANegativeRoundingToZeroKeepsTheSpace) {
    auto R = compileAndRun(
        "program p(output); begin writeln(-1.0e-30:9) end.\n", "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-1.0e-030\n");
}

// The same representation on a text file as on output.
TEST(WriteReal, ATextFileGetsTheSameRepresentation) {
    auto R = compileAndRun(
        "program p(output); var f: text; c: char;\n"
        "begin rewrite(f, 'r.txt'); writeln(f, 1.0); close(f);\n"
        " reset(f, 'r.txt');\n"
        " while not eof(f) do begin read(f, c); write(c) end;\n"
        " close(f) end.\n", "-std=iso7185");
    // The trailing space is the line marker, which §6.4.3.5 reads as one.
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, " 1.00000000000000e+000 ");
}

// EP §6.7.5.5: writestr is defined in terms of writing to a text file, so it
// gets the representation too.
TEST(WriteReal, WritestrGetsTheSameRepresentation) {
    auto R = compileAndRun(
        "program p(output); var s: string(40);\n"
        "begin writestr(s, 1.0); writeln(s) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, " 1.00000000000000e+000\n");
}

// A real written out and read back is the value that was written.
TEST(WriteReal, TheRepresentationReadsBack) {
    auto R = compileAndRun(
        "program p(output); var f: text; x: real;\n"
        "begin rewrite(f, 'rt.txt'); writeln(f, 1.0/3.0); close(f);\n"
        " reset(f, 'rt.txt'); read(f, x); close(f);\n"
        " writeln(x:20:15) end.\n", "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "   0.333333333333333\n");
}

// ---------------------------------------------------------------------------
// ISO §6.9.3: only a write-parameter may be written
// ---------------------------------------------------------------------------

TEST(WriteTypeCheck, ARecordIsRejected) {
    auto R = compileAndRun(
        "program p(output); type r = record x: integer end;\n"
        "var q: r; begin writeln(q) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("cannot be written"), std::string::npos) << R.Stderr;
}

TEST(WriteTypeCheck, AnArrayIsRejected) {
    auto R = compileAndRun(
        "program p(output); var a: array[1..3] of integer;\n"
        "begin writeln(a) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("cannot be written"), std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// ISO §6.4.2.3: an enumeration written inline still introduces its constants
// ---------------------------------------------------------------------------

TEST(InlineEnum, AVarDeclarationIntroducesItsConstants) {
    auto R = compileAndRun(
        "program p(output); var e: (red, green, blue);\n"
        "begin e := blue; writeln(ord(e)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n");
}

TEST(InlineEnum, ALocalVarDeclarationIntroducesItsConstants) {
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure q; var e: (x, y); begin e := y; writeln(ord(e)) end;\n"
        "begin q end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1\n");
}

TEST(InlineEnum, ARecordFieldIntroducesItsConstants) {
    auto R = compileAndRun(
        "program p(output); var r: record c: (red, green) end;\n"
        "begin r.c := green; writeln(ord(r.c)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1\n");
}

// ---------------------------------------------------------------------------
// ISO §6.1.5: a real literal may carry a scale factor
// ---------------------------------------------------------------------------

TEST(ScaleFactor, IsAcceptedInEveryFormAndValue) {
    auto R = compileAndRun(
        "program p(output);\n"
        "const k = 1e3;\n"
        "var r: real;\n"
        "begin\n"
        "  r := 1e3;     writeln(r:0:2);\n"
        "  r := 1.5E-2;  writeln(r:0:4);\n"
        "  r := 2.5e+10; writeln(r:0:1);\n"
        "  r := k;       writeln(r:0:2)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1000.00\n0.0150\n25000000000.0\n1000.00\n");
}

TEST(ScaleFactor, OneTooLargeToRepresentIsReported) {
    auto R = compileAndEmitIR("program p(output);\nbegin write(1.0e100000) end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("out of range"), std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// ISO §6.4.3.2: array index types and the multi-dimension abbreviation
// ---------------------------------------------------------------------------

TEST(MultiDimArray, TheCommaFormIsTheNestedForm) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var a: array[1..2, 1..3] of integer; i, j: integer;\n"
        "begin\n"
        "  for i := 1 to 2 do for j := 1 to 3 do a[i, j] := i * 10 + j;\n"
        "  writeln(a[1, 1], ' ', a[2, 3], ' ', a[2][3])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11 23 23\n");
}

TEST(MultiDimArray, ThreeDimensions) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var t: array[1..2, 1..2, 1..2] of integer;\n"
        "begin t[1, 2, 1] := 42; writeln(t[1, 2, 1]) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

// Only the first dimension indexes a name; the rest index an expression, and
// that path had no extent to test against, so an inner index ran off the end
// in silence.
TEST(MultiDimArray, AnInnerIndexIsBoundsCheckedToo) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var a: array[1..2, 1..3] of integer; i: integer;\n"
        "begin i := 5; a[1, i] := 1 end.\n");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("array index 5 out of bounds 1..3"), std::string::npos)
        << R.Stderr;
}

TEST(ArrayIndexType, ANamedEnumerationSpansTheWholeType) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type color = (red, green, blue);\n"
        "var a: array[color] of integer; c: color;\n"
        "begin\n"
        "  for c := red to blue do a[c] := ord(c) + 1;\n"
        "  writeln(a[red], ' ', a[green], ' ', a[blue])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 2 3\n");
}

TEST(ArrayIndexType, BooleanAndANamedSubrangeAndAnInlineEnumeration) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type idx = 1..3;\n"
        "var b: array[boolean] of integer;\n"
        "    n: array[idx] of integer;\n"
        "    e: array[(lo, mid, hi)] of integer;\n"
        "begin\n"
        "  b[false] := 4; b[true] := 5;\n"
        "  n[1] := 11; n[3] := 33;\n"
        "  e[lo] := 1; e[hi] := 3;\n"
        "  writeln(b[false], b[true], ' ', n[1], n[3], ' ', e[lo], e[hi])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "45 1133 13\n");
}

TEST(ArrayIndexType, ATypeWithNoBoundedRangeIsRejected) {
    for (const char* T : {"integer", "real"}) {
        auto R = compileAndEmitIR(
            std::string("program p;\nvar a: array[") + T +
            "] of integer;\nbegin end.\n");
        EXPECT_FALSE(R.Ok) << T;
        EXPECT_NE(R.Stderr.find("cannot be an array index type"),
                  std::string::npos) << T << ": " << R.Stderr;
    }
}

// The bounds of an array index are constant expressions, which a subrange
// type-denoter's are not; parsing the index as a type must not narrow them.
TEST(ArrayIndexType, ARangeBoundIsStillAFullConstantExpression) {
    auto R = compileAndRun(
        "program p(output);\n"
        "const k = 4;\n"
        "var a: array[1..k * 2] of integer;\n"
        "begin a[8] := 64; writeln(a[8]) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "64\n");
}

// ---------------------------------------------------------------------------
// ISO §6.4.3.2: packed array[1..n] of char is a string type
// ---------------------------------------------------------------------------

TEST(CharStringType, TakesALiteralAndIsWrittenAndCompared) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s, t: packed array[1..5] of char; i: integer;\n"
        "begin\n"
        "  s := 'hello'; t := 'world';\n"
        "  write(s); write(' '); writeln(t);\n"
        "  if s = 'hello' then writeln('eq');\n"
        "  if s < t then writeln('lt');\n"
        "  if s <> t then writeln('ne');\n"
        "  for i := 1 to 5 do write(s[i]);\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "hello world\neq\nlt\nne\nhello\n");
}

// Two equal strings held in different variables used to compare unequal: as
// arrays they fell through to a comparison of their addresses.
TEST(CharStringType, TwoVariablesHoldingTheSameTextAreEqual) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s, t: packed array[1..3] of char;\n"
        "begin s := 'abc'; t := 'abc'; writeln(s = t, ' ', s < t) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false\n");
}

TEST(CharStringType, CopiesWholeAndHonoursAFieldWidth) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s, t: packed array[1..5] of char;\n"
        "begin s := 'hello'; t := s;\n"
        "  writeln(t); writeln(t:8); writeln('[', t, ']') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "hello\n   hello\n[hello]\n");
}

// There is no length field to record a shorter value in, so ISO requires the
// literal to be exactly n characters — too short is as wrong as too long.
TEST(CharStringType, ALiteralOfTheWrongLengthIsRejected) {
    auto R = compileAndEmitIR(
        "program p;\n"
        "var s: packed array[1..5] of char;\n"
        "begin s := 'hi'; s := 'toolong' end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("string of length 2 does not fit"), std::string::npos)
        << R.Stderr;
    EXPECT_NE(R.Stderr.find("string of length 7 does not fit"), std::string::npos)
        << R.Stderr;
}

// The standard makes 'packed' part of what a string type is, and a
// one-character literal is a char rather than a string.
TEST(CharStringType, AnUnpackedCharArrayIsNotAStringType) {
    auto R = compileAndEmitIR(
        "program p(output);\n"
        "var s: array[1..5] of char;\n"
        "begin s := 'hello'; writeln(s) end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("cannot be written"), std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// ISO §6.4.3.3: variant records
// ---------------------------------------------------------------------------

// The variant part was dropped when the LLVM struct was built, so the tag and
// every variant field were missing from the record's storage and touching one
// was an internal error: "record has no field named 'kind'".
TEST(VariantRecord, TheTagAndEveryVariantFieldAreReachable) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type shape = (circle, rect);\n"
        "     fig = record\n"
        "       name: char;\n"
        "       case kind: shape of\n"
        "         circle: (r: integer);\n"
        "         rect:   (w, h: integer)\n"
        "     end;\n"
        "var f: fig;\n"
        "begin\n"
        "  f.name := 'c'; f.kind := circle; f.r := 5;\n"
        "  writeln(f.name, ' ', ord(f.kind), ' ', f.r);\n"
        "  f.kind := rect; f.w := 2; f.h := 3;\n"
        "  writeln(ord(f.kind), ' ', f.w, ' ', f.h)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "c 0 5\n1 2 3\n");
}

// At most one alternative is active at a time, so they share their storage:
// the first field of each starts at the same place.
TEST(VariantRecord, TheAlternativesShareTheirStorage) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type fig = record\n"
        "       case kind: integer of\n"
        "         1: (r: integer);\n"
        "         2: (w, h: integer)\n"
        "     end;\n"
        "var f: fig;\n"
        "begin f.w := 2; f.h := 3; writeln(f.r) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n");
}

TEST(VariantRecord, AnAnonymousTagStillSelects) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type u = record n: integer;\n"
        "       case boolean of true: (i: integer); false: (c: char) end;\n"
        "var x: u;\n"
        "begin x.n := 1; x.i := 65; writeln(x.n, ' ', x.c) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 A\n");
}

TEST(VariantRecord, ANestedVariantIsLaidOutAfterTheFieldsAroundIt) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type t = record\n"
        "       case a: integer of\n"
        "         1: (x: integer;\n"
        "             case b: integer of\n"
        "               10: (p: integer);\n"
        "               11: (q: real));\n"
        "         2: (y: real)\n"
        "     end;\n"
        "var v: t;\n"
        "begin\n"
        "  v.a := 1; v.x := 7; v.b := 10; v.p := 99;\n"
        "  writeln(v.a, ' ', v.x, ' ', v.b, ' ', v.p);\n"
        "  v.q := 1.5; writeln(v.q:0:1)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 7 10 99\n1.5\n");
}

TEST(VariantRecord, EmptyAlternativesReserveNothing) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type t = record k: integer;\n"
        "       case b: boolean of true: (); false: () end;\n"
        "var z: t;\n"
        "begin z.k := 4; z.b := true; writeln(z.k, ' ', z.b) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4 true\n");
}

// A record variable, a record reached through a pointer and a record inside an
// array were laid out by different paths, only one of which saw the variant.
TEST(VariantRecord, IsLaidOutTheSameWhereverItIsReached) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type w = record case s: integer of 1: (a: integer); 2: (b: real) end;\n"
        "     pw = ^w;\n"
        "var arr: array[1..3] of w; q: pw; i: integer;\n"
        "begin\n"
        "  for i := 1 to 3 do begin arr[i].s := 1; arr[i].a := i * i end;\n"
        "  writeln(arr[1].a, ' ', arr[3].a);\n"
        "  new(q); q^.s := 2; q^.b := 3.25; writeln(q^.b:0:2); dispose(q)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 9\n3.25\n");
}

TEST(VariantRecord, WholeRecordAssignmentCarriesTheVariantPart) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type t = record k: integer;\n"
        "       case s: integer of 1: (a: integer); 2: (b: real) end;\n"
        "var x, y: t;\n"
        "begin x.k := 1; x.s := 2; x.b := 2.5; y := x;\n"
        "  writeln(y.k, ' ', y.s, ' ', y.b:0:2) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 2 2.50\n");
}

// The constructor built its own name-to-field map from the fixed fields, so a
// tag or variant field named in one was accepted and then dropped.
TEST(VariantRecord, AConstructorFillsTheTagAndVariantFields) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type t = record k: integer;\n"
        "       case s: integer of 1: (a: integer); 2: (b: real) end;\n"
        "var x: t;\n"
        "begin x := t[k: 7; s: 1; a: 9];\n"
        "  writeln(x.k, ' ', x.s, ' ', x.a) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7 1 9\n");
}

TEST(VariantRecord, AnEnumerationDeclaredInsideOneIsInScope) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type t = record case k: (alpha, beta) of\n"
        "       alpha: (c: (red, green));\n"
        "       beta:  (n: integer) end;\n"
        "var v: t;\n"
        "begin v.k := alpha; v.c := green;\n"
        "  writeln(ord(v.k), ' ', ord(v.c)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0 1\n");
}

TEST(CharStringType, GoesWhereAnEPStringIsExpected) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s: packed array[1..5] of char; v: string(20);\n"
        "begin s := 'hello'; v := s;\n"
        "  writeln(v, '!'); writeln(s = 'hello') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "hello!\ntrue\n");
}

// ISO §6.8.2.3: a procedure-statement names a procedure.  A required function
// written as one used to reach codegen, which called a runtime routine that
// does not exist, or one with the wrong arguments and no diagnostic at all.
TEST(RequiredFunction, IsNoStatement) {
    for (const char* Call : {"abs(-1)", "sqr(2)", "trim(s)", "eof"}) {
        auto R = compileAndRun(
            std::string("program p(output);\n"
                        "var s: string(9);\n"
                        "begin s := 'ab'; ") + Call + "; writeln(1) end.\n",
            kEP);
        EXPECT_NE(R.ExitCode, 0) << Call;
        EXPECT_NE(R.Stderr.find("use it in an expression"), std::string::npos)
            << Call << ": " << R.Stderr;
    }
}

// ---------------------------------------------------------------------------
// ISO 7185 6.8.1: a goto may name a label of an enclosing block, abandoning
// every activation between the two.
// ---------------------------------------------------------------------------

TEST(NonLocalGoto, LeavesAProcedureForTheProgramBlock) {
    auto R = compileAndRun(
        "program p(output);\n"
        "label 9999;\n"
        "var i: integer;\n"
        "procedure bail;\n"
        "begin goto 9999 end;\n"
        "begin\n"
        "  for i := 1 to 3 do begin\n"
        "    writeln('iter ', i:1);\n"
        "    if i = 2 then bail\n"
        "  end;\n"
        "  writeln('not reached');\n"
        "9999:\n"
        "  writeln('landed')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "iter 1\niter 2\nlanded\n");
}

TEST(NonLocalGoto, LeavesEveryActivationBetweenItAndTheLabel) {
    // The goto is two procedures down; both are abandoned.
    auto R = compileAndRun(
        "program p(output);\n"
        "label 1;\n"
        "procedure outer;\n"
        "  procedure inner;\n"
        "  begin writeln('inner'); goto 1 end;\n"
        "begin inner; writeln('not reached in outer') end;\n"
        "begin\n"
        "  outer;\n"
        "  writeln('not reached in main');\n"
        "1:\n"
        "  writeln('landed')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "inner\nlanded\n");
}

TEST(NonLocalGoto, ReturnsToTheActivationTheJumpWasMadeFrom) {
    // Each activation of a recursive owner has a block of its own to return
    // to, so the buffer belongs to the frame and not to the procedure.  The
    // locals of the frame landed in must also survive the jump, which is why
    // k is printed: at -O1 and above it had been kept in a register and came
    // back as 0.
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure search(depth: integer);\n"
        "label 1;\n"
        "var k: integer;\n"
        "  procedure check(d: integer);\n"
        "  begin if d = 2 then goto 1 end;\n"
        "begin\n"
        "  k := depth * 10;\n"
        "  if depth < 3 then search(depth + 1);\n"
        "  check(depth);\n"
        "  writeln('fell through at ', depth:1);\n"
        "1:\n"
        "  writeln('left ', depth:1, ' k=', k:1)\n"
        "end;\n"
        "begin search(1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout,
              "fell through at 3\nleft 3 k=30\n"
              "left 2 k=20\n"
              "fell through at 1\nleft 1 k=10\n");
}

TEST(NonLocalGoto, TheLabelMustBeAtTheOutermostLevelOfItsBlock) {
    // 6.8.1: landing inside a structured statement whose activation was just
    // discarded is what the restriction exists to prevent.
    auto R = compileAndRun(
        "program p(output);\n"
        "label 5;\n"
        "var i: integer;\n"
        "procedure q;\n"
        "begin goto 5 end;\n"
        "begin\n"
        "  for i := 1 to 2 do begin 5: writeln(i) end;\n"
        "  q\n"
        "end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted:\n" << R.Stdout;
    EXPECT_NE(R.Stderr.find("goto '5'"), std::string::npos) << R.Stderr;
}

TEST(NonLocalGoto, AGotoOutOfAWithIsStillALocalOne) {
    // A with pushes a scope, so asking the symbol table whether the label was
    // declared "here" answered no and the goto was read as leaving the block.
    auto R = compileAndRun(
        "program p(output);\n"
        "label 3;\n"
        "type r = record a: integer end;\n"
        "var v: r;\n"
        "begin\n"
        "  v.a := 1;\n"
        "  with v do begin if a = 1 then goto 3; writeln('no') end;\n"
        "3:\n"
        "  writeln('yes')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "yes\n");
}

TEST(NonLocalGoto, ALabelledStatementMustBeInTheBlockThatDeclaredTheLabel) {
    // 6.1.6: a label-declaration-part declares the labels of the statements of
    // THAT block.  The placement check resolved the label with the ordinary
    // enclosing-scope lookup, which answers by spelling, so a `1:` written
    // inside a nested procedure satisfied the program block's `label 1` -- and
    // the landing block was then planted in main, where nothing jumps to it and
    // the block ends with no terminator.  It failed as an LLVM verifier ICE.
    // fpc -Miso: 'Label must be defined in the same scope as it is declared'.
    auto R = compileAndRun(
        "program p(output);\n"
        "label 1;\n"
        "procedure q;\n"
        "begin\n"
        "1: writeln('inner')\n"
        "end;\n"
        "begin q end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted:\n" << R.Stdout;
    EXPECT_EQ(R.Stderr.find("internal error"), std::string::npos)
        << "ICE rather than a diagnostic:\n" << R.Stderr;
    EXPECT_NE(R.Stderr.find("enclosing block"), std::string::npos) << R.Stderr;
}

TEST(NestedProcedures, AProcedureMayAssignTheEnclosingFunctionsResult) {
    // 6.8.2.2 says the function block must CONTAIN the assignment, not be it,
    // so a procedure nested inside the function names the result too.  The
    // assignment-target check knew this -- it searches the stack of open
    // functions -- but the check that gives the identifier its type asked only
    // whether the innermost procedure was that function, so the name fell
    // through to the ordinary lookup and was refused as a call with no
    // arguments.  fpc -Miso prints 'total(4) = 14'.
    auto R = compileAndRun(
        "program p(output);\n"
        "function total(k: integer): integer;\n"
        "var n: integer;\n"
        "  procedure setit;\n"
        "  begin n := n * 2; total := n + k end;\n"
        "begin n := k + 1; setit end;\n"
        "begin writeln('total(4) = ', total(4):1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "total(4) = 14\n");
}

TEST(NestedProcedures, RecursionIsStillACallAndNotTheResult) {
    // The guard on the fix above: reaching the enclosing function's result by
    // name must not swallow an ordinary recursive call, nor the inner
    // function's own result when a nested function shares the spelling.
    auto R = compileAndRun(
        "program p(output);\n"
        "function fact(n: integer): integer;\n"
        "begin if n <= 1 then fact := 1 else fact := n * fact(n - 1) end;\n"
        "function g: integer;\n"
        "  procedure h; begin g := 42 end;\n"
        "begin g := 0; h end;\n"
        "begin writeln(fact(5):1, ' ', g:1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "120 42\n");
}

// ---------------------------------------------------------------------------
// Defects the Pascal Acceptance Test found.
// ---------------------------------------------------------------------------

TEST(ForStatement, TheLimitIsReadBeforeTheControlVariableIsAssigned) {
    // 6.8.3.9 evaluates the initial and final values, and only then assigns.
    // Storing 1 first left the limit expression reading it back.
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin\n"
        "  i := 5;\n"
        "  for i := 1 to i do write(i:2);\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, " 1 2 3 4 5\n");
}

TEST(ForStatement, TheLimitIsReadOnlyOnce) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var i, n: integer;\n"
        "begin\n"
        "  n := 3;\n"
        "  for i := 1 to n do begin write(i:2); n := 99 end;\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, " 1 2 3\n");
}

TEST(Ordinals, SuccAndPredKeepTheArgumentsType) {
    // 6.6.6.4.  The arithmetic is done wide and has to come back, or a
    // boolean result is written as 1 and 0.
    auto R = compileAndRun(
        "program p(output);\n"
        "type color = (red, green, blue);\n"
        "begin\n"
        "  writeln(succ(false), ' ', pred(true));\n"
        "  writeln(succ('a'), pred('z'));\n"
        "  writeln(ord(succ(red)):1, ord(pred(blue)):1)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false\nby\n11\n");
}

TEST(StringCompare, TwoConstantsCompareAsStrings) {
    // Neither operand is an array type, so the comparison went to the ordinary
    // operators and compared the addresses of the two constants.
    auto R = compileAndRun(
        "program p(output);\n"
        "begin\n"
        "  writeln('farka' <= 'farkz', ' ', 'farkz' <= 'farks');\n"
        "  writeln('abc' < 'abd', ' ', 'abd' > 'abc');\n"
        "  writeln('abc' = 'abc', ' ', 'abc' <> 'abd')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false\ntrue true\ntrue true\n");
}

TEST(StringConstant, ANamedOneWritesWhatItsLiteralWould) {
    auto R = compileAndRun(
        "program p(output);\n"
        "const s = 'this is a string';\n"
        "begin writeln(s); writeln('this is a string') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "this is a string\nthis is a string\n");
}

TEST(Parameters, APointerPassedByValueArrivesAsThePointer) {
    // A var parameter and a value parameter of pointer type are both `ptr` in
    // the generated signature; taken for the former, the callee was handed the
    // address of the caller's pointer variable.
    auto R = compileAndRun(
        "program p(output);\n"
        "type iptr = ^integer;\n"
        "var ip: iptr;\n"
        "procedure byval(q: iptr);\n"
        "begin writeln(q^:1); new(q); q^ := 1 end;\n"
        "procedure byref(var q: iptr);\n"
        "begin writeln(q^:1); new(q); q^ := 99 end;\n"
        "begin\n"
        "  new(ip); ip^ := 734;\n"
        "  byval(ip); writeln(ip^:1);\n"
        "  byref(ip); writeln(ip^:1)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "734\n734\n734\n99\n");
}

TEST(Parameters, APackedArrayOfCharIsPassedByValue) {
    // 6.6.3.2: the formal is a variable of its own, so the callee's changes
    // stay with the callee.  An array expression is an address, and the value
    // to hand over has to be read from it.
    auto R = compileAndRun(
        "program p(output);\n"
        "type s10 = packed array [1..10] of char;\n"
        "var s: s10;\n"
        "procedure show(t: s10);\n"
        "begin writeln(t); t[1] := 'X'; writeln(t) end;\n"
        "begin\n"
        "  show('literal   ');\n"
        "  s := 'variable  '; show(s); writeln(s)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout,
              "literal   \nXiteral   \nvariable  \nXariable  \nvariable  \n");
}

TEST(FileVariables, AnElementOfAnArrayOfFilesIsAFileVariable) {
    // 6.6.5.2 takes a file-variable, and 6.5 calls an array element one.  Only
    // an identifier was looked for, so rewrite was passed a null pointer.
    auto R = compileAndRun(
        "program p(output);\n"
        "var avf: array [1..3] of text;\n"
        "    i, x: integer;\n"
        "begin\n"
        "  for i := 1 to 3 do begin\n"
        "    rewrite(avf[i]); writeln(avf[i], i + 10) end;\n"
        "  for i := 1 to 3 do begin\n"
        "    reset(avf[i]); readln(avf[i], x); write(x:1, ' ') end;\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "11 12 13 \n");
}

// ---------------------------------------------------------------------------
// Defects the P5 reference output found, which the acceptance test's own
// annotations did not: the program prints what it should be beside each check,
// and where those two disagreed the annotation was the looser of the pair.
// ---------------------------------------------------------------------------

TEST(FieldWidth, AStringTooLongForItsFieldIsTruncated) {
    // 6.9.3.6: the field is exactly TotalWidth characters, so a longer string
    // loses its tail.  A printf width pads but never truncates.
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin for i := 14 downto 1 do writeln('hello, world': i) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout,
              "  hello, world\n hello, world\nhello, world\nhello, worl\n"
              "hello, wor\nhello, wo\nhello, w\nhello, \nhello,\nhello\n"
              "hell\nhel\nhe\nh\n");
}

TEST(FieldWidth, ABooleanIsTruncatedLikeTheStringItIsWrittenAs) {
    // 6.9.3.5 writes a boolean as its char-string would be written.
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin\n"
        "  for i := 6 downto 1 do writeln(true: i);\n"
        "  for i := 6 downto 1 do writeln(false: i)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout,
              "  true\n true\ntrue\ntru\ntr\nt\n"
              " false\nfalse\nfals\nfal\nfa\nf\n");
}

TEST(FieldWidth, AFieldWidthAppliesWhenWritingToAFile) {
    // The width was dropped on the way to the file writer, so the whole string
    // went into a field too small to hold it.
    auto R = compileAndRun(
        "program p(output);\n"
        "type s10 = packed array [1..10] of char;\n"
        "var f: text; s: s10; c: char;\n"
        "begin\n"
        "  s := 'hi there !';\n"
        "  rewrite(f); writeln(f, s:5); writeln(f, s:12);\n"
        "  reset(f);\n"
        "  while not eof(f) do begin\n"
        "    if eoln(f) then begin readln(f); writeln end\n"
        "    else begin read(f, c); write(c) end\n"
        "  end\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "hi th\n  hi there !\n");
}

TEST(TextFiles, ReadingAtALineMarkerGivesASpace) {
    // 6.4.3.5: the line marker separates lines rather than belonging to one,
    // and f^ is a space wherever one stands.  Reading gave the newline itself.
    auto R = compileAndRun(
        "program p(output);\n"
        "var f: text; c: char;\n"
        "begin\n"
        "  rewrite(f); writeln(f, 'how now'); writeln(f, 'brown cow');\n"
        "  reset(f); write('''');\n"
        "  while not eof(f) do begin\n"
        "    if eoln(f) then write('<eoln>');\n"
        "    read(f, c); write(c)\n"
        "  end;\n"
        "  writeln('''')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "'how now<eoln> brown cow<eoln> '\n");
}

TEST(TextFiles, TheBufferVariableAtALineMarkerIsASpace) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var f: text; c: char;\n"
        "begin\n"
        "  rewrite(f); writeln(f, 'ab');\n"
        "  reset(f); write('''');\n"
        "  while not eof(f) do begin c := f^; get(f); write(c) end;\n"
        "  writeln('''')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "'ab '\n");
}

TEST(TextFiles, AOneByteBinaryFileKeepsAComponentWorthTen) {
    // The space rule is about text.  On a file of a one-byte type the same
    // byte is a component with the value 10 and has to arrive intact, and
    // component size alone does not tell the two apart.
    auto R = compileAndRun(
        "program p(output);\n"
        "type small = 0..255;\n"
        "var f: file of small; v: small; i: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  for i := 8 to 12 do begin f^ := i; put(f) end;\n"
        "  reset(f);\n"
        "  while not eof(f) do begin v := f^; get(f); write(v:1, ' ') end;\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "8 9 10 11 12 \n");
}

TEST(TextFiles, TheLastLineIsTerminatedEvenWithoutAWriteln) {
    // 6.4.3.5 makes a text file a sequence of lines, each ended by a marker,
    // so a file built with write alone still reads back as complete lines.
    auto R = compileAndRun(
        "program p(output);\n"
        "var f: text; c: char;\n"
        "begin\n"
        "  rewrite(f); writeln(f, 'too much'); write(f, 'too soon');\n"
        "  reset(f); write('''');\n"
        "  while not eof(f) do begin\n"
        "    if eoln(f) then write('<eoln>');\n"
        "    read(f, c); write(c)\n"
        "  end;\n"
        "  writeln('''')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "'too much<eoln> too soon<eoln> '\n");
}

TEST(TextFiles, EofIsTrueOnAFileBeingWritten) {
    // 6.6.5.2: rewrite(f) leaves eof(f) true, and writing happens at the end
    // of the file, so it stays true for as long as f is being generated.
    auto R = compileAndRun(
        "program p(output);\n"
        "var f: text;\n"
        "begin\n"
        "  rewrite(f); writeln(eof(f));\n"
        "  writeln(f, 'x'); writeln(eof(f));\n"
        "  reset(f); writeln(eof(f))\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "true\ntrue\nfalse\n");
}

// ---------------------------------------------------------------------------
// Requirements neither the rejection suite nor the acceptance test covers: a
// violation of a "shall" that no test program happened to be written around.
// ---------------------------------------------------------------------------

TEST(FileTypes, AFileVariableCannotBeAssigned) {
    // 6.8.2.2: a file names something outside the program that the variable is
    // a window onto, and copying the window has no meaning.
    auto R = compileAndRun(
        "program p(output);\n"
        "var f, g: text;\n"
        "begin f := g end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted";
    EXPECT_NE(R.Stderr.find("file type"), std::string::npos) << R.Stderr;
}

TEST(FileTypes, AStructureHoldingAFileCannotBeAssigned) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type holder = record f: text; n: integer end;\n"
        "var a, b: holder;\n"
        "begin a := b end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted";
    EXPECT_NE(R.Stderr.find("file"), std::string::npos) << R.Stderr;
}

TEST(FileTypes, AnArrayOfFilesCannotBeAssigned) {
    // An array of files holds files as surely as a record of them does, and
    // the rule was looking only inside records.
    auto R = compileAndRun(
        "program p(output);\n"
        "type fa = array [1..2] of text;\n"
        "var a, b: fa;\n"
        "begin a := b end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted";
    EXPECT_NE(R.Stderr.find("file"), std::string::npos) << R.Stderr;
}

TEST(CaseStatement, TheLabelsMustBeDistinct) {
    // 6.8.3.5.  Two arms holding the same value give the statement two
    // meanings, and which one runs would be an accident of code generation.
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin case i of 1: writeln(1); 1: writeln(2) end end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted";
    EXPECT_NE(R.Stderr.find("more than one arm"), std::string::npos) << R.Stderr;
}

TEST(CaseStatement, ARangeCollidingWithALabelIsCaught) {
    // A range as a case-constant is EP's (§6.9.3.5), and it stands for every
    // value in it, any of which another arm can repeat.
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin case i of 1..5: writeln(1); 3: writeln(2) end end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0) << "accepted";
    EXPECT_NE(R.Stderr.find("more than one arm"), std::string::npos) << R.Stderr;
}

TEST(CaseStatement, ADuplicateIsNamedAsItWasWritten) {
    // The ordinal of a char is not what the reader put in the program.
    auto R = compileAndRun(
        "program p(output);\n"
        "var c: char;\n"
        "begin case c of 'a': writeln(1); 'a': writeln(2) end end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted";
    EXPECT_NE(R.Stderr.find("case label 'a'"), std::string::npos) << R.Stderr;
}

TEST(CaseStatement, DistinctLabelsAreStillAccepted) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin i := 4;\n"
        "  case i of 1..3: writeln('low'); 4..6: writeln('mid') end\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "mid\n");
}

TEST(FunctionResult, MustBeSimpleOrPointerUnderIso7185) {
    // 6.6.2 admits a simple-type or a pointer-type and nothing else.
    auto R = compileAndRun(
        "program p(output);\n"
        "type arr = array [1..3] of integer;\n"
        "var v: arr;\n"
        "function q: arr; begin q := v end;\n"
        "begin end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted";
    EXPECT_NE(R.Stderr.find("simple or pointer"), std::string::npos) << R.Stderr;
}

TEST(FunctionResult, ExtendedPascalAllowsAStructuredOne) {
    // EP §6.6.2 lifts the restriction to anything a value can be assigned
    // from, which leaves out only the file types.
    auto R = compileAndRun(
        "program p(output);\n"
        "type arr = array [1..3] of integer;\n"
        "var v: arr; i: integer;\n"
        "function q: arr; begin q := v end;\n"
        "begin\n"
        "  v[1] := 7; v[2] := 8; v[3] := 9;\n"
        "  v := q;\n"
        "  for i := 1 to 3 do write(v[i]:2); writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, " 7 8 9\n");
}

TEST(FunctionResult, AFileResultIsRefusedInBothStandards) {
    for (const std::string Std : {std::string("-std=iso7185"), std::string(kEP)}) {
        auto R = compileAndRun(
            "program p(output);\n"
            "function q: text; begin end;\n"
            "begin end.\n", Std);
        EXPECT_NE(R.ExitCode, 0) << "accepted under " << Std;
    }
}

TEST(VariantPart, TheCaseConstantsMustBeDistinct) {
    // 6.4.3.3, for the reason 6.8.3.5 has: the tag value has to name one
    // variant and not two.
    auto R = compileAndRun(
        "program p(output);\n"
        "type r = record case t: integer of\n"
        "            1: (a: integer);\n"
        "            1: (b: char)\n"
        "         end;\n"
        "var v: r;\n"
        "begin end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted";
    EXPECT_NE(R.Stderr.find("more than one variant"), std::string::npos) << R.Stderr;
}

TEST(TextOnlyProcedures, WritelnPageAndEolnRefuseANonTextFile) {
    // 6.9.5, 6.9.4 and 6.6.6.5: all three are about lines, and only a text
    // file has any.  readln was checked and the other three were not.
    struct Case { const char* body; const char* want; };
    const Case Cases[] = {
        {"rewrite(f); writeln(f)",              "'writeln'"},
        {"rewrite(f); page(f)",                 "'page'"},
        {"reset(f); if eoln(f) then writeln(1)","'eoln'"},
        {"reset(f); readln(f, x)",              "'readln'"},
    };
    for (const auto& C : Cases) {
        auto R = compileAndRun(
            std::string("program p(output);\n"
                        "var f: file of integer; x: integer;\n"
                        "begin ") + C.body + " end.\n");
        EXPECT_NE(R.ExitCode, 0) << "accepted: " << C.body;
        EXPECT_NE(R.Stderr.find(C.want), std::string::npos)
            << C.body << "\n" << R.Stderr;
    }
}

TEST(TextOnlyProcedures, AFileOfCharIsAText) {
    // 6.4.3.5 makes a file of char a text file, so all four apply to one.
    auto R = compileAndRun(
        "program p(output);\n"
        "var g: file of char;\n"
        "begin\n"
        "  rewrite(g); writeln(g); page(g);\n"
        "  reset(g); if eoln(g) then writeln('eoln')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
}

// ---------------------------------------------------------------------------
// ISO §6.9.1: read(v) reads into v, and only into v
//
// The reader was chosen, and the width to store, from a lookup of the
// argument's *name*.  Only an identifier has one, so `read(a[i])`,
// `read(r.f)` and `read(p^)` fell through to a default of i64: reading into a
// char component picked the integer reader and stored eight bytes into one.
//
// Nothing in the suite caught it.  The 377 conformance cases and the
// acceptance test read into named variables, and the IR of all 181 modules
// they produce is unchanged by the fix -- which is exactly how it survived
// from before 0.1.3.
// ---------------------------------------------------------------------------

TEST(ReadTarget, ReadingIntoAnArrayElementTouchesOnlyThatElement) {
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var s: array[1..4] of char; i: integer;\n"
        "begin\n"
        "  for i := 1 to 4 do s[i] := 'Z';\n"
        "  read(s[1]);\n"
        "  for i := 1 to 4 do write(s[i]);\n"
        "  writeln\n"
        "end.\n", "", "5\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // Eight bytes went in here, so the whole array and four bytes past its end
    // were overwritten and this printed "5" followed by three NULs.
    EXPECT_EQ(R.Stdout, "5ZZZ\n");
}

TEST(ReadTarget, ReadingIntoARecordFieldTouchesOnlyThatField) {
    auto R = compileAndRun(
        "program p(input, output);\n"
        "type r = record a, b, c, d: char end;\n"
        "var v: r;\n"
        "begin\n"
        "  v.a := 'Z'; v.b := 'Z'; v.c := 'Z'; v.d := 'Z';\n"
        "  read(v.a);\n"
        "  writeln(v.a, v.b, v.c, v.d)\n"
        "end.\n", "", "5\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5ZZZ\n");
}

TEST(ReadTarget, ReadingThroughAPointerReadsTheRightWidth) {
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var pc: ^char;\n"
        "begin new(pc); pc^ := 'Z'; read(pc^); writeln(pc^) end.\n", "", "5\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n");
}

TEST(ReadTarget, ANamedVariableIsStillReadCorrectly) {
    // The path that always worked, so that fixing the others cannot break it.
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var c: char; i: integer;\n"
        "begin read(c); readln; read(i); writeln(c); writeln(i) end.\n",
        "", "5\n42\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n42\n");
}

TEST(ReadTarget, ATypedFileComponentIsNotWiderThanWhatItIsReadInto) {
    // A subrange of char is stored as a full ordinal, so `file of 'a'..'z'` has
    // an eight-byte component while an `array of char` has one-byte elements.
    // The byte count came from the component whether or not a temporary was
    // used, so the whole component went into the element.
    auto R = compileAndRun(
        "program p(output);\n"
        "type letter = 'a'..'z';\n"
        "var f: file of letter; s: array[1..4] of char; i: integer;\n"
        "begin\n"
        "  for i := 1 to 4 do s[i] := 'Z';\n"
        "  rewrite(f, 'rt.bin'); write(f, 'q'); close(f);\n"
        "  reset(f, 'rt.bin'); read(f, s[1]); close(f);\n"
        "  for i := 1 to 4 do write(s[i]);\n"
        "  writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "qZZZ\n");
}

// ---------------------------------------------------------------------------
// A type recovered by name is not the type
//
// Four defects with one shape between them: codegen worked out what something
// was by looking a NAME up in a table it maintains itself, where Sema already
// had the answer.  The tables are per-procedure and one hop deep, so a second
// alias, a shadowing declaration or a variant part gave the wrong type
// silently.  All four are plain ISO 7185 and all four predate 0.1.3.
// ---------------------------------------------------------------------------

TEST(TypeByName, AnArrayReachedThroughTwoAliasesKeepsItsLowerBound) {
    // The array's *type* was resolved through the whole chain of names while
    // the lower bound was read with a single hop, so the bound stayed 0: the
    // index was never adjusted and the check ran against 0..n-1.  A legal x[6]
    // aborted, and with the checks off the writes landed past the array.
    auto R = compileAndRun(
        "program p(output);\n"
        "type row = array[5..10] of integer;\n"
        "     rowalias = row;\n"
        "var guard1: integer; x: rowalias; guard2: integer; i: integer;\n"
        "begin\n"
        "  guard1 := 111; guard2 := 222;\n"
        "  for i := 5 to 10 do x[i] := i;\n"
        "  writeln(x[5], ' ', x[10], ' ', guard1, ' ', guard2)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5 10 111 222\n");
}

TEST(TypeByName, AnArrayThroughTwoAliasesIsUncorruptedWithoutChecks) {
    // The same program with the bounds checks off, which is where it stopped
    // being a diagnostic and became a write into the next variable.
    auto R = compileAndRun(
        "program p(output);\n"
        "type row = array[5..10] of integer;\n"
        "     rowalias = row;\n"
        "var guard1: integer; x: rowalias; guard2: integer; i: integer;\n"
        "begin\n"
        "  guard1 := 111; guard2 := 222;\n"
        "  for i := 5 to 10 do x[i] := i;\n"
        "  writeln(guard1, ' ', guard2)\n"
        "end.\n", "-fno-range-checks");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "111 222\n");
}

TEST(TypeByName, APointerKeepsItsOwnRecordWhenTheNameIsShadowed) {
    // The domain type was resolved by name through a table codegen re-points
    // per procedure, so a nested procedure declaring its own `rec` re-aimed
    // every p^.f in its body at the inner layout -- with the field index still
    // taken from the right record, so it read an unrelated offset.
    auto R = compileAndRun(
        "program p(output);\n"
        "type rec = record a: integer; b: integer end;\n"
        "var ptr: ^rec;\n"
        "procedure q;\n"
        "type rec = record x: char; y: char; z: integer end;\n"
        "var l: rec;\n"
        "begin l.x := 'a'; writeln(ptr^.b) end;\n"
        "begin new(ptr); ptr^.a := 11; ptr^.b := 22; writeln(ptr^.b); q end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "22\n22\n");
}

TEST(TypeByName, WithBindsAVariantFieldToItsOwnStorage) {
    // §6.4.3.3 lets a variant field be selected by name like any other, so
    // Sema's field list is flattened while the struct holds one blob for all
    // the alternatives.  Pairing them positionally bound the first variant
    // field to the blob: `with r do c := 4` stored an integer bit pattern into
    // a real and printed 1.97626258336499e-323.
    auto R = compileAndRun(
        "program p(output);\n"
        "type num = record kind: integer;\n"
        "       case tag: integer of 1: (c: real); 2: (e: integer) end;\n"
        "var r: num;\n"
        "begin r.kind := 1; r.tag := 1; with r do c := 4; writeln(r.c:5:1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "  4.0\n");
}

TEST(TypeByName, WithBindsEveryVariantFieldNotJustTheFirst) {
    // The later ones ran off the end of the struct and were never bound, so
    // `with r do b := 22` referred to a `pasg_b` nothing defined and the link
    // failed -- or found some module's exported variable of that name.
    auto R = compileAndRun(
        "program p(output);\n"
        "type num = record kind: integer;\n"
        "       case tag: integer of 1: (a: integer; b: integer); 2: (e: real) end;\n"
        "var r: num;\n"
        "begin r.kind := 1; r.tag := 1;\n"
        "  with r do begin a := 11; b := 22 end;\n"
        "  with r do writeln(a, ' ', b)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11 22\n");
}

TEST(ValueParameter, AnIntegerActualWidensForARealFormal) {
    // §6.6.3.2 makes a value parameter a variable the actual is *assigned* to,
    // so §6.4.6 applies and an integer widens.  Nothing coerced it, so
    // `scale(3)` emitted `call void @pas_scale(i64 3)` against a `void (double)`
    // and the module failed verification: a program could not use a real
    // parameter without writing every actual as a real.
    auto R = compileAndRun(
        "program p(output);\n"
        "var n: integer;\n"
        "procedure scale(x: real); begin writeln(x:6:2) end;\n"
        "function half(x: real): real; begin half := x / 2 end;\n"
        "begin n := 3; scale(3); scale(n); writeln(half(7):6:2) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "  3.00\n  3.00\n  3.50\n");
}

// ---------------------------------------------------------------------------
// A name is not an identity, part two
//
// Six more of the shape 0.1.4 fixed seven of.  Found by re-sweeping after
// those, which is the argument for re-sweeping: three of these are the same
// defect on a path the earlier fix did not cover.
// ---------------------------------------------------------------------------

TEST(TypeByName, AFieldOfAnArrayElementKeepsItsRecordWhenTheNameIsShadowed) {
    // resolveRecordStructType's last case looked the Sema type's NAME up in a
    // table rebuilt per procedure.  The p^.field branch was fixed; this is the
    // path it did not cover -- a field of an array element, of a nested field,
    // or of a function result.
    auto R = compileAndRun(
        "program p(output);\n"
        "type rec = record a, b, c: integer end;\n"
        "var arr: array[1..2] of rec;\n"
        "procedure q;\n"
        "type rec = record x, y, z: char end;\n"
        "var l: rec;\n"
        "begin l.x := 'a'; writeln(arr[1].a, ' ', arr[1].b, ' ', arr[1].c) end;\n"
        "begin arr[1].a := 11; arr[1].b := 22; arr[1].c := 33;\n"
        "  writeln(arr[1].a, ' ', arr[1].b, ' ', arr[1].c); q end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11 22 33\n11 22 33\n");
}

TEST(TypeByName, AWholeRecordReadThroughAPointerIsNotTheShadowingOne) {
    // emitDerefLoad picked the load type by the same name lookup, so p^ as a
    // whole value was loaded as the inner record: a { i8 } read from a
    // three-integer record and stored back over it left 11 0 0.
    auto R = compileAndRun(
        "program p(output);\n"
        "type rec = record a, b, c: integer end;\n"
        "var src: ^rec; dst: rec;\n"
        "procedure q;\n"
        "type rec = record z: char end;\n"
        "var l: rec;\n"
        "begin l.z := 'q'; dst := src^ end;\n"
        "begin new(src); src^.a := 11; src^.b := 22; src^.c := 33;\n"
        "  q; writeln(dst.a, ' ', dst.b, ' ', dst.c) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11 22 33\n");
}

TEST(TypeByName, AWithBoundFieldDoesNotBecomeTheEnclosingFunctionsResult) {
    // ISO §6.8.3.10 makes a with-statement's field designators an inner scope,
    // so a field spelled like the enclosing function denotes the FIELD.  The
    // function-result pseudo-variable was checked before the variable table
    // and won, so the store went to the result and left the field alone.
    auto R = compileAndRun(
        "program p(output);\n"
        "type rec = record count: integer end;\n"
        "var r: rec; k: integer;\n"
        "function count: integer;\n"
        "begin count := 1; with r do count := 99 end;\n"
        "begin r.count := 0; k := count;\n"
        "  writeln(k, ' ', r.count) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 99\n");
}

TEST(TypeByName, AGlobalOfTheFunctionsOwnNameIsStillShadowedByTheResult) {
    // The other side of that.  Inside function `total`, the identifier denotes
    // the result even though a global of that name exists -- so the fix cannot
    // simply consult the variable table first.
    auto R = compileAndRun(
        "program p(output);\n"
        "var total: integer;\n"
        "function f: integer;\n"
        "begin f := 7 end;\n"
        "begin total := 3; writeln(f, ' ', total) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7 3\n");
}

TEST(NestedProcedure, ASiblingCallReachesTheEnclosingActivationsVariable) {
    // The static-link frame resolved each captured variable by NAME in the
    // CALLER's scope.  With `b` and `c` both nested in `a`, and `c` declaring
    // its own `n`, `c` calling `b` handed `b` the address of c's n: b's
    // increment landed in c's private local and a's n never moved.
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure a;\n"
        "var n: integer;\n"
        "  procedure b; begin n := n + 1 end;\n"
        "  procedure c;\n"
        "  var n: integer;\n"
        "    procedure d; begin n := n + 100 end;\n"
        "  begin n := 500; b; d; writeln('c ', n) end;\n"
        "  procedure e; begin b end;\n"
        "begin n := 10; b; c; e; writeln('a ', n) end;\n"
        "begin a end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // c's own d must still reach c's n; b must always reach a's n.
    EXPECT_EQ(R.Stdout, "c 600\na 13\n");
}

TEST(ReadTarget, ASubrangeOfCharIsReadAsACharacter) {
    // The reader was chosen from the LLVM type, and a subrange of char is held
    // in a full ordinal -- so this called the INTEGER reader, tried to parse a
    // number out of "xy", found none, and left both variables untouched.
    // fpc -Miso reads x and y here.
    auto R = compileAndRun(
        "program p(input, output);\n"
        "type letter = 'a'..'z';\n"
        "var c1, c2: letter; n: integer;\n"
        "begin read(c1, c2); readln(n); writeln(c1, c2, ' ', n) end.\n",
        "", "xy\n7\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "xy 7\n");
}

TEST(ReadTarget, ATypeTheStandardDoesNotReadIntoIsRefused) {
    // ISO §6.9.2 reads into an integer, a real or a char variable.  Anything
    // else was accepted and handed to whichever runtime reader its storage
    // width selected: a boolean took the integer reader.
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var b: boolean;\n"
        "begin read(b) end.\n", "", "1\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("cannot be read into"), std::string::npos) << R.Stderr;
}

TEST(ReadTarget, ATypedFileStillReadsAWholeComponent) {
    // §6.9.1's read on a typed file reads a component of the file's own type,
    // whatever that is -- so the textfile rule must not reach it.
    auto R = compileAndRun(
        "program p(output);\n"
        "type rec = record a, b: integer end;\n"
        "var f: file of rec; r: rec;\n"
        "begin\n"
        "  rewrite(f, 'tf.bin'); r.a := 1; r.b := 2; write(f, r); close(f);\n"
        "  reset(f, 'tf.bin'); r.a := 0; r.b := 0; read(f, r); close(f);\n"
        "  writeln(r.a, ' ', r.b)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 2\n");
}

TEST(TypeByName, AnArrayKeepsItsBoundWhenAnInnerScopeRedeclaresTheTypeName) {
    // The bound was read from the declaration through a table rebuilt per
    // procedure and answered by spelling, so a nested procedure declaring its
    // own `t` handed an outer `a: array[0..4]` the inner t's bound of ten:
    // a[0] wrote ten elements before the array, and the range check passed
    // because it was checked against 10..14 too.  Making Sema a fallback for a
    // ZERO bound did not reach this -- the wrong bound was ten.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t = array[0..4] of integer;\n"
        "var a: t; guard: integer; i: integer;\n"
        "procedure q;\n"
        "type t = array[10..14] of integer;\n"
        "var l: t;\n"
        "begin l[10] := 1; a[0] := 42; a[4] := 44 end;\n"
        "begin guard := 7; for i := 0 to 4 do a[i] := 0; q;\n"
        "  writeln(a[0], ' ', a[4], ' ', guard) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42 44 7\n");
}

TEST(TypeByName, AnAliasToARecordSurvivesAShadowingDeclaration) {
    // llvmTypeOfNode resolved a named type through a table rebuilt per
    // procedure and answered by spelling, so a procedure declaring its own `t`
    // re-aimed an outer variable declared through an alias to the outer `t`.
    // The size-agreement check turned it into an internal compiler error --
    // "type 't' takes 1 bytes as it is written and 24 bytes as Sema resolved
    // it" -- on a program fpc -Miso compiles and runs.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t = record a, b, c: integer end;\n"
        "     outert = t;\n"
        "     pt = ^t;\n"
        "var g: pt;\n"
        "procedure inner(q: pt);\n"
        "type t = record ch: char end;\n"
        "var local: t; r: outert;\n"
        "begin local.ch := 'z'; r.a := 1; r.b := 2; r.c := 3;\n"
        "  writeln(local.ch, ' ', r.a, ' ', r.b, ' ', r.c) end;\n"
        "begin new(g); inner(g) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "z 1 2 3\n");
}

TEST(NestedProcedure, AGrandparentsVariableIsNotConfusedWithAParentsOfTheSameName) {
    // A frame slot is for a particular VARIABLE, and a name does not name one:
    // two nesting levels may declare the same one.  Filling the slots by name
    // gave both the innermost binding, so the outer variable never travelled --
    // `d`, which captures b's n, was handed e's n and read 100 for 42.
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure b;\n"
        "var n: integer;\n"
        "  procedure d; begin writeln(n) end;\n"
        "  procedure e;\n"
        "  var n: integer;\n"
        "    procedure f; begin d end;\n"
        "  begin n := 100; f end;\n"
        "begin n := 42; e end;\n"
        "begin b end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(NestedProcedure, ABodyReadsTheNearestEnclosingVariableOfThatName) {
    // The other half.  Every captured slot is loaded, because an outer one may
    // have to travel on to a callee that captured it -- but only the innermost
    // takes the NAME here, or a grandparent's variable answers to it and the
    // body reads straight past its parent's.
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure a;\n"
        "var n: integer;\n"
        "  procedure c;\n"
        "  var n: integer;\n"
        "    procedure d; begin n := n + 100 end;\n"
        "  begin n := 500; d; writeln(n) end;\n"
        "begin n := 10; c; writeln(n) end;\n"
        "begin a end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // d must bump c's n, not a's.
    EXPECT_EQ(R.Stdout, "600\n10\n");
}

TEST(NestedProcedure, ACallFromInsideAWithReachesTheEnclosingVariable) {
    // A frame slot the caller itself declares was resolved with findVar, which
    // starts at the innermost scope -- and a with-statement opens one.  Calling
    // a nested procedure from inside `with r do`, where r has a field spelled
    // like the captured variable, handed it the address of the FIELD, so the
    // increments meant for the enclosing variable landed in the record.
    auto R = compileAndRun(
        "program p(output);\n"
        "type rec = record n: integer end;\n"
        "var r: rec;\n"
        "procedure outer;\n"
        "var n: integer;\n"
        "  procedure bump; begin n := n + 1 end;\n"
        "begin\n"
        "  n := 5; r.n := 900;\n"
        "  with r do begin bump; bump end;\n"
        "  writeln(n, ' ', r.n)\n"
        "end;\n"
        "begin outer end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7 900\n");
}

TEST(Shadowing, AVariableHidesAConstantOfTheSameName) {
    // ISO §6.2.2.1: an identifier denotes its innermost enclosing definition.
    // The constant table is flat and has no idea which is nearer, so it
    // answered every read while the writes went to the variable: `size := 42`
    // stored 42 and `writeln(size)` printed 10.  A required constant was
    // already handled this way; every constant the program declares needed it.
    auto R = compileAndRun(
        "program p(output);\n"
        "const size = 10;\n"
        "procedure q;\n"
        "var size: integer;\n"
        "begin size := 42; writeln(size) end;\n"
        "begin writeln(size); q; writeln(size) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // And the constant is itself again once the scope that hid it ends.
    EXPECT_EQ(R.Stdout, "10\n42\n10\n");
}

TEST(Shadowing, EveryKindOfBindingHidesAConstant) {
    // A value parameter, a for-loop control variable and a with-bound field
    // are all nearer definitions of the name.  fpc -Miso agrees on each.
    auto R = compileAndRun(
        "program p(output);\n"
        "const size = 10; red = 7;\n"
        "procedure byparam(size: integer); begin write(size, ' ') end;\n"
        "procedure byenum; var red: integer; begin red := 3; write(red, ' ') end;\n"
        "procedure byfor; var size: integer;\n"
        "begin for size := 1 to 3 do write(size); write(' ') end;\n"
        "procedure bywith;\n"
        "type r = record size: integer end;\n"
        "var rr: r;\n"
        "begin rr.size := 99; with rr do write(size) end;\n"
        "begin byparam(5); byenum; byfor; bywith; writeln end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5 3 123 99\n");
}

TEST(Shadowing, AnInnerTypeOfTheSameNameDoesNotResizeAnOuterVariable) {
    // ISO §6.2.2.1: a name denotes what the innermost enclosing declaration of
    // it says, judged where the name is WRITTEN.  Codegen resolved type names
    // through a flat table keyed by spelling and rebuilt per procedure, so
    // inside `inner` the outer g's domain type was re-read as inner's `t`.
    //
    // new(g) then allocated two elements for a ten-element array, and the
    // writes that followed ran off the end of the block: the program aborted
    // inside glibc with a corrupted heap.  Plain ISO 7185 -- no schema, no
    // extension, nothing exotic.
    //
    // The size-agreement check could not see it: both readings are ordinary
    // array types, and it compares a denoter against Sema only where BOTH can
    // answer, which is exactly what the spelling table had already decided.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t = array[1..10] of integer;\n"
        "var g: ^t; i: integer;\n"
        "procedure inner;\n"
        "type t = array[1..2] of integer;\n"
        "var q: ^t;\n"
        "begin new(q); q^[1] := 0; new(g) end;\n"
        "begin\n"
        "  inner;\n"
        "  for i := 1 to 10 do g^[i] := i * 3;\n"
        "  for i := 1 to 10 do write(g^[i]:1, ' ');\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3 6 9 12 15 18 21 24 27 30 \n");
}

TEST(Shadowing, AnInnerTypeOfTheSameNameDoesNotResizeAValueOfIt) {
    // NOT a test of the change that added it: this passes without it.  A
    // RECORD named type was already special-cased to consult Sema, which is
    // why only the array shape above failed -- and that special case is one of
    // the two the general rule replaces.  It is here so that deleting them
    // cannot quietly take this behaviour with it.
    auto R = compileAndRun(
        "program p(output);\n"
        "type r = record a, b, c: integer end;\n"
        "var g: r;\n"
        "procedure inner;\n"
        "type r = record a: integer end;\n"
        "var l: r;\n"
        "begin l.a := 1; g.a := 11; g.b := 22; g.c := 33 end;\n"
        "begin inner; writeln(g.a:1, ' ', g.b:1, ' ', g.c:1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11 22 33\n");
}

TEST(Shadowing, AFileElementIsSizedByTheTypeItWasDeclaredWith) {
    // getFileElemType reads the file variable's recorded denoter -- a node
    // written where the VARIABLE was declared -- and lowers its element.  When
    // that element is a type name and the lowering answered by spelling, a
    // procedure redeclaring the name resized the file's component: `f^ := x`
    // wrote two integers where ten were declared, and the program aborted
    // inside glibc with a corrupted heap.
    //
    // A record element never showed it, because a record named type was
    // already special-cased to consult Sema.  It took an array to see, which
    // is the same reason the general rule had to replace those special cases
    // rather than gain a third.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t = array[1..10] of integer;\n"
        "var f: file of t; x, y: t; i: integer;\n"
        "procedure inner;\n"
        "type t = array[1..2] of integer;\n"
        "var l: t; i: integer;\n"
        "begin\n"
        "  l[1] := 0;\n"
        "  rewrite(f);\n"
        "  for i := 1 to 10 do x[i] := i * 5;\n"
        "  f^ := x; put(f)\n"
        "end;\n"
        "begin\n"
        "  inner;\n"
        "  reset(f); y := f^;\n"
        "  for i := 1 to 10 do write(y[i]:1, ' ');\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5 10 15 20 25 30 35 40 45 50 \n");
}

TEST(Shadowing, ARecordLayoutIsFoldedInTheScopeItWasDeclaredIn) {
    // `arrayIndexRange` folded a field's bounds against codegen's constant
    // table, which holds whatever is innermost where the denoter is being
    // LOWERED rather than where it was written.  A record whose layout is first
    // computed inside a procedure declaring its own `n` was sized for the
    // stranger's n -- and `recordLayouts` memoises on the declaration node, so
    // that wrong layout then served every later use of the type.
    //
    // The declaration order is the whole test: with a global variable of `r`
    // the layout is computed at file scope first and the memo is correct, which
    // is why this needs a type used ONLY from procedures.
    auto R = compileAndRun(
        "program p(output);\n"
        "const n = 10;\n"
        "type r = record a: array[1..n] of integer; tail: integer end;\n"
        "procedure inner;\n"
        "const n = 2;\n"
        "var l: r;\n"
        "begin l.a[1] := 0 end;\n"
        "procedure later;\n"
        "var m: r; i: integer;\n"
        "begin\n"
        "  m.tail := 999;\n"
        "  for i := 1 to 10 do m.a[i] := i;\n"
        "  writeln(m.a[10]:1, ' ', m.tail:1)\n"
        "end;\n"
        "begin inner; later end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10 999\n");
}

TEST(Shadowing, AUserDeclaredEofMeansTheUsersOwn) {
    // ISO §6.2.2.10: a program that declares one of the required names means
    // its own.  The guard routing a bare `eof` to the runtime tested findVar
    // and the constant table -- two of the several things a name can denote --
    // so a parameterless FUNCTION called eof was in neither and the builtin
    // won.
    //
    // Worse than a wrong answer: the builtin reads standard input, so a
    // program whose own eof never touches a file hangs on a terminal.  This
    // test therefore also stands as a hang regression; it is why compileAndRun
    // closing stdin is not incidental here.
    auto R = compileAndRun(
        "program p(output);\n"
        "function eof: boolean;\n"
        "begin eof := false end;\n"
        "begin\n"
        "  if eof then writeln('builtin won') else writeln('user function won')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "user function won\n");
}

TEST(CaseStatement, ALabelMustBeAConstant) {
    // ISO §6.8.3.5: a case-label is a case-CONSTANT.  Sema folded labels only
    // to find duplicates and skipped quietly when a label would not fold, so
    // one that was not constant reached codegen and lowered to a LOAD of the
    // variable -- `case i of 1..n:` compared the selector against whatever n
    // held at that moment, and the illegal program compiled into a
    // plausible-looking one that even produced the "right" answer here.
    auto Bad = compileAndRun(
        "program p(output);\n"
        "var i, n: integer;\n"
        "begin\n"
        "  n := 3; i := 2;\n"
        "  case i of\n"
        "    1..n: writeln('in range');\n"
        "    otherwise writeln('out')\n"
        "  end\n"
        "end.\n", kEP);
    EXPECT_NE(Bad.ExitCode, 0);
    EXPECT_NE(Bad.Stderr.find("not a constant"), std::string::npos) << Bad.Stderr;

    // A constant range is still a range; the diagnostic must not cost the
    // feature it is protecting.
    auto Ok = compileAndRun(
        "program p(output);\n"
        "const hi = 3;\n"
        "var i: integer;\n"
        "begin\n"
        "  i := 2;\n"
        "  case i of\n"
        "    1..hi: writeln('in range');\n"
        "    otherwise writeln('out')\n"
        "  end\n"
        "end.\n", kEP);
    ASSERT_EQ(Ok.ExitCode, 0) << Ok.Stderr;
    EXPECT_EQ(Ok.Stdout, "in range\n");
}
