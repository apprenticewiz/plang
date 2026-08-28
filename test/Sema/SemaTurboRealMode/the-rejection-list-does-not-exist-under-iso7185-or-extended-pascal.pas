(*
The real-mode-DOS rejection list is a Turbo-only concept: ISO 7185 and
Extended Pascal never had a real-mode DOS target to begin with, so an
undeclared use of a name like Mem, Seg or Intr under either of those dialects
must get the ordinary, generic "undefined identifier"/"undefined procedure"
message -- exactly what any other undeclared name gets -- never the
Turbo-specific err_turbo_real_mode_facility wording.  checkRealModeDosName
(Sema.h) gates on Opts.turbo() first and returns false immediately for any
other dialect, before even consulting the name list.
*)

(*
RUN: not %plang -std=iso7185 -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK-DAG: undefined identifier 'Mem'
CHECK-DAG: undefined identifier 'Seg'
CHECK-DAG: undefined procedure 'Intr'
CHECK-NOT: real-mode DOS facility
*)

program p;
var x, p1: integer;
begin
  x := Mem;
  x := Seg;
  Intr(p1)
end.
