(*
RUN: %plang -std=iso7185 %s -o %t.iso
RUN: %run %t.iso | FileCheck --check-prefix=ISO --strict-whitespace --match-full-lines %s

RUN: %plang -std=turbo %s -o %t.turbo
RUN: %run %t.turbo | FileCheck --check-prefix=TURBO --strict-whitespace --match-full-lines %s
*)

(* Non-regression, ISO 7185 and Turbo side: the new Sema-level type check on
   reset/rewrite's optional second argument (see
   reset-and-rewrite-refuse-a-non-string-second-argument.pas, test/Sema/
   Builtins) must not disturb the plain string-literal filename shape --
   the only 2-arg form -std=iso7185 and -std=turbo have, since EP's
   string(n) is -std=iso10206-only.  One source, two RUN lines differing
   only in -std, same shape checked both ways.  (A `char` read target,
   rather than a `string`-typed one, keeps this source legal under plain
   -std=iso7185, where bare `string` is itself an Extended Pascal
   extension.) *)

program p;
var f: text;
    c: char;
begin
  rewrite(f, 'plang_isoturbo_literal.txt');
  write(f, 'HELLO');
  close(f);
  reset(f, 'plang_isoturbo_literal.txt');
  read(f, c);
  writeln(c)
end.

(*
ISO:H
TURBO:H
*)
