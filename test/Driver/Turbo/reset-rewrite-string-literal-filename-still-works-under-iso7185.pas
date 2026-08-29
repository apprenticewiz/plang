(*
RUN: %plang -std=iso7185 %s -o %t.iso
RUN: %run %t.iso | FileCheck --check-prefix=ISO --strict-whitespace --match-full-lines %s
*)

(* Non-regression, ISO 7185 side only: the new Sema-level type check on
   reset/rewrite's optional second argument (see
   reset-and-rewrite-refuse-a-non-string-second-argument.pas, test/Sema/
   Builtins) must not disturb the plain string-literal filename shape --
   the only 2-arg form -std=iso7185 has, since EP's string(n) is
   -std=iso10206-only.  (A `char` read target, rather than a `string`-typed
   one, keeps this source legal under plain -std=iso7185, where bare
   `string` is itself an Extended Pascal extension.)

   The -std=turbo half of this file was retired (Tier 3 Cluster A item 4),
   DELIBERATELY and INTENTIONALLY: real Turbo Pascal's Reset/Rewrite second
   argument is an INTEGER RecSize for an untyped file, not a filename
   (confirmed against `fpc -Mtp`, which REJECTS `reset(f, 'x.txt')` with an
   incompatible-type error).  This project used to accept a string here
   under -std=turbo too as a plang-only convenience -- see this file's own
   git history for the RUN/CHECK lines that used to exercise that -- which
   has now been retired in favor of matching real field practice: a genuine
   Turbo program writes `Assign(f, 'name'); Reset(f);`, never
   `Reset(f, 'name')`.  See
   test/Sema/Builtins/reset-and-rewrite-refuse-a-non-string-second-argument-
   under-turbo.pas for the new rejection, and test/CodeGen/Turbo/reset-
   rewrite-two-argument-implicit-assign-was-retired-explicit-assign-still-
   binds-the-name.pas for the CodeGen-side non-regression test covering
   the same retirement. *)

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
*)
