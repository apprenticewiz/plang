(*
A real, previously-live bug found during verification, not a hypothetical:
Turbo's own reset/rewrite/append dispatch (CGProcCall.cpp) used to check
only `!s.Args.empty()` before routing to plang_tp_reset/rewrite/append --
true for BOTH the 1-argument form (`rewrite(f)`, opens whatever a prior
Assign bound f to) and the (at the time) pre-Assign 2-argument form
(`rewrite(f, 'name.txt')`, then still accepted under -std=turbo per PR
#475's own Sema-level type check and its own "still works under Turbo"
non-regression test) -- and unconditionally called the 1-argument runtime
entry point, silently DISCARDING the filename argument whenever one was
given.  Since a file variable's bound Name starts out empty (the zero-
initialized default), and an empty bound name means "the console" (a
deliberate, real TP idiom for Reset/Rewrite/Append with NO name), the
practical effect was severe: `rewrite(f, 'realname.txt')` under -std=turbo
silently wrote to STDOUT instead of creating the named file at all, and a
following `reset(f, 'realname.txt')` silently read from STDIN instead of
the file -- which, run without a redirected stdin, HANGS waiting for
input that never arrives, rather than failing loudly.  The original fix
(PR #478) made the 2-argument case perform an IMPLICIT Assign (bind the
name first, the same plang_tp_assign call the explicit `assign` builtin
itself makes) before opening.

DELIBERATE, INTENTIONAL follow-up (Tier 3 Cluster A item 4): that implicit-
Assign convenience was itself retired.  Real Turbo Pascal's Reset/Rewrite
second argument is an INTEGER RecSize for an untyped file (confirmed
against `fpc -Mtp`, which REJECTS a string there with an incompatible-type
error) -- plang's old "2-arg reset/rewrite is an implicit Assign" behavior
was a plang-only convenience that contradicted real Turbo field practice.
Now that Assign exists as its own explicit builtin (also from PR #478), a
genuine Turbo program writes `Assign(f, 'name'); Reset(f);`, never
`Reset(f, 'name')`; see reset-and-rewrite-refuse-a-non-string-second-
argument-under-turbo.pas (test/Sema/Builtins) for the new rejection this
file's old 2-argument shape now gets.  This file is kept, rewritten to use
explicit Assign, as the regression test for the underlying "an empty bound
Name means the console" mechanism the original bug was about -- Assign
still has to genuinely bind the name (not leave it to whatever silently
falls back to stdin/stdout) for this to pass.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:HELLO
*)

program p;
var f: text;
    s: string[10];
begin
  assign(f, 'reset-rewrite-two-argument-form-under-turbo-actually-opens-the-named-file.txt');
  rewrite(f);
  writeln(f, 'HELLO');
  close(f);
  assign(f, 'reset-rewrite-two-argument-form-under-turbo-actually-opens-the-named-file.txt');
  reset(f);
  readln(f, s);
  writeln(s)
end.
