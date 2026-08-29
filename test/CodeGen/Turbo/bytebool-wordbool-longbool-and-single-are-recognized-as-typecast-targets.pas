(*
The parser pre-seeds TypeNames_ with the sized-integer ladder's eleven names
so `Byte(x)` etc. parse as a typecast rather than falling through to an
ordinary CallExpr (which Sema then rejects as "not callable", since these are
predefined Sema symbols the parser never sees declared anywhere) -- but the
Boolean-family variants and Single, registered by the very next
Opts.turbo() block in Sema::registerBuiltins, were the identical situation
and were left out of the seed list: `ByteBool(x)`/`WordBool(x)`/
`LongBool(x)`/`Single(x)` all failed to parse as casts ("'ByteBool' is not
callable").  This is not merely a convenience gap: a loose Boolean's whole
POINT is holding a non-canonical nonzero value (`ByteBool` holding 200, not
just 0/1), and a working cast is the ONLY way to construct one at all --
`bb := 200;` for `bb: ByteBool` is itself rejected ("cannot assign 'integer'
to variable of type 'ByteBool'"), so without the cast there was no legal
plang program that could ever reach the loose-Boolean write path with a
value FPC's own "any nonzero is true" rule actually depends on testing.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:TRUE
CHECK-NEXT:TRUE
CHECK-NEXT:TRUE
CHECK-NEXT:3.50
CHECK-NEXT:FALSE
*)

var
  bb: ByteBool;
  wb: WordBool;
  lb: LongBool;
  sg: Single;
begin
  bb := ByteBool(200);   writeln(bb);
  wb := WordBool(300);   writeln(wb);
  lb := LongBool(70000); writeln(lb);
  sg := Single(3.5);     writeln(sg:0:2);
  bb := ByteBool(0);     writeln(bb);
end.
