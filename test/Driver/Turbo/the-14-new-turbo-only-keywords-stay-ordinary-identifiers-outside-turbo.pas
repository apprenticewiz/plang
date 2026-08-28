(*
TokenKinds.def now reserves asm, constructor, destructor, exports,
implementation, inherited, inline, library, object, shl, shr, unit, uses
and xor only for -std=turbo (DIALECT_KEYWORD's mask names D_Turbo alone for
each). Under ISO 7185 and Extended Pascal neither reserves them, so a
program is entitled to declare and use every one as an ordinary variable --
this compiles and runs under both to prove the widened per-dialect scanner
mechanism (see lib/Lex/Scanner.cpp's keywordDialects) does not leak the new
reservation into dialects whose mask does not name them. The same program
is then confirmed genuinely rejected under -std=turbo, where the words are
real keywords and cannot be declared as variables at all.

RUN: %plang %s -o %t
RUN: %run %t | FileCheck --check-prefix=SUM --strict-whitespace --match-full-lines %s
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --check-prefix=SUM --strict-whitespace --match-full-lines %s
RUN: not %plang -std=turbo %s -o %t
*)

(*
SUM:105
*)

program p;
var
  asm, constructor, destructor, exports, implementation, inherited,
  inline, library, object, shl, shr, unit, uses, xor: integer;
begin
  asm := 1; constructor := 2; destructor := 3; exports := 4;
  implementation := 5; inherited := 6; inline := 7; library := 8;
  object := 9; shl := 10; shr := 11; unit := 12; uses := 13; xor := 14;
  writeln(asm + constructor + destructor + exports + implementation +
          inherited + inline + library + object + shl + shr + unit +
          uses + xor)
end.
