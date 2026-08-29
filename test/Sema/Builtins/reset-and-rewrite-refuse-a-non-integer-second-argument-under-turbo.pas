(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* Tier 3 Cluster A item 4: real Turbo Pascal's Reset/Rewrite second
   argument is an INTEGER RecSize for an untyped file (confirmed against
   `fpc -Mtp`, which REJECTS `reset(f, 'x.txt')` with an incompatible-type
   error of its own) -- the mirror image of
   reset-and-rewrite-refuse-a-non-string-second-argument.pas's own check,
   which governs plain -std=iso7185 and -std=iso10206 (EP's own
   reset(f, name) external-file-name form, §6.7.5.2, untouched by this
   item).  This project used to accept a string here under -std=turbo too,
   treating it as an implicit Assign (PR #475/#478) -- a plang-only
   convenience that has now been retired in favor of matching real field
   practice: DELIBERATELY and INTENTIONALLY, a string second argument to
   Reset/Rewrite is now REJECTED under -std=turbo, with a clear diagnostic
   rather than silently opening the named file the old way.  See
   test/Driver/Turbo/reset-rewrite-string-literal-filename-still-works-
   under-iso7185.pas and test/CodeGen/Turbo/reset-rewrite-two-argument-
   implicit-assign-was-retired-explicit-assign-still-binds-the-name.pas for
   the two pre-existing tests this retirement updated. *)

program p;
var f: text;
    u: file;
    n: integer;
begin
  reset(f, 'name.txt');
  rewrite(f, 'name.txt');
  reset(u, 'name.txt');
  n := 128;
  reset(u, n)   (* still legal: a genuine integer RecSize *)
end.

(*
CHECK: 'reset' record-size argument must be an integer, got 'string'
CHECK: 'rewrite' record-size argument must be an integer, got 'string'
CHECK: 'reset' record-size argument must be an integer, got 'string'
CHECK-NOT: record-size argument must be an integer
*)
