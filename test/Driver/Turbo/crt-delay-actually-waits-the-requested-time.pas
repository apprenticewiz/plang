(*
Turbo Tier 4, Cluster C item 5: Delay(MS) is a real nanosleep-based wait
(runtime/plang_crt.cpp's own plang_crt_delay), not a no-op -- checked here
by timing a real 200ms Delay from outside the program (test/tools/timed-
run-at-least-ms.sh), with a generously loose lower bound (150ms) to absorb
process-start/scheduling jitter without the test itself becoming flaky,
rather than asserting anything close to the requested 200ms exactly.

RUN: %plang -std=turbo %s -o %t
RUN: %timed_run_at_least 150 %run %t | FileCheck %s
*)
program DelayReallyWaits;
uses Crt;
begin
  Delay(200);
end.
(*
CHECK: elapsed_ms_ok=1
*)
