(*
Regression gate: shl/shr/xor are DIALECT_KEYWORD entries gated to D_Turbo
alone (TokenKinds.def), so under ISO 7185 or Extended Pascal the scanner
hands each one back as a plain, undeclared Identifier (Scanner.cpp's
keywordDialects) rather than the operator token ParseExpr.cpp's isMulop/
isAddop now recognize -- there is no such operator in either of those two
dialects at all, not even one that happens to reject its operand types.
Referencing each by name with nothing declared under that name is the
cleanest, most direct proof of "does not exist as anything here": no
operator meaning, no keyword meaning, and no variable meaning either, which
is exactly Sema's "undefined identifier" diagnostic.  (The complementary
half -- that ISO/EP a program IS entitled to declare and use these three
spellings as ordinary variables, since neither dialect reserves them --
is already covered by the-14-new-turbo-only-keywords-stay-ordinary-
identifiers-outside-turbo.pas.)  All three are checked in one program
since Sema keeps going after an undefined-identifier error rather than
bailing at the first one.
*)

(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: undefined identifier 'shl'
CHECK: undefined identifier 'shr'
CHECK: undefined identifier 'xor'
*)

program p;
var a, b, c: integer;
begin
  a := shl;
  b := shr;
  c := xor;
  writeln(a, b, c)
end.
