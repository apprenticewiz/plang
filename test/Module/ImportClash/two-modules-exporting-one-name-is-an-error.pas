(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: imported from both
*)

module A; function f: integer; begin f := 1 end; end.
module B; function f: integer; begin f := 2 end; end.
program p(output); import A; import B; begin writeln(f) end.
