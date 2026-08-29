(*
A deliberate TP idiom, confirmed against `fpc -Mtp`: Assign(f, '') binds f
to "the console" rather than to any real path -- a following Reset(f)
attaches f to stdin (the sibling test right next to this one,
assign-empty-name-rewrite-writes-the-console.pas, covers Rewrite/stdout).

RUN: %plang -std=turbo %s -o %t
RUN: echo -n "typed at the console" | %run %t | FileCheck %s
*)

(*
CHECK:[typed at the console]
*)

var f: text; s: string;
begin
  assign(f, '');
  reset(f);
  readln(f, s);
  writeln('[', s, ']');
  close(f);
end.
