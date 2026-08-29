(*
The Rewrite/stdout half of the same TP console idiom
assign-empty-name-reset-reads-the-console.pas covers for Reset/stdin:
Assign(f, '') binds f to "the console", and Rewrite(f) attaches f to
stdout -- confirmed against `fpc -Mtp`.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:to stdout via an empty-name rewrite
*)

var f: text;
begin
  assign(f, '');
  rewrite(f);
  writeln(f, 'to stdout via an empty-name rewrite');
  close(f);
end.
