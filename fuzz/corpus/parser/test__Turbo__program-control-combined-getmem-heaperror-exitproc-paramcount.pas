(*
Tier 3 capstone (integration): heap/program-control mechanisms already
proven individually (getmem-without-heaperror-on-out-of-memory-returns-
nil-not-abort.pas, heaperror-returning-1-makes-getmem-return-nil-and-runs-
the-handler.pas, exitproc-runs-before-halt-terminates-the-process.pas,
paramcount-and-paramstr-report-the-real-command-line-arguments.pas --
all test/CodeGen/Turbo/, and each already a genuine end-to-end compiled
program, not a mock) composed into ONE realistic program: read a
command-line argument, attempt an allocation sized far beyond anything
that can succeed, recover from the failure via a HeapError handler that
logs what it saw, and run cleanup logic through ExitProc when the program
then Halts with a status derived from what it did -- the actual reason a
real Turbo program installs both HeapError and ExitProc together (log/
clean-up-on-the-way-out), rather than each mechanism in its own vacuum.

RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 7 %run %t widget | FileCheck %s
*)

(*
CHECK:program started with arg1=widget
CHECK-NEXT:HeapError handler invoked, size=9000000000000000000
CHECK-NEXT:GetMem returned nil as expected
CHECK-NEXT:ExitProc handler ran, cleaning up widget
CHECK-NEXT:ExitCode as seen inside ExitProc=7
*)

program combinedheap;
var
  p: Pointer;
  heapErrorCalls: Integer;

function MyHeapError(Size: Int64): Int64;
begin
  heapErrorCalls := heapErrorCalls + 1;
  writeln('HeapError handler invoked, size=', Size);
  MyHeapError := 1; { "handled, give me nil" }
end;

procedure Cleanup;
begin
  writeln('ExitProc handler ran, cleaning up ', ParamStr(1));
  writeln('ExitCode as seen inside ExitProc=', ExitCode);
end;

begin
  heapErrorCalls := 0;
  writeln('program started with arg1=', ParamStr(1));

  HeapError := MyHeapError;
  ExitProc := Cleanup;

  GetMem(p, 9000000000000000000);
  if p = nil then
    writeln('GetMem returned nil as expected')
  else
    writeln('FAIL: GetMem returned non-nil');

  ExitCode := heapErrorCalls * 7; { Halt's own status argument never touches
                                     ExitCode -- the two are deliberately
                                     independent mechanisms (see
                                     exitcode-assigned-before-the-program-
                                     ends-becomes-the-process-exit-status.pas's
                                     own comment) -- so a program that wants
                                     ExitProc to see a meaningful ExitCode
                                     must set it itself first. }
  Halt(heapErrorCalls * 7); { heapErrorCalls is 1: Halt(7) }
end.
