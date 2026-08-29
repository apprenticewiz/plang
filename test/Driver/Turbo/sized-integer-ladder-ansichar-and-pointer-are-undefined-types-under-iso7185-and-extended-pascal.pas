(*
Regression gate, the same shape as
exitcode-is-turbo-only-not-available-under-iso7185-or-extended-pascal.pas:
Sema::registerBuiltins only declares ShortInt, Byte, SmallInt, Word,
LongInt, Cardinal, LongWord, Int64, QWord, AnsiChar and Pointer under
Opts.turbo(), so under -std=iso7185 or -std=iso10206 none of them is a
required identifier refused by name -- each is simply never declared, and
a program that names one as a type gets the plain "undefined type" any
other never-declared name would (err_undefined_type, unchanged wording).
*)

(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: undefined type 'ShortInt'
CHECK: undefined type 'Byte'
CHECK: undefined type 'SmallInt'
CHECK: undefined type 'Word'
CHECK: undefined type 'Cardinal'
CHECK: undefined type 'LongInt'
CHECK: undefined type 'LongWord'
CHECK: undefined type 'Int64'
CHECK: undefined type 'QWord'
CHECK: undefined type 'AnsiChar'
CHECK: undefined type 'Pointer'
*)

program p;
var
  a: ShortInt;
  b: Byte;
  c: SmallInt;
  d: Word;
  e: Cardinal;
  f: LongInt;
  g: LongWord;
  h: Int64;
  i: QWord;
  j: AnsiChar;
  k: Pointer;
begin
end.
