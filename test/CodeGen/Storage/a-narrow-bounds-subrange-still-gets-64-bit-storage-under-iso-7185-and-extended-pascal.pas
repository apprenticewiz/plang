(*
The single most important guarantee of TypeContext::getSubrange's Turbo
storage-width-selection rule (see the sibling tests under
test/Driver/Turbo and test/CodeGen/Turbo) is that ISO 7185 and Extended
Pascal are completely unaffected by it: the rule is gated on Opts.turbo()
and every subrange (and, via SemaType.cpp's EnumTypeNode arm, every enum)
built under those two dialects must keep getting exactly the width/
signedness it always has -- 64-bit, signed for a numeric subrange
inheriting an Integer host's own Width/IsSigned; 64-bit, unsigned for a
Char-, Boolean- or Enum-hosted one; 64-bit, signed (Type's own struct
defaults) for an Enum itself -- REGARDLESS of how narrow its own bounds
or member count are.  `Grade = 1..100` fits a Turbo byte (see
test/Driver/Turbo/subrange-storage-narrows-to-its-own-declared-bounds-
not-its-hosts-width.pas) but must still be a full i64 here.

This one behavioural test is a spot check, not the real evidence for the
guarantee above: the actual verification was an LLVM-IR-text byte-identity
diff over the whole non-Turbo test suite (975 successfully-compiled ISO
7185/EP programs, everything under test/CodeGen, test/EP, test/Conformance,
test/Acceptance, test/Module, test/Smoke, test/Sema, test/Parse, test/Lex
and test/Basic that is not under a Turbo/ directory), compiled to
`-emit-llvm` text with a pre-change and a post-change build and diffed
byte for byte -- described in this change's own PR description.  This
test exists so the guarantee has a permanent, compiled, running check of
its own rather than living only in that one-time diff's output.

Same record shape as char-boolean-and-enum-hosted-subranges-get-their-
natural-host-width.pas (minus the enum-hosted subrange, ISO 7185 has no
`Green..Blue`-shaped host restriction to speak of that test doesn't
already cover) plus a numeric subrange with tight bounds, compiled once
under each dialect: both must read `{ i8, i64, i64, i64, i64, i8 }`, not
the byte-narrowed `{ i8, i8, i8, i8, i8, i8 }` shape Turbo gets for the
Char/Boolean/Enum-hosted fields, nor any narrower Grade.  That struct
spelling can only ever appear as real LLVM output, never as valid Pascal,
so it lives outside the compiled chunk -- see split-file below.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang -emit-llvm %t.dir/test.pas -o %t.iso.ll
RUN: FileCheck %s < %t.iso.ll
RUN: %plang %t.dir/test.pas -o %t.iso
RUN: %run %t.iso | FileCheck --check-prefix=RUNS --strict-whitespace --match-full-lines %s
RUN: %plang -std=iso10206 -emit-llvm %t.dir/test.pas -o %t.ep.ll
RUN: FileCheck %s < %t.ep.ll
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t.ep
RUN: %run %t.ep | FileCheck --check-prefix=RUNS --strict-whitespace --match-full-lines %s
*)

(*
CHECK: { i8, i64, i64, i64, i64, i8 }
RUNS:x5q12y
*)

//--- test.pas
program p(output);
type
  Grade   = 1..100;
  Letters = 'a'..'z';
  Bools   = false..true;
  Col     = (Red, Green, Blue);
var
  v: record
    a: char;
    g: Grade;
    l: Letters;
    b: Bools;
    c: Col;
    z: char;
  end;
begin
  v.a := 'x'; v.g := 5; v.l := 'q'; v.b := true; v.c := Blue; v.z := 'y';
  writeln(v.a, v.g, v.l, ord(v.b), ord(v.c), v.z)
end.
