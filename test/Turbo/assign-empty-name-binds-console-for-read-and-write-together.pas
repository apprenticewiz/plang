(*
Tier 3 capstone (integration): Assign(f, '') as ONE coherent console-binding
mechanism, confirmed across a realistic read-then-write scenario in a
single program, rather than the two separate Reset-only/Rewrite-only
proofs (assign-empty-name-reset-reads-the-console.pas,
assign-empty-name-rewrite-writes-the-console.pas, both
test/CodeGen/Turbo/) exercising it in isolation. A realistic idiom this
proves end to end: read a line typed at the console, transform it, and
echo the result back to the console, all through the SAME file variable
rebound between the two operations via a fresh Assign('') + the
appropriate open call.

RUN: %plang -std=turbo %s -o %t
RUN: echo -n "hello console" | %run %t | FileCheck %s
*)

(*
CHECK:you typed: hello console
CHECK-NEXT:transformed: HELLO CONSOLE
*)

var f: text; s: string; i: Integer;
begin
  assign(f, '');
  reset(f);
  readln(f, s);
  close(f);

  writeln('you typed: ', s);

  for i := 1 to Length(s) do
    s[i] := UpCase(s[i]);

  assign(f, '');
  rewrite(f);
  writeln(f, 'transformed: ', s);
  close(f);
end.
