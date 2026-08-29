(*
TP `Randomize` (Builtins.def, -std=turbo only) reseeds RandSeed from
wall-clock time -- runtime/plang_math.cpp's plang_tp_randomize, which reads
clock_gettime(CLOCK_REALTIME) rather than plang_gettimestamp/time_t
(plang_time.cpp), whose one-second resolution would make two runs started in
the same second seed identically and defeat the entire point.

This is the one property of Randomize a purely in-process test cannot
exercise: a single process only ever calls Randomize once and has no
"previous run" of its own to compare against.  So this test actually RUNS
the same compiled binary twice, a short sleep apart, and confirms their two
RandSeed outputs differ -- `diff` exits nonzero exactly when its two inputs
differ, so `not diff` is the RUN line that passes when Randomize did its job.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t > %t.out1
RUN: sleep 0.05
RUN: %run %t > %t.out2
RUN: not diff %t.out1 %t.out2
*)

program p;
begin
  Randomize;
  writeln(RandSeed);
end.
