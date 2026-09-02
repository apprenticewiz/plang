(*
Issue #643: an {$R+} Turbo ShortString out-of-range index error used to
report the bounds as "1..N", inherited unchanged from EP's own
plang_err_str_index (whose 1-based s[i] rule really does start at 1) --
but index 0 is a legal ShortString index (the length-byte alias; see
shortstring-index-zero-aliases-the-length-byte.pas), so the reported range
should say 0, not 1.

RUN: %plang -std=turbo %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: string index -1 out of bounds 0..10
*)

{$R+}
program p;
var s: string[10];
begin
  s := 'abc';
  s[-1] := #5;
end.
