(*
issues #602/#653: Halt, RunError and Delay declared their one argument
AK_Any (Builtins.def), so Sema's generic checkBuiltinArgKinds pass had
nothing to enforce and a real-valued argument sailed through to CodeGen,
which just converted it to an integer -- fpc -Mtp rejects all three with an
"Incompatible type for arg no. 1... expected LongInt/Word" diagnostic.  Now
AK_Integer (isIntegral(): Integer or a subrange of it, unlike AK_Ordinal
which would wrongly still accept Boolean/Char/Enum, or AK_Numeric which
would wrongly still accept Real).
*)

(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'delay' requires an integer argument, got 'real'
CHECK: 'runerror' requires an integer argument, got 'real'
CHECK: 'halt' requires an integer argument, got 'real'
*)

program p;
begin
  Delay(1.5);
  RunError(1.5);
  Halt(3.7)
end.
