(*
The other half of the sibling default-off test: an explicit {$R+} under
Turbo still range-checks, and when it catches something the failure goes
through the NEW plang_tp_runerror(201) reporter -- "Runtime error 201 at
$<addr>", exit status 201 itself -- never the shared ISO/EP
plang_err_range/plang_err_index path (exit PlangRuntimeErrorStatus, 70,
"plang runtime: ..." wording).  Borland/FPC's own "Runtime error 201:
Range check error" (confirmed against `fpc -Mtp`) covers both an
array-index failure and a plain subrange-assignment failure with the SAME
number; this uses the subrange shape (see the sibling default-off test's
own comment on why that is the safe, deterministic one to write to with
range checking's actual on/off state uncertain at compile time -- here it
is known ON, but keeping the same shape as that sibling keeps the two
directly comparable).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 201 %run %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: Runtime error 201 at $
CHECK-NOT: plang runtime:
*)

program explicitrplus;
{$R+}
var
  s: 1..10;
  i: Integer;
begin
  i := 500;
  s := i;
  writeln('unreachable: {$R+} did not take effect');
end.
