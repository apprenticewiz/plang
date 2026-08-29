(*
A Char-, Boolean- or Enum-hosted subrange (`'a'..'z'`, `false..true`,
`Green..Blue` over `type Col = (Red, Green, Blue)`) must get its host's
own natural width under -std=turbo -- 8 bits for Char/Boolean always, and
an Enum's own (possibly Turbo-narrowed, see the sibling
an-enum-with-more-than-256-members-narrows-to-16-bit-storage.pas) width
for an Enum host -- not TypeContext::getSubrange's DefaultIntWidth_
fallback (16 under Turbo, i.e. Turbo's own `Integer`).  Before this rule,
a Char/Boolean/Enum-hosted subrange under Turbo fell into that fallback
and got stamped 16 regardless of its host: a previously deferred anomaly
this rule also fixes (see TypeContext::getSubrange's own comment).

Six single-byte fields packed together, alternating plain char with the
four kinds this rule narrows, is the simplest thing whose LLVM struct
layout can only read `{ i8, i8, i8, i8, i8, i8 }` if every one of them
really is a byte -- if any single field were still 16 (or 64) bits wide,
this literal spelling would not appear at all.  That struct spelling can
only ever appear as real LLVM output, never as valid Pascal, so it lives
outside the compiled chunk -- see split-file below.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang -std=turbo -emit-llvm %t.dir/test.pas -o %t.ll
RUN: FileCheck %s < %t.ll
RUN: %plang -std=turbo %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --check-prefix=RUNS --strict-whitespace --match-full-lines %s
*)

(*
CHECK: { i8, i8, i8, i8, i8, i8 }
RUNS:x q 1 2 2 y
*)

//--- test.pas
program p;
type
  Letters = 'a'..'z';
  Bools   = false..true;
  Col     = (Red, Green, Blue);
  ColSub  = Green..Blue;
  Rec = record
    a:  char;
    l:  Letters;
    b:  Bools;
    c:  Col;
    cs: ColSub;
    z:  char;
  end;
var v: Rec;
begin
  v.a  := 'x';
  v.l  := 'q';
  v.b  := true;
  v.c  := Blue;
  v.cs := Blue;
  v.z  := 'y';
  writeln(v.a, ' ', v.l, ' ', ord(v.b), ' ', ord(v.c), ' ', ord(v.cs), ' ', v.z);
end.
