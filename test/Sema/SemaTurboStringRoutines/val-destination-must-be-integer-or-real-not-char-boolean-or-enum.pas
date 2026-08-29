(*
System-unit string routines item: Val(s, v, code) requires v to be
Integer- or Real-kind (any width) -- confirmed against a local `fpc -Mtp`
install, which refuses a Char/Boolean/Enum destination with its own
"Integer or real expression expected" (a narrower rule than "ordinal",
which Char/Boolean/Enum all are too).  err_val_argument is this compiler's
own diagnostic for the identical refusal.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'val' requires an integer or real variable, got 'char'
CHECK: 'val' requires an integer or real variable, got 'boolean'
*)

program p;
var
  s: string;
  c: char;
  b: boolean;
  code: integer;
begin
  s := '65';
  Val(s, c, code);
  Val(s, b, code);
end.
