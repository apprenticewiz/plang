(*
RUN: %plang -std=iso7185 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK: 1.7976931348623157e+308
CHECK-NEXT: 2.2250738585072014e-308
*)

(* The default real width used to leave only fifteen significant digits
   (DecPlaces = 14), which is one short of the seventeen a double needs to
   round-trip exactly.  Written at that precision, a value near DBL_MAX
   rounds up past the representable range, so writing it to a file and
   reading it back produced +Infinity instead of the original value. *)
program p(output); var f: text; x: real;
begin
  rewrite(f, 'ne.txt');
  writeln(f, 1.7976931348623157e+308);
  writeln(f, 2.2250738585072014e-308);
  close(f);
  reset(f, 'ne.txt');
  read(f, x); writeln(x);
  read(f, x); writeln(x);
  close(f)
end.
