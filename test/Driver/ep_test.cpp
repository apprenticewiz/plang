/// ep_test.cpp — Extended Pascal, end to end
///
/// The ISO 10206 extensions, tier by tier: strings and their operations,
/// complex numbers, direct-access files, schema types, structured value
/// constructors, date and time, binding, restricted types, `for ... in` and
/// `type of`.  Modules are the other half of Extended Pascal and are in
/// module_test.cpp, which needs a different harness for them.

#include "DriverHarness.h"

#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// Tier 4: EP string(N) compile+run regression tests
// ---------------------------------------------------------------------------

TEST(Tier4String, AssignAndWrite) {
    auto R = compileAndRun(
        "program p; var s: string(20);\n"
        "begin s := 'Hello'; writeln(s) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "Hello\n");
}

TEST(Tier4String, CharToString) {
    auto R = compileAndRun(
        "program p; var s: string(10); c: char;\n"
        "begin c := '!'; s := c; writeln(s) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "!\n");
}

TEST(Tier4String, Concatenation) {
    auto R = compileAndRun(
        "program p; var s: string(20); u: string(40);\n"
        "begin s := 'Hello'; u := s + ', World'; writeln(u) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "Hello, World\n");
}

TEST(Tier4String, Length) {
    auto R = compileAndRun(
        "program p; var s: string(20); n: integer;\n"
        "begin s := 'Hello'; n := length(s); writeln(n) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n");
}

TEST(Tier4String, Equality) {
    auto R = compileAndRun(
        "program p; var s: string(20); b: boolean;\n"
        "begin s := 'Hello'; b := s = 'Hello'; writeln(b) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true\n");
}

TEST(Tier4String, LessThan) {
    auto R = compileAndRun(
        "program p; var s: string(20); b: boolean;\n"
        "begin s := 'Apple'; b := s < 'Banana'; writeln(b) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true\n");
}

TEST(Tier4String, SubstrFunction) {
    // EP §6.7.5.4: four characters starting at index 2, not characters 2..4.
    auto R = compileAndRun(
        "program p; var s, u: string(20);\n"
        "begin s := 'Hello'; u := substr(s, 2, 4); writeln(u) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "ello\n");
}

TEST(Tier4String, IndexFunction) {
    auto R = compileAndRun(
        "program p; var s: string(20); n: integer;\n"
        "begin s := 'Hello'; n := index(s, 'ell'); writeln(n) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n");
}

TEST(Tier4String, TrimFunction) {
    auto R = compileAndRun(
        "program p; var s, u: string(20); n: integer;\n"
        "begin s := 'hello   '; u := trim(s); n := length(u); writeln(n) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n");
}

TEST(Tier4String, SubstringVariable) {
    auto R = compileAndRun(
        "program p; var s: string(20); n: integer;\n"
        "begin s := 'Pascal'; n := length(s[2..4]); writeln(n) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n");
}

// ---------------------------------------------------------------------------
// EP Tier 9 — Complex Number Type (§6.4.2.2 / §6.7.6.2–3 / §6.8.3.2)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// EP Tier 10 — Direct-Access File Handling (§6.4.3.6 / §6.7.5.2 / §6.7.6.5–6)
// Items 55–60: file[index] of T, extend, update, SeekRead/Write/Update,
//              position, LastPosition, empty
// ---------------------------------------------------------------------------

TEST(EP10DirectFile, DirectAccessFileTypeParsesAndCompiles) {
    // Item 55: file [index-type] of T should parse without errors.
    auto R = compileAndRun(
        "program p;\n"
        "type IntFile = file [1..10] of integer;\n"
        "var f: IntFile;\n"
        "    g: file [0..99] of integer;\n"
        "begin\n"
        "  writeln('ok')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "ok\n");
}

TEST(EP10DirectFile, BinaryReadWrite) {
    // Binary typed-file I/O: write(f, v) and read(f, v) for file of integer.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    v: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  v := 42;\n"
        "  write(f, v);\n"
        "  v := 99;\n"
        "  write(f, v);\n"
        "  update(f);\n"        // rewind to beginning (update on internal file)
        "  read(f, v);\n"
        "  writeln(v);\n"       // should print 42
        "  read(f, v);\n"
        "  writeln(v)\n"        // should print 99
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n99\n");
}

TEST(EP10DirectFile, SeekWriteAndSeekRead) {
    // Items 58: SeekWrite positions and writes; SeekRead positions and reads.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    v: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  v := 10; seekwrite(f, 0); write(f, v);\n"
        "  v := 20; seekwrite(f, 1); write(f, v);\n"
        "  v := 30; seekwrite(f, 2); write(f, v);\n"
        "  seekread(f, 1);\n"   // seek to component 1
        "  read(f, v);\n"
        "  writeln(v);\n"       // 20
        "  seekread(f, 0);\n"   // seek to component 0
        "  read(f, v);\n"
        "  writeln(v);\n"       // 10
        "  seekread(f, 2);\n"   // seek to component 2
        "  read(f, v);\n"
        "  writeln(v)\n"        // 30
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "20\n10\n30\n");
}

TEST(EP10DirectFile, SeekUpdateReadAndWrite) {
    // Item 58: SeekUpdate — read then write at same position.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    v: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  v := 100; seekwrite(f, 0); write(f, v);\n"
        "  v := 200; seekwrite(f, 1); write(f, v);\n"
        "  seekupdate(f, 1);\n"    // position at component 1 for update
        "  read(f, v);\n"          // read 200
        "  v := v + 5;\n"
        "  seekwrite(f, 1);\n"     // overwrite component 1
        "  write(f, v);\n"
        "  seekread(f, 1);\n"
        "  read(f, v);\n"
        "  writeln(v)\n"            // 205
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "205\n");
}

TEST(EP10DirectFile, PositionFunction) {
    // Item 59: position(f) returns the current component index.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    v, p: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  v := 1; write(f, v);\n"
        "  v := 2; write(f, v);\n"
        "  v := 3; write(f, v);\n"
        "  p := position(f);\n"   // position after writing 3 items = 3
        "  writeln(p);\n"
        "  seekwrite(f, 0);\n"
        "  p := position(f);\n"   // back at 0
        "  writeln(p);\n"
        "  seekwrite(f, 2);\n"
        "  p := position(f);\n"   // at 2
        "  writeln(p)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n0\n2\n");
}

TEST(EP10DirectFile, LastPositionFunction) {
    // Item 59: lastposition(f) returns the index of the last component.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    v, lp: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  v := 10; write(f, v);\n"
        "  v := 20; write(f, v);\n"
        "  v := 30; write(f, v);\n"
        "  lp := lastposition(f);\n"  // 3 components → lastpos = 2
        "  writeln(lp)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n");
}

TEST(EP10DirectFile, EmptyOnNewFile) {
    // Item 60: empty(f) is true for a freshly rewritten (empty) file.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  if empty(f) then writeln('empty') else writeln('not-empty')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "empty\n");
}

TEST(EP10DirectFile, EmptyAfterSeekPastEnd) {
    // Item 60: empty(f) is false when position <= lastposition,
    //          true when position > lastposition (past the last component).
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    v: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  v := 42; write(f, v);\n"
        "  { file now has 1 component at index 0; position = 1 (past end) }\n"
        "  if empty(f) then writeln('past-end') else writeln('not-past-end');\n"
        "  seekread(f, 0);\n"   // seek back to component 0
        "  if empty(f) then writeln('past-end') else writeln('at-component')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "past-end\nat-component\n");
}

TEST(EP10DirectFile, ExtendAppendsContent) {
    // Item 56: extend(f) opens for appending; existing content preserved.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    v: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  v := 11; write(f, v);\n"  // component 0
        "  v := 22; write(f, v);\n"  // component 1
        "  extend(f);\n"              // seek to end; preserve content
        "  v := 33; write(f, v);\n"  // component 2
        "  update(f);\n"              // rewind to beginning
        "  read(f, v); writeln(v);\n" // 11
        "  read(f, v); writeln(v);\n" // 22
        "  read(f, v); writeln(v)\n"  // 33
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11\n22\n33\n");
}

TEST(EP10DirectFile, UpdateRewrites) {
    // Item 57: update(f) opens for read+write without truncating.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    v: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  v := 7; write(f, v);\n"   // write 7 at component 0
        "  update(f);\n"              // rewind
        "  read(f, v);\n"             // read back 7
        "  writeln(v);\n"
        "  update(f);\n"              // rewind again
        "  v := 99; write(f, v);\n"  // overwrite component 0 with 99
        "  update(f);\n"              // rewind
        "  read(f, v);\n"             // read 99
        "  writeln(v)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n99\n");
}

// ---------------------------------------------------------------------------
// ISO §6.5.5 / §6.9.1: the buffer variable
//
// f^ is the component at the file's current position: reset and get fill it,
// put appends it, and read and write are defined in terms of it.  A file of
// anything but a scalar can only be reached this way, so these also stand in
// for typed-file I/O generally.
// ---------------------------------------------------------------------------

TEST(BufferVariable, PutWritesWhatWasAssignedToIt) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    i: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  for i := 1 to 4 do begin f^ := i * i; put(f) end;\n"
        "  reset(f);\n"
        "  while not eof(f) do begin write(f^, ' '); get(f) end;\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 4 9 16 \n");
}

TEST(BufferVariable, ResetLeavesTheFirstComponentInIt) {
    // §6.5.5: after reset, f^ is the first component, and read(f,v) is
    // v := f^; get(f) — so the two views agree before and after.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    i, v: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  for i := 1 to 3 do begin f^ := i; put(f) end;\n"
        "  reset(f);\n"
        "  v := f^;\n"
        "  read(f, i);\n"
        "  writeln(v, ' ', i, ' ', f^)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 1 2\n");
}

TEST(BufferVariable, CarriesARecordComponent) {
    // A file of records has no textual form, so the buffer variable is the
    // only way to reach one.
    auto R = compileAndRun(
        "program p;\n"
        "type rec = record a: integer; b: char end;\n"
        "var f: file of rec;\n"
        "    r: rec;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  r.a := 7; r.b := 'x'; f^ := r; put(f);\n"
        "  r.a := 8; r.b := 'y'; f^ := r; put(f);\n"
        "  reset(f);\n"
        "  while not eof(f) do begin write(f^.a, f^.b, ' '); get(f) end;\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7x 8y \n");
}

TEST(BufferVariable, WriteAndReadCarryARecordToo) {
    // §6.9.1: on a file that is not a textfile, write(f,e) is f^ := e; put(f),
    // so a record is a write-parameter like any other component.
    auto R = compileAndRun(
        "program p;\n"
        "type rec = record a: integer; b: char end;\n"
        "var f: file of rec;\n"
        "    r: rec;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  r.a := 1; r.b := 'p'; write(f, r);\n"
        "  r.a := 2; r.b := 'q'; write(f, r);\n"
        "  reset(f);\n"
        "  read(f, r); write(r.a, r.b, ' ');\n"
        "  read(f, r); writeln(r.a, r.b)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1p 2q\n");
}

TEST(BufferVariable, CarriesAnArrayComponent) {
    auto R = compileAndRun(
        "program p;\n"
        "type row = array[1..3] of integer;\n"
        "var f: file of row;\n"
        "    r: row;\n"
        "    i: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  for i := 1 to 3 do r[i] := i * 5;\n"
        "  write(f, r);\n"
        "  reset(f);\n"
        "  read(f, r);\n"
        "  for i := 1 to 3 do write(r[i], ' ');\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5 10 15 \n");
}

TEST(BufferVariable, ReadsAndWritesATextFileOneCharacterAtATime) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var f: text;\n"
        "    c: char;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  f^ := 'a'; put(f);\n"
        "  f^ := 'b'; put(f);\n"
        "  reset(f);\n"
        "  while not eof(f) do begin c := f^; write(c); get(f) end;\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // Trailing space: the two puts left a line with no marker, and §6.4.3.5
    // makes a text file a sequence of terminated lines, so reading it back
    // finds the marker that closes the one that was written.
    EXPECT_EQ(R.Stdout, "ab \n");
}

TEST(BufferVariable, IsTheComponentAtThePositionSeekReadChose) {
    // The buffer is filled by peeking, so position(f) still reports where f^
    // itself is rather than the component after it.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file[1..10] of integer;\n"
        "    i: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  for i := 1 to 5 do write(f, i * 2);\n"
        "  seekread(f, 2);\n"
        "  writeln(position(f), ' ', f^, ' ', lastposition(f));\n"
        "  get(f);\n"
        "  writeln(position(f), ' ', f^)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2 6 4\n3 8\n");
}

TEST(BufferVariable, PutAfterSeekWriteOverwritesThatComponent) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: file[1..10] of integer;\n"
        "    i: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  for i := 1 to 4 do write(f, i);\n"
        "  seekwrite(f, 1); f^ := 99; put(f);\n"
        "  seekread(f, 0);\n"
        "  for i := 1 to 4 do begin write(f^, ' '); get(f) end;\n"
        "  writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 99 3 4 \n");
}

TEST(BufferVariable, AWrittenValueBecomesAComponentOfTheFilesType) {
    // §6.9.1: write(f,e) is f^ := e, so an integer written to a file of real
    // widens on the way in rather than being read back as a real.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of real;\n"
        "    r: real;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  write(f, 3);\n"
        "  write(f, 2.5);\n"
        "  reset(f);\n"
        "  read(f, r); write(r:0:1, ' ');\n"
        "  read(f, r); writeln(r:0:1)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3.0 2.5\n");
}

TEST(BufferVariable, AValueThatIsNoComponentIsTurnedAway) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  write(f, 1.5)\n"
        "end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("type mismatch in write"), std::string::npos) << R.Stderr;
}

TEST(EP9Complex, CmplxConstructor) {
    // cmplx(re, im) constructor; re() and im() extractors.
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := cmplx(3.0, 4.0);\n"
        "  writeln(re(c):1:1);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3.0\n4.0\n");
}

TEST(EP9Complex, PolarConstructor) {
    // polar(r, 0) = (r, 0i)
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := polar(2.0, 0.0);\n"
        "  writeln(re(c):1:1);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2.0\n0.0\n");
}

TEST(EP9Complex, Addition) {
    // (1+2i) + (3+4i) = (4+6i)
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := cmplx(1.0, 2.0) + cmplx(3.0, 4.0);\n"
        "  writeln(re(c):1:1);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4.0\n6.0\n");
}

TEST(EP9Complex, Subtraction) {
    // (5+7i) - (2+3i) = (3+4i)
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := cmplx(5.0, 7.0) - cmplx(2.0, 3.0);\n"
        "  writeln(re(c):1:1);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3.0\n4.0\n");
}

TEST(EP9Complex, Multiplication) {
    // (1+2i)*(3+4i) = (3-8) + (4+6)i = -5+10i
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := cmplx(1.0, 2.0) * cmplx(3.0, 4.0);\n"
        "  writeln(re(c):1:1);\n"
        "  writeln(im(c):2:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-5.0\n10.0\n");
}

TEST(EP9Complex, Division) {
    // (1+2i) / (1+0i) = (1+2i)
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := cmplx(1.0, 2.0) / cmplx(1.0, 0.0);\n"
        "  writeln(re(c):1:1);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1.0\n2.0\n");
}

TEST(EP9Complex, AbsComplex) {
    // |3+4i| = 5.0
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := cmplx(3.0, 4.0);\n"
        "  writeln(abs(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5.0\n");
}

TEST(EP9Complex, ReIm) {
    // re and im extraction
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := cmplx(2.5, 3.5);\n"
        "  writeln(re(c):1:1);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2.5\n3.5\n");
}

TEST(EP9Complex, Arg) {
    // arg(0+1i) = pi/2 ≈ 1.5708; write rounded to 4 decimals
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "    a: real;\n"
        "begin\n"
        "  c := cmplx(0.0, 1.0);\n"
        "  a := arg(c);\n"
        "  writeln(a:1:4)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1.5708\n");
}

TEST(EP9Complex, SqrtComplex) {
    // sqrt(-1+0i) ≈ 0+1i
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "    r: complex;\n"
        "begin\n"
        "  c := cmplx(-1.0, 0.0);\n"
        "  r := sqrt(c);\n"
        "  writeln(re(r):1:1);\n"
        "  writeln(im(r):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0.0\n1.0\n");
}

TEST(EP9Complex, SinComplex) {
    // sin(0+0i) = 0+0i
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "    r: complex;\n"
        "begin\n"
        "  c := cmplx(0.0, 0.0);\n"
        "  r := sin(c);\n"
        "  writeln(re(r):1:1);\n"
        "  writeln(im(r):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0.0\n0.0\n");
}

TEST(EP9Complex, Widening) {
    // real -> complex widening: c := 3.14 sets re=3.14, im=0
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := 3.14;\n"
        "  writeln(re(c):1:2);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3.14\n0.0\n");
}

TEST(EP9Complex, IntegerWidening) {
    // integer -> complex widening: c := 42 sets re=42, im=0
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := 42;\n"
        "  writeln(re(c):2:1);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42.0\n0.0\n");
}

TEST(EP9Complex, MixedArith) {
    // cmplx(1,0) + 2.0 (complex + real) = cmplx(3,0)
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := cmplx(1.0, 0.0) + 2.0;\n"
        "  writeln(re(c):1:1);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3.0\n0.0\n");
}

// ---------------------------------------------------------------------------
// EP Tier 5 — Function/Procedure Enhancements
// ---------------------------------------------------------------------------

// §6.7.2: named result variable
TEST(EP5NamedResult, BasicAssignAndReturn) {
    auto R = compileAndRun(
        "program p;\n"
        "function double(n: integer) = result : integer;\n"
        "begin result := n * 2 end;\n"
        "begin writeln(double(7)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "14\n");
}

TEST(EP5NamedResult, FunctionNameStillWorks) {
    // Traditional assignment to the function name must still work even when
    // a result variable is also declared.
    auto R = compileAndRun(
        "program p;\n"
        "function triple(n: integer) = res : integer;\n"
        "begin triple := n * 3 end;\n"
        "begin writeln(triple(5)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "15\n");
}

TEST(EP5NamedResult, ReadResultVariable) {
    // The result variable can be both read and written inside the body.
    auto R = compileAndRun(
        "program p;\n"
        "function fib(n: integer) = r : integer;\n"
        "begin\n"
        "  if n <= 1 then r := n\n"
        "  else r := fib(n-1) + fib(n-2)\n"
        "end;\n"
        "begin writeln(fib(8)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "21\n");
}

// §6.7.3.1: protected parameters
TEST(EP5Protected, AssignmentToProtectedRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure q(protected x: integer);\n"
        "begin x := 5 end;\n"
        "begin q(1) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("protected") != std::string::npos) << R.Stderr;
}

TEST(EP5Protected, ReadingProtectedParamAllowed) {
    auto R = compileAndRun(
        "program p;\n"
        "function double(protected n: integer): integer;\n"
        "begin double := n * 2 end;\n"
        "begin writeln(double(6)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "12\n");
}

// §6.4.9: type of x
TEST(EP5TypeOf, BasicTypeInquiry) {
    // var y: type of x; should give y the same type as x (integer here).
    auto R = compileAndRun(
        "program p;\n"
        "var x: integer;\n"
        "var y: type of x;\n"
        "begin x := 42; y := x; writeln(y) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(EP5TypeOf, TypeOfInParamList) {
    // EP §6.4.9: type of x as a parameter type; x is integer so the param
    // is integer too. (Using integer avoids float-formatting ambiguity.)
    auto R = compileAndRun(
        "program p;\n"
        "var v: integer;\n"
        "procedure show(x: type of v);\n"
        "begin writeln(x) end;\n"
        "begin show(99) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "99\n");
}

// §6.9.3.9.3: for v in set-expr do
TEST(EP5ForIn, IteratesSetMembers) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of 1..5;\n"
        "var v, total: integer;\n"
        "begin\n"
        "  s := [1, 3, 5];\n"
        "  total := 0;\n"
        "  for v in s do total := total + v;\n"
        "  writeln(total)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "9\n");
}

TEST(EP5ForIn, EmptySetIteratesZeroTimes) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of 0..10;\n"
        "var v, count: integer;\n"
        "begin\n"
        "  s := [];\n"
        "  count := 0;\n"
        "  for v in s do count := count + 1;\n"
        "  writeln(count)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0\n");
}

TEST(EP5ForIn, NonSetExprRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "var x, v: integer;\n"
        "begin for v in x do writeln(v) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("set") != std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// EP §6.7.3.7: Conformant Array Parameters (Tier 6, features #35–#38)
// ---------------------------------------------------------------------------

// #35: Value conformant array param — sum elements using lo..hi bounds.
TEST(EP6ConformantArray, ValueParamSum) {
    auto R = compileAndRun(
        "program p;\n"
        "function sumArr(A: array [lo..hi : integer] of integer) : integer;\n"
        "var i, s: integer;\n"
        "begin\n"
        "  s := 0;\n"
        "  for i := lo to hi do s := s + A[i];\n"
        "  sumArr := s\n"
        "end;\n"
        "var arr: array [1..5] of integer;\n"
        "begin\n"
        "  arr[1] := 10; arr[2] := 20; arr[3] := 30;\n"
        "  arr[4] := 40; arr[5] := 50;\n"
        "  writeln(sumArr(arr))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "150\n");
}

// #35: Value conformant array — use lo and hi bound variables in body.
TEST(EP6ConformantArray, ValueParamUseBounds) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure showbounds(A: array [lo..hi : integer] of integer);\n"
        "begin\n"
        "  writeln(lo);\n"
        "  writeln(hi)\n"
        "end;\n"
        "var arr: array [3..7] of integer;\n"
        "begin\n"
        "  showbounds(arr)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n7\n");
}

// #36: Variable conformant array param — fill array via var param.
TEST(EP6ConformantArray, VarParamFill) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure fill(var A: array [lo..hi : integer] of integer; v: integer);\n"
        "var i: integer;\n"
        "begin\n"
        "  for i := lo to hi do A[i] := v\n"
        "end;\n"
        "var arr: array [1..4] of integer;\n"
        "var i: integer;\n"
        "begin\n"
        "  fill(arr, 7);\n"
        "  for i := 1 to 4 do writeln(arr[i])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n7\n7\n7\n");
}

// #35: Call same conformant procedure with two different-sized arrays.
TEST(EP6ConformantArray, DifferentSizedArrays) {
    auto R = compileAndRun(
        "program p;\n"
        "function countElems(A: array [lo..hi : integer] of integer) : integer;\n"
        "begin\n"
        "  countElems := hi - lo + 1\n"
        "end;\n"
        "var a3: array [1..3] of integer;\n"
        "var a7: array [1..7] of integer;\n"
        "begin\n"
        "  writeln(countElems(a3));\n"
        "  writeln(countElems(a7))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n7\n");
}

// #35: Value conformant with 0-based array.
TEST(EP6ConformantArray, ZeroBasedArray) {
    auto R = compileAndRun(
        "program p;\n"
        "function first(A: array [lo..hi : integer] of integer) : integer;\n"
        "begin first := A[lo] end;\n"
        "var arr: array [0..2] of integer;\n"
        "begin\n"
        "  arr[0] := 99;\n"
        "  writeln(first(arr))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "99\n");
}

// #36: Conformant var param — modify and read back.
TEST(EP6ConformantArray, VarParamModifyReadback) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure setFirst(var A: array [lo..hi : integer] of integer; v: integer);\n"
        "begin A[lo] := v end;\n"
        "var arr: array [2..5] of integer;\n"
        "begin\n"
        "  arr[2] := 0;\n"
        "  setFirst(arr, 42);\n"
        "  writeln(arr[2])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

// #37: Multi-dimensional abbreviated form — abbreviated syntax
// `array [lo..hi : T1; lo2..hi2 : T2] of E` parses correctly and expands to
// nested conformant schemas. This test verifies the abbreviated form compiles
// and that all bound variables are visible in the procedure body.
TEST(EP6ConformantArray, MultiDimAbbreviatedSyntax) {
    // Declare a 2D conformant param using the abbreviated form.
    // The abbreviated syntax expands at parse time to nested schemas.
    auto R = compileAndRun(
        "program p;\n"
        "{ abbreviated 2D conformant syntax — reads only bound vars }\n"
        "procedure showBounds(A: array [lo..hi : integer; c1..c2 : integer] of integer);\n"
        "begin\n"
        "  writeln(lo); writeln(hi); writeln(c1); writeln(c2)\n"
        "end;\n"
        "{ type-compatible actual: array of array for the 2D conformant }\n"
        "type Row = array [10..12] of integer;\n"
        "var mat: array [3..5] of Row;\n"
        "begin\n"
        "  showBounds(mat)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n5\n10\n12\n");
}

TEST(EP6ConformantArray, MultiDimElementAccess) {
    // The array arrives as one flat block, so an element of it can only be
    // found by folding the subscripts against the runtime bounds of every
    // dimension.
    auto R = compileAndRun(
        "program p;\n"
        "type m = array[1..2, 1..3] of integer;\n"
        "var a: m;\n"
        "    i, j: integer;\n"
        "function total(var x: array[l1..h1: integer;\n"
        "                            l2..h2: integer] of integer): integer;\n"
        "var r, s, t: integer;\n"
        "begin\n"
        "  t := 0;\n"
        "  for r := l1 to h1 do\n"
        "    for s := l2 to h2 do\n"
        "      t := t + x[r, s];\n"
        "  total := t\n"
        "end;\n"
        "begin\n"
        "  for i := 1 to 2 do for j := 1 to 3 do a[i, j] := i * 10 + j;\n"
        "  writeln(total(a))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "102\n");   // 11+12+13 + 21+22+23
}

TEST(EP6ConformantArray, MultiDimAssignmentThroughAVarParameter) {
    // Chained subscripts reach the same element as the comma form, and the
    // dimensions need not start at 1.
    auto R = compileAndRun(
        "program p;\n"
        "type m = array[0..1, 5..7] of integer;\n"
        "var a: m;\n"
        "    i, j: integer;\n"
        "procedure fill(var x: array[l1..h1: integer;\n"
        "                            l2..h2: integer] of integer);\n"
        "var r, s: integer;\n"
        "begin\n"
        "  for r := l1 to h1 do\n"
        "    for s := l2 to h2 do\n"
        "      x[r][s] := r * 100 + s\n"
        "end;\n"
        "begin\n"
        "  fill(a);\n"
        "  for i := 0 to 1 do\n"
        "    for j := 5 to 7 do write(a[i, j], ' ');\n"
        "  writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5 6 7 105 106 107 \n");
}

TEST(EP6ConformantArray, MultiDimRowWidthComesFromTheActual) {
    // Two actuals of different widths reach the same routine; the row stride
    // has to come from the bounds passed with each of them.
    auto R = compileAndRun(
        "program p;\n"
        "type small = array[1..2, 1..2] of integer;\n"
        "     big   = array[1..2, 1..4] of integer;\n"
        "var s: small;\n"
        "    b: big;\n"
        "    i, j: integer;\n"
        "function corner(var x: array[l1..h1: integer;\n"
        "                             l2..h2: integer] of integer): integer;\n"
        "begin corner := x[h1, h2] end;\n"
        "begin\n"
        "  for i := 1 to 2 do for j := 1 to 2 do s[i, j] := i * j;\n"
        "  for i := 1 to 2 do for j := 1 to 4 do b[i, j] := i * j;\n"
        "  writeln(corner(s), ' ', corner(b))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4 8\n");
}

TEST(EP6ConformantArray, ThreeDimensions) {
    auto R = compileAndRun(
        "program p;\n"
        "type m = array[1..2, 1..2, 1..2] of integer;\n"
        "var a: m;\n"
        "    i, j, k: integer;\n"
        "function total(var x: array[a1..b1: integer;\n"
        "                            a2..b2: integer;\n"
        "                            a3..b3: integer] of integer): integer;\n"
        "var r, s, t, n: integer;\n"
        "begin\n"
        "  n := 0;\n"
        "  for r := a1 to b1 do\n"
        "    for s := a2 to b2 do\n"
        "      for t := a3 to b3 do n := n + x[r, s, t];\n"
        "  total := n\n"
        "end;\n"
        "begin\n"
        "  for i := 1 to 2 do for j := 1 to 2 do for k := 1 to 2 do\n"
        "    a[i, j, k] := i * 100 + j * 10 + k;\n"
        "  writeln(total(a))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1332\n");  // each of i, j, k is 1 four times and 2 four times
}

TEST(MultiDimIndexing, ReadsAnElementUnderExtendedPascalToo) {
    // EP §6.8.7 spells a structured value TypeName[...], which reads like a
    // subscript list; a variable's name is not a type name, so a[i, j] still
    // indexes.
    auto R = compileAndRun(
        "program p;\n"
        "type digits = set of 0..9;\n"
        "var a: array[1..2, 1..2] of integer;\n"
        "    s: digits;\n"
        "begin\n"
        "  a[1, 1] := 4;\n"
        "  a[2, 2] := 7;\n"
        "  s := digits[1, 3, 5];\n"
        "  writeln(a[1, 1] + a[2, 2], ' ', card(s))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11 3\n");
}

// #38: Protected conformant array — cannot assign to elements.
TEST(EP6ConformantArray, ProtectedConformantRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure tryWrite(protected A: array [lo..hi : integer] of integer);\n"
        "begin A[lo] := 99 end;\n"
        "var arr: array [1..3] of integer;\n"
        "begin tryWrite(arr) end.\n", kEP);
    // Should fail to compile because 'protected' param cannot be assigned.
    EXPECT_NE(R.ExitCode, 0);
}

// #35/#36: Conformant array param type checking — wrong element type rejected.
TEST(EP6ConformantArray, ElemTypeMismatchRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure proc(A: array [lo..hi : integer] of integer);\n"
        "begin end;\n"
        "var arr: array [1..3] of real;\n"
        "begin proc(arr) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("conformant") != std::string::npos ||
                R.Stderr.find("mismatch")   != std::string::npos) << R.Stderr;
}

// #35: Conformant param passed a non-array argument is rejected.
TEST(EP6ConformantArray, NonArrayActualRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure proc(A: array [lo..hi : integer] of integer);\n"
        "begin end;\n"
        "var x: integer;\n"
        "begin proc(x) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("conformant") != std::string::npos ||
                R.Stderr.find("array")      != std::string::npos) << R.Stderr;
}

// #36: Pass conformant actual to another conformant param (forward bounds).
TEST(EP6ConformantArray, ConformantPassThrough) {
    auto R = compileAndRun(
        "program p;\n"
        "function sumArr(A: array [lo..hi : integer] of integer) : integer;\n"
        "var i, s: integer;\n"
        "begin\n"
        "  s := 0;\n"
        "  for i := lo to hi do s := s + A[i];\n"
        "  sumArr := s\n"
        "end;\n"
        "procedure wrapper(var B: array [lo2..hi2 : integer] of integer);\n"
        "begin writeln(sumArr(B)) end;\n"
        "var arr: array [1..4] of integer;\n"
        "begin\n"
        "  arr[1] := 1; arr[2] := 2; arr[3] := 3; arr[4] := 4;\n"
        "  wrapper(arr)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n");
}

// ISO 7185 level 1: conformant array parameters are standard Pascal, not an
// Extended Pascal extension, and were rejected under -std=iso7185 for as long
// as they had existed.
TEST(ISO7185Level1, ConformantArrayIsStandardPascal) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var a: array [1..4] of integer; i: integer;\n"
        "function total(x: array [lo..hi: integer] of integer): integer;\n"
        "var j, s: integer;\n"
        "begin s := 0; for j := lo to hi do s := s + x[j]; total := s end;\n"
        "begin for i := 1 to 4 do a[i] := i; writeln(total(a)) end.\n",
        "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n");
}

// ISO 7185 §6.6.3.7.1: the packed form of the schema, which is how a string of
// any length is passed.  It did not parse at all: in a parameter list `packed`
// went to the ordinary array parser, which stopped at the ':' in the bounds.
TEST(ISO7185Level1, PackedConformantArray) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var a: packed array [1..5] of char;\n"
        "    b: packed array [1..11] of char;\n"
        "procedure show(s: packed array [lo..hi: integer] of char);\n"
        "var i: integer;\n"
        "begin for i := lo to hi do write(s[i]); writeln end;\n"
        "begin a := 'hello'; b := 'hello world'; show(a); show(b) end.\n",
        "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "hello\nhello world\n");
}

// ISO 7185 §6.6.3.7.1: the packed form takes one index-type-specification;
// only the unpacked form may name several.
TEST(ISO7185Level1, PackedConformantArrayIsOneDimension) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure q(s: packed array [lo..hi: integer;\n"
        "                             j..k: integer] of char);\n"
        "begin end;\n"
        "begin end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("one dimension") != std::string::npos) << R.Stderr;
}

// A conformant array parameter is a type of its own, so no array is assignable
// to it whole.  This was allowed, and copied the source array's length into
// whatever the caller had passed: eight elements written through a parameter
// bound to an array of three ran five past the end of it.
TEST(ISO7185Level1, WholeArrayAssignmentToConformantRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "var big: array [1..8] of integer; small: array [1..3] of integer;\n"
        "procedure q(var x: array [lo..hi: integer] of integer);\n"
        "begin x := big end;\n"
        "begin q(small) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("conformant array") != std::string::npos) << R.Stderr;
}

// ISO 7185 §6.6.5.4: pack and unpack take any array, and a conformant array
// parameter is one — its bounds are values rather than numbers, which is a
// matter for the code generator.  Sema let it through and the generator, which
// wanted two arrays with bounds it could read off their types, stopped the
// compiler with an internal error.
TEST(ISO7185Level1, PackAndUnpackTakeAConformantArray) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var u: array [1..5] of char; z: packed array [1..5] of char;\n"
        "    wide: array [1..9] of char; i: integer;\n"
        "procedure topacked(var x: array [lo..hi: integer] of char);\n"
        "begin pack(x, lo, z) end;\n"
        "procedure fromacked(var x: array [lo..hi: integer] of char);\n"
        "begin unpack(z, x, lo) end;\n"
        "begin\n"
        "  u[1] := 'a'; u[2] := 'b'; u[3] := 'c'; u[4] := 'd'; u[5] := 'e';\n"
        "  topacked(u); writeln(z);\n"
        "  for i := 1 to 9 do wide[i] := '.';\n"
        "  fromacked(wide);\n"
        "  for i := 1 to 9 do write(wide[i]); writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "abcde\nabcde....\n");
}

// The operands of pack and unpack were never checked to be arrays, so one that
// was not reached a generator with nothing to lower.
TEST(ISO7185Level1, PackOnANonArrayIsDiagnosed) {
    auto R = compileAndRun(
        "program p;\n"
        "var i: integer; z: packed array [1..3] of char;\n"
        "begin pack(i, 1, z) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("expects an array") != std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// EP Tier 7 — Schema System (EP §6.4.7–§6.8.4)
// ---------------------------------------------------------------------------

// #39: Basic schema definition and array element use.
TEST(EP7Schema, BasicDefinitionAndUse) {
    auto R = compileAndRun(
        "program p;\n"
        "type Vector(n: integer) = array[1..n] of real;\n"
        "var v: Vector(5);\n"
        "begin\n"
        "  v[1] := 1.0; v[2] := 2.0; v[3] := 3.0;\n"
        "  writeln(v[2]:1:0)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n");
}

// #41: Discriminant access v.n returns the discriminant value.
TEST(EP7Schema, DiscriminantAccess) {
    auto R = compileAndRun(
        "program p;\n"
        "type Vector(n: integer) = array[1..n] of integer;\n"
        "var v: Vector(5);\n"
        "begin\n"
        "  writeln(v.n)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n");
}

// #42: 'with' on a schema instance exposes discriminant identifiers.
TEST(EP7Schema, WithExposesDiscriminants) {
    auto R = compileAndRun(
        "program p;\n"
        "type Vec(n: integer) = array[1..n] of integer;\n"
        "var v: Vec(7);\n"
        "begin\n"
        "  with v do writeln(n)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n");
}

// Two schema instances of the same schema with different discriminants are independent.
TEST(EP7Schema, TwoDifferentInstances) {
    auto R = compileAndRun(
        "program p;\n"
        "type Vector(n: integer) = array[1..n] of integer;\n"
        "var a: Vector(3);\n"
        "var b: Vector(7);\n"
        "begin\n"
        "  a[1] := 10; a[2] := 20; a[3] := 30;\n"
        "  b[1] := 100;\n"
        "  writeln(a.n);\n"
        "  writeln(b.n);\n"
        "  writeln(a[2]);\n"
        "  writeln(b[1])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n7\n20\n100\n");
}

// #44: Schema as value parameter type.
TEST(EP7Schema, SchemaAsValueParam) {
    auto R = compileAndRun(
        "program p;\n"
        "type Vec(n: integer) = array[1..n] of integer;\n"
        "function first(v: Vec(3)) : integer;\n"
        "begin first := v[1] end;\n"
        "var a: Vec(3);\n"
        "begin\n"
        "  a[1] := 42;\n"
        "  writeln(first(a))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

// #43: new(p) for a pointer to a schema instance.
TEST(EP7Schema, NewForSchemaPointer) {
    auto R = compileAndRun(
        "program p;\n"
        "type Vec(n: integer) = array[1..n] of integer;\n"
        "type VecPtr = ^Vec(4);\n"
        "var ptr: VecPtr;\n"
        "begin\n"
        "  new(ptr);\n"
        "  ptr^[1] := 99;\n"
        "  writeln(ptr^[1])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "99\n");
}

// Wrong argument count for schema instantiation is an error.
TEST(EP7Schema, WrongArgCountRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "type Vec(n: integer) = array[1..n] of integer;\n"
        "var v: Vec(1, 2);\n"
        "begin end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("discriminant") != std::string::npos ||
                R.Stderr.find("expects")      != std::string::npos ||
                R.Stderr.find("schema")       != std::string::npos) << R.Stderr;
}

// #45: 'bindable' prefix on a type definition is accepted without error.
TEST(EP7Schema, BindableKeyword) {
    auto R = compileAndRun(
        "program p;\n"
        "type T = bindable integer;\n"
        "var x: T;\n"
        "begin x := 5; writeln(x) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n");
}

// Schema whose body is a record (multi-field schema).
TEST(EP7Schema, RecordSchema) {
    auto R = compileAndRun(
        "program p;\n"
        "type Pair(n: integer) = record\n"
        "  x: array[1..n] of integer;\n"
        "  y: integer\n"
        "end;\n"
        "var p2: Pair(3);\n"
        "begin\n"
        "  p2.x[1] := 7;\n"
        "  p2.y := 42;\n"
        "  writeln(p2.x[1]);\n"
        "  writeln(p2.y);\n"
        "  writeln(p2.n)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n42\n3\n");
}

// EP §6.4.7: a field of a record body may be bounded by a discriminant.  The
// extent is a constant in each instance, but not in the declaration, and a
// bound that would not fold used to be read as zero — which made `array[0..n]`
// one element long, small enough that everything written past the first ran
// off the end of the variable.
TEST(EP7Schema, ARecordBodyGivesEachFieldItsDiscriminantExtent) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type poly(n: integer) = record\n"
        "  deg: integer;\n"
        "  c: array[0..n] of real\n"
        "end;\n"
        "var q: poly(2); i: integer;\n"
        "begin\n"
        "  q.deg := 2;\n"
        "  for i := 0 to 2 do q.c[i] := i + 0.5;\n"
        "  writeln(q.deg:0, ' ', q.c[0]:0:1, ' ', q.c[2]:0:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2 0.5 2.5\n");
}

// One declaration serves every instantiation, so a layout worked out from the
// declaration alone is whichever instance reached it first: `poly(5)` took the
// three elements of `poly(2)` and wrote its last two into the next variable.
TEST(EP7Schema, TwoInstancesOfARecordSchemaAreLaidOutApart) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type poly(n: integer) = record\n"
        "  deg: integer;\n"
        "  c: array[0..n] of real\n"
        "end;\n"
        "var big: poly(5); small: poly(2); i: integer;\n"
        "begin\n"
        "  small.deg := 2; big.deg := 5;\n"
        "  for i := 0 to 2 do small.c[i] := i;\n"
        "  for i := 0 to 5 do big.c[i] := 100 + i;\n"
        "  writeln(small.deg:0, ' ', small.c[2]:0:1);\n"
        "  writeln(big.deg:0, ' ', big.c[5]:0:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2 2.0\n5 105.0\n");
}

// The discriminants reach every extent in the body, however deeply it is
// written: several dimensions at once, an expression over more than one
// discriminant, and a record nested inside the body.
TEST(EP7Schema, ARecordBodyIsMeasuredThroughoutByItsDiscriminants) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type mat(r, c: integer) = record\n"
        "  m: array[1..r, 1..c] of integer;\n"
        "  edge: array[1..2*r+1] of integer;\n"
        "  inner: record w: array[1..c] of integer end\n"
        "end;\n"
        "var a: mat(2, 3); b: mat(4, 1); i, j: integer;\n"
        "begin\n"
        "  for i := 1 to 2 do for j := 1 to 3 do a.m[i, j] := i * 10 + j;\n"
        "  for i := 1 to 5 do a.edge[i] := i;\n"
        "  for i := 1 to 3 do a.inner.w[i] := 100 + i;\n"
        "  for i := 1 to 4 do b.m[i, 1] := i;\n"
        "  for i := 1 to 9 do b.edge[i] := i * 2;\n"
        "  b.inner.w[1] := 7;\n"
        "  writeln(a.m[2, 3]:0, ' ', a.edge[5]:0, ' ', a.inner.w[3]:0);\n"
        "  writeln(b.m[4, 1]:0, ' ', b.edge[9]:0, ' ', b.inner.w[1]:0)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "23 5 103\n4 18 7\n");
}

// ISO §6.4.3.3: the alternatives of a variant share one run of storage, and
// what that run has to hold is a question the discriminants answer too.
TEST(EP7Schema, AVariantInARecordSchemaIsSizedByTheDiscriminant) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type vrec(n: integer) = record\n"
        "  w: array[0..n] of integer;\n"
        "  case kind: boolean of\n"
        "    true:  (many: array[0..n] of integer);\n"
        "    false: (one: integer)\n"
        "end;\n"
        "var a: vrec(1); b: vrec(4); i: integer;\n"
        "begin\n"
        "  a.kind := true; b.kind := true;\n"
        "  for i := 0 to 1 do begin a.w[i] := i + 10; a.many[i] := i + 50 end;\n"
        "  for i := 0 to 4 do begin b.w[i] := i + 100; b.many[i] := i + 500 end;\n"
        "  writeln(a.w[1]:0, ' ', a.many[1]:0);\n"
        "  writeln(b.w[4]:0, ' ', b.many[4]:0)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11 51\n104 504\n");
}

// A local and a heap instance are laid out by the same reckoning as a global.
TEST(EP7Schema, ARecordSchemaIsMeasuredTheSameWhereverItLives) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type box(n: integer) = record w: array[0..n] of integer; tail: integer end;\n"
        "type pbox = ^box(3);\n"
        "var ptr: pbox; i: integer;\n"
        "procedure local;\n"
        "var l3: box(3); l2: box(2); j: integer;\n"
        "begin\n"
        "  for j := 0 to 3 do l3.w[j] := 300 + j;\n"
        "  for j := 0 to 2 do l2.w[j] := 200 + j;\n"
        "  l3.tail := 33; l2.tail := 22;\n"
        "  writeln(l3.w[3]:0, ' ', l3.tail:0, ' ', l2.w[2]:0, ' ', l2.tail:0)\n"
        "end;\n"
        "begin\n"
        "  local;\n"
        "  new(ptr);\n"
        "  for i := 0 to 3 do ptr^.w[i] := i * 7;\n"
        "  ptr^.tail := 77;\n"
        "  writeln(ptr^.w[3]:0, ' ', ptr^.tail:0)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "303 33 202 22\n21 77\n");
}

// Schema with two discriminants.
TEST(EP7Schema, TwoDiscriminants) {
    auto R = compileAndRun(
        "program p;\n"
        "type Mat(m: integer; n: integer) = array[1..m] of array[1..n] of real;\n"
        "var A: Mat(2, 3);\n"
        "begin\n"
        "  A[1][2] := 5.0;\n"
        "  writeln(A.m);\n"
        "  writeln(A.n);\n"
        "  writeln(A[1][2]:1:0)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n3\n5\n");
}

// ---------------------------------------------------------------------------
// EP §6.8.7 Structured value constructors (Tier 8)
// ---------------------------------------------------------------------------

// §6.8.7.2 Array value constructors

TEST(EP8ArrayConstructor, AllIndicesSpecified) {
    // Row[1: 10; 2: 20; 3: 30; 4: 40; 5: 50] assigns all five elements.
    auto R = compileAndRun(
        "program p;\n"
        "type Row = array[1..5] of integer;\n"
        "var r: Row;\n"
        "    i: integer;\n"
        "begin\n"
        "  r := Row[1: 10; 2: 20; 3: 30; 4: 40; 5: 50];\n"
        "  for i := 1 to 5 do writeln(r[i])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n20\n30\n40\n50\n");
}

TEST(EP8ArrayConstructor, OtherwiseDefault) {
    // Row[otherwise: 99] fills every element with 99.
    auto R = compileAndRun(
        "program p;\n"
        "type Row = array[1..4] of integer;\n"
        "var r: Row;\n"
        "    i: integer;\n"
        "begin\n"
        "  r := Row[otherwise: 99];\n"
        "  for i := 1 to 4 do writeln(r[i])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "99\n99\n99\n99\n");
}

TEST(EP8ArrayConstructor, MixedIndexAndOtherwise) {
    // Set index 2 to 42, all others to 0 (zero-init then override).
    auto R = compileAndRun(
        "program p;\n"
        "type Row = array[1..5] of integer;\n"
        "var r: Row;\n"
        "    i: integer;\n"
        "begin\n"
        "  r := Row[2: 42; otherwise: 0];\n"
        "  for i := 1 to 5 do writeln(r[i])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0\n42\n0\n0\n0\n");
}

TEST(EP8ArrayConstructor, RangeIndex) {
    // Row[1..3: 7; 4..5: 9] uses range labels.
    auto R = compileAndRun(
        "program p;\n"
        "type Row = array[1..5] of integer;\n"
        "var r: Row;\n"
        "    i: integer;\n"
        "begin\n"
        "  r := Row[1..3: 7; 4..5: 9];\n"
        "  for i := 1 to 5 do writeln(r[i])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n7\n7\n9\n9\n");
}

TEST(EP8ArrayConstructor, MultiLabelArm) {
    // Row[1,3: 0; 2: 42; 4,5: 99] uses comma-separated index lists.
    auto R = compileAndRun(
        "program p;\n"
        "type Row = array[1..5] of integer;\n"
        "var r: Row;\n"
        "    i: integer;\n"
        "begin\n"
        "  r := Row[1,3: 0; 2: 42; 4,5: 99];\n"
        "  for i := 1 to 5 do writeln(r[i])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0\n42\n0\n99\n99\n");
}

// §6.8.7.3 Record value constructors

TEST(EP8RecordConstructor, AllFields) {
    // Point[x: 10; y: 20] assigns both fields.
    auto R = compileAndRun(
        "program p;\n"
        "type Point = record x, y: integer end;\n"
        "var p: Point;\n"
        "begin\n"
        "  p := Point[x: 10; y: 20];\n"
        "  writeln(p.x);\n"
        "  writeln(p.y)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n20\n");
}

TEST(EP8RecordConstructor, PartialFields) {
    // Point[x: 7] leaves y as zero (zero-initialized).
    auto R = compileAndRun(
        "program p;\n"
        "type Point = record x, y: integer end;\n"
        "var pt: Point;\n"
        "begin\n"
        "  pt := Point[x: 7];\n"
        "  writeln(pt.x);\n"
        "  writeln(pt.y)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n0\n");
}

TEST(EP8ArrayConstructor, CompleterNeedsNoColon) {
    // §6.8.7.2: array-value-completer = 'otherwise' component-value.  plang
    // also tolerates a colon there, so both spellings must work.
    auto R = compileAndRun(
        "program p;\n"
        "type row = array[1..4] of integer;\n"
        "var a, b: row;\n"
        "    i: integer;\n"
        "begin\n"
        "  a := row[1: 10; otherwise 0];\n"
        "  b := row[1: 10; otherwise: 0];\n"
        "  for i := 1 to 4 do write(a[i], b[i], ' ');\n"
        "  writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1010 00 00 00 \n");
}

TEST(EP8RecordConstructor, VariantPart) {
    // §6.8.7.3: variant-part-value = 'case' [ tag-field-identifier ':' ]
    //   constant-tag-value 'of' '[' field-list-value ']'.  The tag value is a
    //   component of the record like any other.
    auto R = compileAndRun(
        "program p;\n"
        "type shape = record\n"
        "  area: integer;\n"
        "  case kind: 1..2 of\n"
        "    1: (side: integer);\n"
        "    2: (w, h: integer)\n"
        "end;\n"
        "var s: shape;\n"
        "begin\n"
        "  s := shape[area: 9; case kind: 1 of [side: 3]];\n"
        "  writeln(s.area, ' ', s.kind, ' ', s.side);\n"
        "  s := shape[area: 12; case kind: 2 of [w: 3; h: 4]];\n"
        "  writeln(s.area, ' ', s.kind, ' ', s.w, ' ', s.h)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "9 1 3\n12 2 3 4\n");
}

TEST(EP8StructuredConst, ArrayValueInAConstantDefinition) {
    // §6.8.7 exists largely so that a structured value can be a constant, so
    // the constant has to be able to name a type defined above it.
    auto R = compileAndRun(
        "program p;\n"
        "type row = array[1..4] of integer;\n"
        "const v = row[1: 10; 2..3: 20; otherwise 0];\n"
        "var i: integer;\n"
        "begin\n"
        "  for i := 1 to 4 do write(v[i], ' ');\n"
        "  writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10 20 20 0 \n");
}

TEST(EP8StructuredConst, RecordValueAndALocalOne) {
    auto R = compileAndRun(
        "program p;\n"
        "type row = array[1..3] of integer;\n"
        "     pt  = record x, y: integer end;\n"
        "const origin = pt[x: 3; y: 4];\n"
        "procedure show;\n"
        "const v = row[1: 7; otherwise 1];\n"
        "var i: integer;\n"
        "begin\n"
        "  for i := 1 to 3 do write(v[i], ' ');\n"
        "  writeln\n"
        "end;\n"
        "begin\n"
        "  show;\n"
        "  writeln(origin.x + origin.y)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7 1 1 \n7\n");
}

TEST(EP8StructuredConst, IsStillNotAssignable) {
    auto R = compileAndRun(
        "program p;\n"
        "type row = array[1..3] of integer;\n"
        "const v = row[otherwise 1];\n"
        "begin v[1] := 9 end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("not an assignable variable"), std::string::npos)
        << R.Stderr;
}

// §6.8.7.4 Set value constructors with type-name prefix

TEST(EP8SetConstructor, TypedSetLiteralMultiElement) {
    // Colors[red, green] — typed set literal with two elements.
    auto R = compileAndRun(
        "program p;\n"
        "type Color = (red, green, blue);\n"
        "type Colors = set of Color;\n"
        "var c: Colors;\n"
        "begin\n"
        "  c := Colors[red, green];\n"
        "  if red in c then writeln('red');\n"
        "  if green in c then writeln('green');\n"
        "  if not (blue in c) then writeln('no blue')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "red\ngreen\nno blue\n");
}

// §6.4.1 'value' initial state specifier

TEST(EP8ValueInit, ScalarInitializer) {
    // var x: integer value 42;
    auto R = compileAndRun(
        "program p;\n"
        "var x: integer value 42;\n"
        "begin\n"
        "  writeln(x)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(EP8ValueInit, ArrayWithConstructorInit) {
    // var r: Row value Row[otherwise: 5]; — array with constructor init.
    auto R = compileAndRun(
        "program p;\n"
        "type Row = array[1..3] of integer;\n"
        "var r: Row value Row[1: 10; 2: 20; 3: 30];\n"
        "    i: integer;\n"
        "begin\n"
        "  for i := 1 to 3 do writeln(r[i])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n20\n30\n");
}

TEST(EP8ValueInit, GlobalVarInit) {
    // Global variable with value initializer.
    auto R = compileAndRun(
        "program p;\n"
        "var counter: integer value 100;\n"
        "begin\n"
        "  counter := counter + 1;\n"
        "  writeln(counter)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "101\n");
}

TEST(EP8ValueInit, LocalVarInit) {
    // Local variable with value initializer inside a procedure.
    auto R = compileAndRun(
        "program p;\n"
        "function add(a, b: integer): integer;\n"
        "var result: integer value 0;\n"
        "begin\n"
        "  result := a + b;\n"
        "  add := result\n"
        "end;\n"
        "begin\n"
        "  writeln(add(3, 4))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n");
}

// ---------------------------------------------------------------------------
// EP Tier 11 — Date & Time
// ---------------------------------------------------------------------------

// §6.4.3.4: TimeStamp is a predefined record type with 8 fields
TEST(EP11DateTime, TimeStampFieldsAccessible) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin\n"
        "  t.DateValid := true;\n"
        "  t.year := 2025; t.month := 6; t.day := 15;\n"
        "  t.TimeValid := false;\n"
        "  t.hour := 0; t.minute := 0; t.second := 0;\n"
        "  writeln(t.year, ' ', t.month, ' ', t.day)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2025 6 15\n");
}

// §6.7.6.9: date(t) formats as YYYY-MM-DD
TEST(EP11DateTime, DateFormatsCorrectly) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin\n"
        "  t.DateValid := true;\n"
        "  t.year := 2026; t.month := 8; t.day := 8;\n"
        "  t.TimeValid := false;\n"
        "  t.hour := 0; t.minute := 0; t.second := 0;\n"
        "  writeln(date(t))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2026-08-08\n");
}

// §6.7.6.9: time(t) formats as HH:MM:SS
TEST(EP11DateTime, TimeFormatsCorrectly) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin\n"
        "  t.DateValid := false;\n"
        "  t.year := 0; t.month := 0; t.day := 0;\n"
        "  t.TimeValid := true;\n"
        "  t.hour := 14; t.minute := 30; t.second := 5;\n"
        "  writeln(time(t))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "14:30:05\n");
}

// §6.7.6.9: invalid DateValid/TimeValid yields zeroed output
TEST(EP11DateTime, InvalidDateGivesZeroes) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin\n"
        "  t.DateValid := false;\n"
        "  t.year := 0; t.month := 0; t.day := 0;\n"
        "  t.TimeValid := false;\n"
        "  t.hour := 0; t.minute := 0; t.second := 0;\n"
        "  writeln(date(t)); writeln(time(t))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0000-00-00\n00:00:00\n");
}

// §6.7.5.8: GetTimeStamp fills the record; DateValid and year >= 2024
TEST(EP11DateTime, GetTimeStampFillsRecord) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin\n"
        "  GetTimeStamp(t);\n"
        "  if t.DateValid then writeln('date ok') else writeln('date fail');\n"
        "  if t.year >= 2024 then writeln('year ok') else writeln('year fail');\n"
        "  if t.TimeValid then writeln('time ok') else writeln('time fail')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "date ok\nyear ok\ntime ok\n");
}

// §6.7.5.8 + §6.7.6.9 combined: GetTimeStamp then format
TEST(EP11DateTime, GetTimeStampThenFormat) {
    // Just verify the formatted strings have the right length (10 and 8)
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin\n"
        "  GetTimeStamp(t);\n"
        "  writeln(length(date(t)));\n"
        "  writeln(length(time(t)))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n8\n");
}

// ---------------------------------------------------------------------------
// EP Tier 12 — Binding System
// ---------------------------------------------------------------------------

// §6.4.3.4: BindingType has required 'bound' field accessible
TEST(EP12Binding, BindingTypeFieldsAccessible) {
    auto R = compileAndRun(
        "program p;\n"
        "var b: BindingType;\n"
        "begin\n"
        "  b.bound := true;\n"
        "  if b.bound then writeln('bound') else writeln('unbound');\n"
        "  b.bound := false;\n"
        "  if b.bound then writeln('bound') else writeln('unbound')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "bound\nunbound\n");
}

// §6.7.6.8: binding(f) after bind(f,b) reports the entity named by b.name.
// §6.7.5.6 NOTE 3: b.bound is ignored by bind, so the name is what binds.
TEST(EP12Binding, BindingAfterExplicitBind) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; b, b2: BindingType;\n"
        "begin\n"
        "  b.name := '/tmp/plang_bind_explicit.txt';\n"
        "  bind(f, b);\n"
        "  rewrite(f);\n"
        "  b2 := binding(f);\n"
        "  if b2.bound then writeln('bound') else writeln('unbound');\n"
        "  writeln(b2.name)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "bound\n/tmp/plang_bind_explicit.txt\n");
}

// §6.7.5.6 NOTE 3: b.bound is ignored, so binding to an unnamed entity does
// not make the file bound.
TEST(EP12Binding, BoundFieldIsIgnoredByBind) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; b, b2: BindingType;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  b.bound := true;\n"
        "  bind(f, b);\n"
        "  b2 := binding(f);\n"
        "  if b2.bound then writeln('bound') else writeln('unbound')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "unbound\n");
}

// §6.7.5.6: unbind(f) causes binding(f).bound = false
TEST(EP12Binding, UnbindClearsBinding) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; b: BindingType;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  unbind(f);\n"
        "  b := binding(f);\n"
        "  if b.bound then writeln('bound') else writeln('unbound')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "unbound\n");
}

// §6.7.5.6: bind(f, b) re-establishes binding after unbind
TEST(EP12Binding, BindReestablishesBinding) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; b, b2: BindingType;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  unbind(f);\n"
        "  b.name := '/tmp/plang_bind_reestablish.txt';\n"
        "  bind(f, b);\n"
        "  rewrite(f);\n"
        "  b2 := binding(f);\n"
        "  if b2.bound then writeln('bound') else writeln('unbound')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "bound\n");
}

// §6.7.5.6: a bound file opens the named entity, so data written through it
// survives close and is read back by a plain reset.
TEST(EP12Binding, BoundFileRoundTrips) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; b: BindingType; s: string(40);\n"
        "begin\n"
        "  b.name := '/tmp/plang_bind_roundtrip.txt';\n"
        "  bind(f, b);\n"
        "  rewrite(f); writeln(f, 'through the binding'); close(f);\n"
        "  reset(f); readln(f, s); close(f);\n"
        "  writeln(s)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "through the binding\n");
}

// §6.7.6.8: binding(f) on a closed file returns bound=false
TEST(EP12Binding, BindingOnClosedFileReturnsUnbound) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; b: BindingType;\n"
        "begin\n"
        "  b := binding(f);\n"
        "  if b.bound then writeln('bound') else writeln('unbound')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "unbound\n");
}

// EP §6.4.1: only a variable declared bindable may be bound.  Before, the
// qualifier was stripped by the parser and any file at all was accepted.
TEST(EP12Binding, APlainFileVariableCannotBeBound) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: text; b: BindingType;\n"
        "begin b.name := '/tmp/plang_notbindable.txt'; bind(f, b) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("not declared bindable"), std::string::npos);
}

TEST(EP12Binding, UnbindAndBindingAlsoRequireABindableVariable) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: text; b: BindingType;\n"
        "begin unbind(f); b := binding(f) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
}

// A named type carries the qualifier to every variable declared with it.
TEST(EP12Binding, ANamedBindableTypeDeclaresBindableVariables) {
    auto R = compileAndRun(
        "program p;\n"
        "type bf = bindable text;\n"
        "var f: bf; b: BindingType; s: string(30);\n"
        "begin\n"
        "  b.name := '/tmp/plang_bind_namedtype.txt';\n"
        "  bind(f, b);\n"
        "  rewrite(f); writeln(f, 'named'); close(f);\n"
        "  reset(f); readln(f, s); close(f);\n"
        "  writeln(s)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "named\n");
}

// EP §6.7.5.6: the first argument is a variable, not any expression that
// happens to have a file type.
TEST(EP12Binding, TheFirstArgumentMustBeAVariable) {
    auto R = compileAndRun(
        "program p;\n"
        "var b: BindingType;\n"
        "begin bind(42, b) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("must be a variable"), std::string::npos);
}

TEST(EP12Binding, TheSecondArgumentMustBeABindingType) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; n: integer;\n"
        "begin bind(f, n) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("BindingType"), std::string::npos);
}

TEST(EP12Binding, TheArgumentCountIsChecked) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text;\n"
        "begin bind(f) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
}

// EP §6.4.1 lets any type be bindable and leaves what that means to the
// implementation; plang binds files to paths and says so for anything else.
TEST(EP12Binding, ABindableVariableThatIsNotAFileIsRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "var n: bindable integer; b: BindingType;\n"
        "begin b.name := '/tmp/x'; bind(n, b) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("only a file variable"), std::string::npos);
}

// A bindable var parameter stands for the bindable variable passed to it.
TEST(EP12Binding, ABindableVarParameterCanBeBound) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; s: string(30);\n"
        "procedure attach(var g: bindable text; path: string(60));\n"
        "var b: BindingType;\n"
        "begin b.name := path; bind(g, b) end;\n"
        "begin\n"
        "  attach(f, '/tmp/plang_bind_varparam.txt');\n"
        "  rewrite(f); writeln(f, 'via parameter'); close(f);\n"
        "  reset(f); readln(f, s); close(f);\n"
        "  writeln(s)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "via parameter\n");
}

// ---------------------------------------------------------------------------
// Strings that are not string variables
//
// A string value is the address of a { length, bytes } struct, and three
// places built one that was not: the 'value' initializer stored the pointer to
// the literal's temporary straight into the variable, a named string constant
// was interned as a bare run of bytes whose first eight characters were then
// read as its length, and a string argument passed by value was handed over as
// an address where the callee had declared the struct itself.
// ---------------------------------------------------------------------------

TEST(StringValueInit, GlobalStringInitializer) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(10) value 'hi';\n"
        "begin writeln('[', s, '] ', length(s)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hi] 2\n");
}

TEST(StringValueInit, LocalStringInitializer) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure q; var s: string(10) value 'local';\n"
        "begin writeln('[', s, ']') end;\n"
        "begin q end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[local]\n");
}

TEST(StringValueInit, TwoNamesShareOneInitializer) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: string(8) value 'xy';\n"
        "begin writeln('[', a, '][', b, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[xy][xy]\n");
}

TEST(StringValueInit, TheVariableIsStillWritable) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(10) value 'hi';\n"
        "begin writeln(s); s := 'there'; writeln(s) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "hi\nthere\n");
}

TEST(StringValueInit, ThroughANamedType) {
    // Whether a declaration is a string is a question about the type, not
    // about how it was written; keying off the syntax missed this one and left
    // the pointer bits in the length field.
    auto R = compileAndRun(
        "program p;\n"
        "type st = string(12);\n"
        "var a: st value 'init';\n"
        "begin writeln('a=[', a, '] len=', length(a)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "a=[init] len=4\n");
}

TEST(StringValueInit, ThroughANamedTypeInsideAProcedure) {
    auto R = compileAndRun(
        "program p;\n"
        "type st = string(12);\n"
        "procedure q; var a: st value 'init';\n"
        "begin writeln('[', a, ']') end;\n"
        "begin q end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[init]\n");
}

TEST(StringValueInit, NonStringInitializersAreUnaffected) {
    auto R = compileAndRun(
        "program p;\n"
        "var n: integer value 7; r: real value 1.5;\n"
        "begin writeln(n, ' ', r:0:1) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7 1.5\n");
}

TEST(StringConst, AssignedToAStringVariable) {
    auto R = compileAndRun(
        "program p;\n"
        "const greeting = 'hello';\n"
        "var s: string(10);\n"
        "begin s := greeting; writeln('[', s, '] ', length(s)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hello] 5\n");
}

TEST(StringConst, WrittenDirectly) {
    auto R = compileAndRun(
        "program p;\n"
        "const greeting = 'hello';\n"
        "begin writeln('[', greeting, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hello]\n");
}

TEST(StringConst, Concatenated) {
    auto R = compileAndRun(
        "program p;\n"
        "const greeting = 'hello';\n"
        "var s: string(20);\n"
        "begin s := greeting + ' there'; writeln('[', s, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hello there]\n");
}

TEST(StringConst, Compared) {
    auto R = compileAndRun(
        "program p;\n"
        "const greeting = 'hello';\n"
        "var s: string(10);\n"
        "begin s := 'hello'; writeln(s = greeting) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true\n");
}

TEST(StringConst, DeclaredInsideAProcedure) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure q; const tag = 'inner'; var s: string(10);\n"
        "begin s := tag; writeln('[', s, ']') end;\n"
        "begin q end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[inner]\n");
}

TEST(StringConst, ASingleCharacterConstantIsStillAChar) {
    auto R = compileAndRun(
        "program p;\n"
        "const c = 'x';\n"
        "var ch: char;\n"
        "begin ch := c; writeln('[', ch, '] ', ord(ch)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[x] 120\n");
}

TEST(StringParam, PassedByValue) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(20);\n"
        "procedure show(x: string(20)); begin writeln('[', x, ']') end;\n"
        "begin s := 'hi'; show(s) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hi]\n");
}

TEST(StringParam, ArgumentCapacityNeedNotMatchTheParameter) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(10);\n"
        "procedure show(x: string(20)); begin writeln('[', x, ']') end;\n"
        "begin s := 'hi'; show(s) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hi]\n");
}

TEST(StringParam, LiteralAndConstantArguments) {
    auto R = compileAndRun(
        "program p;\n"
        "const g = 'hello';\n"
        "procedure show(x: string(20)); begin writeln('[', x, ']') end;\n"
        "begin show('hi'); show(g) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hi]\n[hello]\n");
}

TEST(StringParam, ByValueIsACopy) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(20);\n"
        "procedure show(x: string(20));\n"
        "begin x := 'changed'; writeln('[', x, ']') end;\n"
        "begin s := 'orig'; show(s); writeln('[', s, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[changed]\n[orig]\n");
}

TEST(StringParam, VarParameterStillAliases) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(20);\n"
        "procedure setit(var x: string(20)); begin x := 'set' end;\n"
        "begin s := 'orig'; setit(s); writeln('[', s, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[set]\n");
}

TEST(StringParam, ToAFunctionAndAlongsideOthers) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(20);\n"
        "function len2(x: string(20)): integer;\n"
        "begin len2 := length(x) * 2 end;\n"
        "procedure both(a: string(10); b: string(10));\n"
        "begin writeln('[', a, '][', b, ']') end;\n"
        "begin s := 'abc'; writeln(len2(s)); both('one', 'two') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "6\n[one][two]\n");
}

// ---------------------------------------------------------------------------
// EP §6.7.5.4: substr(s, i, n) takes n characters starting at i.  The third
// argument is a count, which coincides with an end index only when i is 1.
// ---------------------------------------------------------------------------

TEST(Substr, ThirdArgumentIsALengthNotAnEndIndex) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: string(20);\n"
        "begin t := 'abcdefgh';\n"
        " writeln(substr(t,2,3), ' ', substr(t,1,3), ' ', substr(t,3,1),\n"
        " ' ', substr(t,7,2)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "bcd abc c gh\n");
}

TEST(Substr, ExtractsAWordFromTheMiddle) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: string(20);\n"
        "begin t := 'Hello World'; writeln(substr(t, 7, 5)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "World\n");
}

TEST(Substr, ZeroLengthYieldsTheEmptyString) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: string(20);\n"
        "begin t := 'abc'; writeln('[', substr(t, 2, 0), ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[]\n");
}

TEST(Substr, ReachingPastTheEndIsAnError) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: string(20);\n"
        "begin t := 'abc'; writeln(substr(t, 2, 10)) end.\n", kEP);
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("outside a string of length 3"), std::string::npos)
        << R.Stderr;
}

TEST(Substr, OmittingTheLengthTakesTheRest) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: string(20);\n"
        "begin t := 'abcdefgh'; writeln(substr(t, 4)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "defgh\n");
}

TEST(Substr, ASubstringVariableStillUsesBounds) {
    // EP §6.5.6 writes s[i..j] with an end index, so the two notations differ
    // and both have to keep working.
    auto R = compileAndRun(
        "program p;\n"
        "var t: string(20);\n"
        "begin t := 'abcdefgh'; writeln(t[2..4]) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "bcd\n");
}

// ---------------------------------------------------------------------------
// EP §6.9.3.6: writing a complex value
// ---------------------------------------------------------------------------

TEST(WriteComplex, WritesAsAParenthesisedPair) {
    auto R = compileAndRun(
        "program p(output); var a: complex;\n"
        "begin a := cmplx(3.0, 4.0); writeln(a) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // Each half is a real and is written as one, in the representation of
    // ISO §6.9.3.4.1 and at the default width for a real.
    EXPECT_EQ(R.Stdout, "( 3.00000000000000e+000, 4.00000000000000e+000)\n");
}

TEST(WriteComplex, HonoursWidthAndFractionDigits) {
    auto R = compileAndRun(
        "program p(output); var a: complex;\n"
        "begin a := cmplx(3.0, -4.5); writeln(a:8:2) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "(    3.00,   -4.50)\n");
}

TEST(WriteComplex, GoesToATextFile) {
    auto R = compileAndRun(
        "program p(output); var f: text; z: complex; c: char;\n"
        "begin z := cmplx(1.5, 2.5);\n"
        " rewrite(f, 'cplx.txt'); writeln(f, z); close(f);\n"
        " reset(f, 'cplx.txt');\n"
        " while not eof(f) do begin read(f, c); write(c) end;\n"
        " close(f) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "( 1.50000000000000e+000, 2.50000000000000e+000) ");
}

TEST(WriteComplex, AnExpressionResultIsWritable) {
    auto R = compileAndRun(
        "program p(output); var a, b: complex;\n"
        "begin a := cmplx(1.0, 2.0); b := cmplx(3.0, 4.0);\n"
        " writeln(a + b) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "( 4.00000000000000e+000, 6.00000000000000e+000)\n");
}

// ---------------------------------------------------------------------------
// EP §6.4.3.3 / §6.7.3: a string is a valid function result type
// ---------------------------------------------------------------------------

TEST(StringFunction, ResultAssignedToAVariable) {
    auto R = compileAndRun(
        "program p(output); var n: string(20);\n"
        "function f: string(20); begin f := 'abc' end;\n"
        "begin n := f; writeln('[', n, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[abc]\n");
}

TEST(StringFunction, ResultWrittenDirectly) {
    // A parameterless function named in an expression is a call, and used to
    // be emitted as a reference to a global named after it.
    auto R = compileAndRun(
        "program p(output); function f: string(20); begin f := 'abc' end;\n"
        "begin writeln('[', f, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[abc]\n");
}

TEST(StringFunction, TakesAndReturnsAString) {
    auto R = compileAndRun(
        "program p(output);\n"
        "function g(s: string(10)): string(20); begin g := s + '!' end;\n"
        "begin writeln('[', g('hi'), ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hi!]\n");
}

TEST(StringFunction, ResultParticipatesInConcatenation) {
    auto R = compileAndRun(
        "program p(output); var n: string(30);\n"
        "function f: string(20); begin f := 'abc' end;\n"
        "begin n := f + 'def'; writeln('[', n, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[abcdef]\n");
}

TEST(StringFunction, ResultParticipatesInComparison) {
    auto R = compileAndRun(
        "program p(output); function f: string(20); begin f := 'abc' end;\n"
        "begin writeln(f = 'abc', ' ', length(f)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true 3\n");
}

TEST(StringFunction, RecursesThroughTheEmptyString) {
    auto R = compileAndRun(
        "program p(output);\n"
        "function rep(n: integer): string(20);\n"
        "begin if n <= 0 then rep := '' else rep := 'x' + rep(n - 1) end;\n"
        "begin writeln('[', rep(3), ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[xxx]\n");
}

// ---------------------------------------------------------------------------
// A string component is written and read through its own address
// ---------------------------------------------------------------------------

TEST(StringComponent, ARecordFieldRoundTrips) {
    auto R = compileAndRun(
        "program p(output); var r: record s: string(10) end; n: string(20);\n"
        "begin r.s := 'hi'; n := r.s + '!';\n"
        " writeln('[', r.s, '][', n, ']', ' ', r.s = 'hi') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hi][hi!] true\n");
}

TEST(StringComponent, AnArrayElementRoundTrips) {
    auto R = compileAndRun(
        "program p(output); var a: array[1..2] of string(10);\n"
        "begin a[1] := 'one'; a[2] := 'two';\n"
        " writeln('[', a[1], '][', a[2], ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[one][two]\n");
}

TEST(StringComponent, APointerTargetRoundTrips) {
    auto R = compileAndRun(
        "program p(output); type ps = ^string(10); var q: ps;\n"
        "begin new(q); q^ := 'hi'; writeln('[', q^, ']'); dispose(q) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hi]\n");
}

// ---------------------------------------------------------------------------
// EP §6.1.8: the zero-length string
// ---------------------------------------------------------------------------

TEST(EmptyString, IsAssignableAndHasLengthZero) {
    auto R = compileAndRun(
        "program p(output); var s: string(10);\n"
        "begin s := ''; writeln('[', s, '] ', length(s)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[] 0\n");
}

TEST(EmptyString, ConcatenatesAndCompares) {
    auto R = compileAndRun(
        "program p(output); var s: string(10);\n"
        "begin s := '' + 'ab'; writeln('[', s, '] ', s = 'ab', ' ', '' = '') end.\n",
        kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[ab] true true\n");
}

TEST(EmptyString, IsStillRejectedUnderIso7185) {
    auto R = compileAndRun(
        "program p(output); begin writeln('') end.\n", "-std=iso7185");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("at least one character"), std::string::npos)
        << R.Stderr;
}

// ---------------------------------------------------------------------------
// EP §6.8.3.2: a char is a string-compatible operand of '+'
// ---------------------------------------------------------------------------

TEST(CharConcat, CharPlusString) {
    auto R = compileAndRun(
        "program p(output); var c: char; s: string(10);\n"
        "begin c := 'x'; s := 'ab'; s := c + s; writeln('[', s, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[xab]\n");
}

TEST(CharConcat, StringPlusChar) {
    auto R = compileAndRun(
        "program p(output); var c: char; s: string(10);\n"
        "begin c := 'x'; s := 'ab'; s := s + c; writeln('[', s, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[abx]\n");
}

TEST(CharConcat, CharPlusChar) {
    auto R = compileAndRun(
        "program p(output); var s: string(10); c: char;\n"
        "begin c := 'c'; s := 'a' + 'b' + c; writeln('[', s, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[abc]\n");
}

TEST(CharConcat, CharPlusCharIsStillNotArithmeticUnderIso7185) {
    auto R = compileAndRun(
        "program p(output); var c: char; begin c := 'a' + 'b' end.\n",
        "-std=iso7185");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("requires numeric operands"), std::string::npos)
        << R.Stderr;
}

// ---------------------------------------------------------------------------
// EP §6.7.3.1: the undiscriminated 'string' parameter-form
// ---------------------------------------------------------------------------

TEST(UndiscriminatedString, AcceptsALiteral) {
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure q(s: string); begin writeln('[', s, ']') end;\n"
        "begin q('xy') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[xy]\n");
}

TEST(UndiscriminatedString, AcceptsAnyCapacity) {
    auto R = compileAndRun(
        "program p(output); var a: string(5); b: string(40);\n"
        "procedure q(s: string); begin writeln('[', s, '] ', length(s)) end;\n"
        "begin a := 'ab'; b := 'cdefgh'; q(a); q(b) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[ab] 2\n[cdefgh] 6\n");
}

TEST(UndiscriminatedString, IsPassedByValue) {
    auto R = compileAndRun(
        "program p(output); var a: string(5);\n"
        "procedure q(s: string); begin s := 'zz'; writeln('[', s, ']') end;\n"
        "begin a := 'ab'; q(a); writeln('[', a, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[zz]\n[ab]\n");
}

TEST(UndiscriminatedString, WorksAsAFunctionParameter) {
    auto R = compileAndRun(
        "program p(output);\n"
        "function f(s: string): string(20); begin f := s + '!' end;\n"
        "begin writeln('[', f('hi'), ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hi!]\n");
}

TEST(UndiscriminatedString, PassesThroughAProceduralParameter) {
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure shows(s: string); begin writeln('[', s, ']') end;\n"
        "procedure runs(procedure s(t: string)); begin s('hello') end;\n"
        "begin runs(shows) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hello]\n");
}

// ---------------------------------------------------------------------------
// EP §6.8.3.2: 'pow' yields the type of its base
// ---------------------------------------------------------------------------

TEST(IntegerPow, AnIntegerBaseYieldsAnInteger) {
    auto R = compileAndRun(
        "program p(output); var i: integer;\n"
        "begin i := 2 pow 10; writeln(i) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1024\n");
}

TEST(IntegerPow, IsExactBeyondTheRangeOfADouble) {
    // Routed through std::pow this comes back rounded.
    auto R = compileAndRun(
        "program p(output); begin writeln(3 pow 39) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4052555153018976267\n");
}

TEST(IntegerPow, HandlesNegativeBasesAndZeroExponent) {
    auto R = compileAndRun(
        "program p(output);\n"
        "begin writeln((-3) pow 3, ' ', (-3) pow 2, ' ', 5 pow 0) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-27 9 1\n");
}

TEST(IntegerPow, ARealBaseStillYieldsAReal) {
    auto R = compileAndRun(
        "program p(output); var r: real;\n"
        "begin r := 2.0 pow 3; writeln(r:0:1) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "8.0\n");
}

TEST(IntegerPow, DoubleStarAlwaysYieldsAReal) {
    auto R = compileAndRun(
        "program p(output); var r: real;\n"
        "begin r := 2 ** 3; writeln(r:0:1) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "8.0\n");
}

TEST(IntegerPow, FoldsInAConstantExpressionAndAnArrayBound) {
    auto R = compileAndRun(
        "program p(output); const k = 2 pow 3;\n"
        "var a: array[1..k] of integer;\n"
        "begin a[8] := 5; writeln(k, ' ', a[8]) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "8 5\n");
}

TEST(IntegerPow, ANegativeExponentIsReportedAtRuntime) {
    auto R = compileAndRun(
        "program p(output); var i, j: integer;\n"
        "begin j := -3; i := 2 pow j; writeln(i) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("negative exponent"), std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// EP §6.9.2.2: a string value shall fit the capacity it is assigned to
// ---------------------------------------------------------------------------

TEST(StringCapacity, AnOverLongLiteralIsRejectedAtCompileTime) {
    auto R = compileAndRun(
        "program p(output); var s: string(3);\n"
        "begin s := 'abcdef' end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("does not fit a string(3)"), std::string::npos)
        << R.Stderr;
}

TEST(StringCapacity, AnOverLongValueInitializerIsRejected) {
    auto R = compileAndRun(
        "program p(output); var s: string(3) value 'abcdef';\n"
        "begin writeln(s) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("does not fit a string(3)"), std::string::npos)
        << R.Stderr;
}

TEST(StringCapacity, AnOverLongValueIsReportedAtRuntime) {
    auto R = compileAndRun(
        "program p(output); var s: string(3); u: string(10);\n"
        "begin u := 'abcdef'; s := u; writeln(s) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("assigned to a string(3)"), std::string::npos)
        << R.Stderr;
}

TEST(StringCapacity, AnOverflowingConcatenationIsReported) {
    auto R = compileAndRun(
        "program p(output); var s: string(4); a, b: string(4);\n"
        "begin a := 'abc'; b := 'def'; s := a + b end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("assigned to a string(4)"), std::string::npos)
        << R.Stderr;
}

TEST(StringCapacity, AValueThatFitsIsUnaffected) {
    auto R = compileAndRun(
        "program p(output); var s: string(10); u: string(3);\n"
        "begin u := 'ab'; s := u; s := 'abcdefghij';\n"
        " writeln('[', u, '][', s, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[ab][abcdefghij]\n");
}

// ---------------------------------------------------------------------------
// EP §6.5.3.2: a string has char components selectable by index
// ---------------------------------------------------------------------------

TEST(StringIndex, SelectsACharacter) {
    auto R = compileAndRun(
        "program p(output); var s: string(10);\n"
        "begin s := 'hello'; writeln(s[1], s[5]) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "ho\n");
}

TEST(StringIndex, DrivesACharacterLoop) {
    auto R = compileAndRun(
        "program p(output); var s: string(10); i: integer;\n"
        "begin s := 'hello';\n"
        " for i := 1 to length(s) do write(s[i], '-'); writeln end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "h-e-l-l-o-\n");
}

TEST(StringIndex, IsAssignable) {
    auto R = compileAndRun(
        "program p(output); var s: string(20); i, n: integer; c: char;\n"
        "begin s := 'abcdef'; n := length(s);\n"
        " for i := 1 to n div 2 do\n"
        "  begin c := s[i]; s[i] := s[n-i+1]; s[n-i+1] := c end;\n"
        " writeln(s) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "fedcba\n");
}

TEST(StringIndex, RunsToTheLengthNotTheCapacity) {
    auto R = compileAndRun(
        "program p(output); var s: string(10);\n"
        "begin s := 'ab'; writeln(s[5]) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("string index 5 out of bounds 1..2"),
              std::string::npos) << R.Stderr;
}

TEST(StringIndex, StartsAtOne) {
    auto R = compileAndRun(
        "program p(output); var s: string(10);\n"
        "begin s := 'ab'; writeln(s[0]) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("string index 0"), std::string::npos) << R.Stderr;
}

TEST(StringIndex, WorksOnAComponentString) {
    auto R = compileAndRun(
        "program p(output); var r: record s: string(10) end;\n"
        "begin r.s := 'hey'; writeln(r.s[2]) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "e\n");
}

TEST(StringIndex, DoesNotDisturbSubstringSyntax) {
    auto R = compileAndRun(
        "program p(output); var s: string(20);\n"
        "begin s := 'Pascal'; writeln(s[2..4]) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "asc\n");
}

// ---------------------------------------------------------------------------
// EP §6.5.6 Substring-variables
// ---------------------------------------------------------------------------

TEST(Substring, IsAVariableAndTakesAnAssignment) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s: string(10);\n"
        "begin s := 'abcdef'; s[2..3] := 'XY';\n"
        "  writeln(s, ' ', length(s)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "aXYdef 6\n");
}

TEST(Substring, LeavesTheRestOfTheStringAlone) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s: string(10); i: integer;\n"
        "begin s := 'abcdef';\n"
        "  for i := 1 to 3 do s[i..i] := 'z';\n"
        "  writeln(s, ' ', length(s)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "zzzdef 6\n");
}

TEST(Substring, TakesTheValueOfAnyStringExpression) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s: string(10); t: string(3);\n"
        "begin s := 'abcdef'; t := 'pq';\n"
        "  s[4..6] := t + 'r';\n"
        "  writeln(s) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "abcpqr\n");
}

// EP §6.5.6: the substring is a fixed string of exactly j-i+1 characters, so
// a value of any other length has nowhere to go.
TEST(Substring, TurnsAwayAValueOfAnotherLength) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s: string(10);\n"
        "begin s := 'abcdef'; s[2..3] := 'TOOLONG'; writeln(s) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("substring of length 2"), std::string::npos)
        << R.Stderr;
}

TEST(Substring, IsNotStandardPascal) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s: packed array[1..6] of char;\n"
        "begin s := 'abcdef'; s[2..3] := 'XY'; writeln(s) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("substring operator"), std::string::npos)
        << R.Stderr;
}

// ---------------------------------------------------------------------------
// EP §6.4.2.5 Restricted types
// ---------------------------------------------------------------------------

// The example of §6.4.2.5, whose point is that the interface hides
// real_widget and so leaves an importer nothing it can do with a widget but
// hand it back to the module.
TEST(EP4Restricted, CarriesTheStandardsWidgetExample) {
    auto R = compileAndRun(
        "module widget_module interface;\n"
        "export widgets = (widget, copy_widget, increment_widget, print_widget);\n"
        "type real_widget = record f1: integer; f2: real end;\n"
        "     widget = restricted real_widget;\n"
        "procedure copy_widget(source: real_widget; var target: real_widget);\n"
        "function increment_widget(w: real_widget): widget;\n"
        "procedure print_widget(var f: text; w: real_widget);\n"
        "end;\n"
        "function increment_widget;\n"
        "var mycopy: real_widget;\n"
        "begin mycopy.f1 := w.f1 + 1; mycopy.f2 := w.f2 + 1.0;\n"
        "  increment_widget := mycopy end;\n"
        "procedure copy_widget;\n"
        "begin target := source end;\n"
        "procedure print_widget;\n"
        "begin writeln(f, w.f1, ' ', w.f2:3:1) end;\n"
        "end.\n"
        "program p(output);\n"
        "import widget_module;\n"
        "var a, b: widget;\n"
        "begin copy_widget(a, b); print_widget(output, b) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0 0.0\n");
}

// EP §6.7.3.3: a var parameter of the underlying type accepts a variable of
// the restricted type, which is how one is ever given a value.
TEST(EP4Restricted, IsFilledThroughAVarParameterOfItsUnderlyingType) {
    auto R = compileAndRun(
        "module m interface;\n"
        "export m = (handle, seth, showh);\n"
        "type rep = record n: integer end;\n"
        "     handle = restricted rep;\n"
        "procedure seth(var h: rep; v: integer);\n"
        "procedure showh(h: rep);\n"
        "end;\n"
        "procedure seth; begin h.n := v end;\n"
        "procedure showh; begin writeln(h.n) end;\n"
        "end.\n"
        "program p(output);\n"
        "import m;\n"
        "var x: handle;\n"
        "begin seth(x, 7); showh(x) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n");
}

TEST(EP4Restricted, HasNoComponentsToReachInto) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type rw = record f1: integer end;\n"
        "     w = restricted rw;\n"
        "var a: w;\n"
        "begin writeln(a.f1) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("components are not accessible"), std::string::npos)
        << R.Stderr;
}

TEST(EP4Restricted, IsNeitherAssignedNorCompared) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type rw = record f1: integer end;\n"
        "     w = restricted rw;\n"
        "var a, b: w; c: rw;\n"
        "begin a := b; c := a; if a = b then writeln('eq') end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("nothing can be assigned"), std::string::npos)
        << R.Stderr;
    EXPECT_NE(R.Stderr.find("can only be passed as a parameter"),
              std::string::npos) << R.Stderr;
}

TEST(EP4Restricted, IsNoArithmeticAndNoIO) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type k = restricted integer;\n"
        "var a: k;\n"
        "begin writeln(a); read(a); writeln(a + 1) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_EQ(diagCount(R.Stderr), 3) << R.Stderr;
}

// EP §6.4.3.6: were it a file component, reading the file back would make a
// value of the type without going through the module that owns it.
TEST(EP4Restricted, IsNoComponentOfAFile) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type k = restricted integer;\n"
        "var f: file of k;\n"
        "begin end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("component type of a file"), std::string::npos)
        << R.Stderr;
}

TEST(EP4Restricted, IsNotStandardPascal) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type k = restricted integer;\n"
        "begin end.\n");
    EXPECT_NE(R.ExitCode, 0);
}

// ---------------------------------------------------------------------------
// Two more where codegen recovered something it already knew
//
// EP §6.5.6's substring took the source's capacity by scanning for a variable
// at the same address, and §6.6.3.7.2's conformant relay took the bounds from a
// type that has none.  Both predate 0.1.3.
// ---------------------------------------------------------------------------

TEST(SubstringCapacity, ASubstringOfAFieldOrAnElementKeepsItsCapacity) {
    // The capacity was hunted for by scanning every scope for a variable whose
    // address matched, defaulting to 255.  A field or an element is a GEP that
    // matches nothing, so the substring was silently cut to 255 characters.
    auto R = compileAndRun(
        "program p(output);\n"
        "type rec = record s: string(300) end;\n"
        "var r: rec; a: array[1..2] of string(300); n: string(300); k: integer;\n"
        "begin\n"
        "  n := '';\n"
        "  for k := 1 to 300 do n := n + 'x';\n"
        "  a[1] := n; r.s := n;\n"
        "  writeln(length(n[1..300]), ' ', length(a[1][1..300]), ' ',\n"
        "          length(r.s[1..300]))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "300 300 300\n");
}

TEST(SubstringCapacity, ACapacityIsNotTakenFromTheFieldNextToIt) {
    // The scan was unsound even when it matched: a record whose first field is
    // a string has the record's own address, so it found the RECORD and read a
    // capacity off whatever the second element happened to be.  Here that is
    // `array[1..5] of char`, and a ten-character substring came back five long.
    auto R = compileAndRun(
        "program p(output);\n"
        "type thief = record s: string(20); t: array[1..5] of char end;\n"
        "var th: thief;\n"
        "begin th.s := 'abcdefghij'; writeln(th.s[1..10]) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "abcdefghij\n");
}

TEST(ConformantRelay, RelayingAConformantArrayKeepsEveryDimensionsBounds) {
    // ISO §6.6.3.7.2 permits passing a conformant parameter on to another
    // conformant formal, and it is the ordinary way to factor code over one.
    // Only the outermost dimension's bounds were passed on; the rest came from
    // a ConformantArray type, which has no static bounds, so they arrived 0..0
    // and the callee indexed the flat block with the wrong row width.
    const char* Body =
        "program p(output);\n"
        "type mat = array[1..2, 3..7] of integer;\n"
        "var m: mat; i, j: integer;\n"
        "procedure show(var a: array[u..w: integer; lo..hi: integer] of integer);\n"
        "var r, c: integer;\n"
        "begin\n"
        "  writeln(u, '..', w, ' ', lo, '..', hi);\n"
        "  for r := u to w do begin\n"
        "    for c := lo to hi do write(a[r, c], ' ');\n"
        "    writeln\n"
        "  end\n"
        "end;\n"
        "procedure relay(var a: array[u..w: integer; lo..hi: integer] of integer);\n"
        "begin show(a) end;\n"
        "begin\n"
        "  for i := 1 to 2 do for j := 3 to 7 do m[i, j] := i * 10 + j;\n"
        "  show(m); relay(m)\n"
        "end.\n";
    auto R = compileAndRun(Body, kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // Relayed must be identical to direct, which is the whole point.
    const std::string One = "1..2 3..7\n13 14 15 16 17 \n23 24 25 26 27 \n";
    EXPECT_EQ(R.Stdout, One + One);
}

TEST(ConformantRelay, WithOverASchemaBodyBindsEveryVariantFieldToo) {
    // A schema body may have a variant part like any other record, and emitWith
    // has a second positional walk for it.  Fixing the record path alone left
    // this one binding the first variant field to the blob and never binding
    // the rest -- `with b do two := 22` referred to a pasg_two nothing defined,
    // and the link failed.
    auto R = compileAndRun(
        "program p(output);\n"
        "type box(n: integer) = record kind: integer;\n"
        "       case tag: integer of 1: (one: integer; two: integer); 2: (r: real)\n"
        "     end;\n"
        "var b: box(4);\n"
        "begin b.kind := 9; b.tag := 1;\n"
        "  with b do begin one := 11; two := 22 end;\n"
        "  writeln(b.one, ' ', b.two)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11 22\n");
}

TEST(ConformantRelay, ASubscriptUsesTheArraysBoundsNotAShadowingField) {
    // The bounds were recovered by re-resolving the bound-identifier SPELLING
    // at each subscript, so a record with fields named like the bounds made
    // every subscript inside `with r do` adjust by the record's fields and
    // read outside the block.
    auto R = compileAndRun(
        "program p(output);\n"
        "type rec = record lo, hi: integer end;\n"
        "var a: array[5..9] of integer; r: rec; i: integer;\n"
        "procedure show(x: array[lo..hi: integer] of integer);\n"
        "begin\n"
        "  writeln(x[5], ' ', x[9]);\n"
        "  with r do writeln(x[5], ' ', x[9])\n"
        "end;\n"
        "begin\n"
        "  for i := 5 to 9 do a[i] := i * 10;\n"
        "  r.lo := 0; r.hi := 0; show(a)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "50 90\n50 90\n");
}

TEST(InitialState, NewAppliesItThroughAFieldOrAnElementToo) {
    // EP §6.6: the variable new creates begins in the state its type says.
    // The domain type was only found for an identifier argument, so
    // `new(h.p)` and `new(a[1])` applied no initial state at all.
    auto R = compileAndRun(
        "program p(output);\n"
        "type node = record x: integer value 7 end;\n"
        "     pn = ^node;\n"
        "     holder = record p: pn end;\n"
        "var h: holder; q: pn; a: array[1..2] of pn;\n"
        "begin new(q); new(h.p); new(a[1]);\n"
        "  writeln(q^.x, ' ', h.p^.x, ' ', a[1]^.x) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7 7 7\n");
}

TEST(ForIn, TheControlVariableIsTheDeclaredOne) {
    // EP §6.9.3.9.3 makes the control variable a variable-access, so the
    // declared variable takes each value.  The loop bound the name to a fresh
    // alloca instead, so the body had one variable and everything else had
    // another: a procedure called from the body read the declared variable,
    // which the loop never wrote, and saw it unset on every iteration.
    auto R = compileAndRun(
        "program p(output);\n"
        "var c: char; s: set of char;\n"
        "procedure show; begin write(c) end;\n"
        "begin s := ['q','z']; for c in s do show; writeln end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "qz\n");
}
