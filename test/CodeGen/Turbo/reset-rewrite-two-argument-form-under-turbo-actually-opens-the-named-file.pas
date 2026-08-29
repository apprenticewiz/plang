(*
A real, previously-live bug found during verification, not a hypothetical:
Turbo's own reset/rewrite/append dispatch (CGProcCall.cpp) used to check
only `!s.Args.empty()` before routing to plang_tp_reset/rewrite/append --
true for BOTH the 1-argument form (`rewrite(f)`, opens whatever a prior
Assign bound f to) and the pre-Assign 2-argument form
(`rewrite(f, 'name.txt')`, still accepted under -std=turbo per PR #475's
own Sema-level type check and its own "still works under Turbo"
non-regression test) -- and unconditionally called the 1-argument runtime
entry point, silently DISCARDING the filename argument whenever one was
given.  Since a file variable's bound Name starts out empty (the zero-
initialized default), and an empty bound name means "the console" (a
deliberate, real TP idiom for Reset/Rewrite/Append with NO name), the
practical effect was severe: `rewrite(f, 'realname.txt')` under -std=turbo
silently wrote to STDOUT instead of creating the named file at all, and a
following `reset(f, 'realname.txt')` silently read from STDIN instead of
the file -- which, run without a redirected stdin, HANGS waiting for
input that never arrives, rather than failing loudly.  Fixed by making the
2-argument case perform an IMPLICIT Assign (bind the name first, exactly
the same plang_tp_assign call the explicit `assign` builtin itself makes)
before opening -- this file proves the fix by writing and then reading
back real content through the 2-argument form alone, with no explicit
Assign call anywhere in this program.

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
  rewrite(f, 'reset-rewrite-two-argument-form-under-turbo-actually-opens-the-named-file.txt');
  writeln(f, 'HELLO');
  close(f);
  reset(f, 'reset-rewrite-two-argument-form-under-turbo-actually-opens-the-named-file.txt');
  readln(f, s);
  writeln(s)
end.
