(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: undefined identifier 'nosuchvar'
ERR-ABSENT-NOT: internal error
*)

module M;
  function f(x: integer): integer;
  begin f := nosuchvar + x end;
end.
program p;
  import M;
begin writeln(f(1)) end.
