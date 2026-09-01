(*
Tier 3 capstone (integration): Rewrite then, in a LATER open, Append --
proving the full lifecycle rather than Append's own isolated unit proof
(assign-append-appends-to-a-named-file.pas, test/CodeGen/Turbo/, which
starts from a fixture file `printf`'d into existence outside the compiled
program entirely). Here the SAME program writes the original content with
Rewrite, closes it, reopens with Append and writes more, closes again, and
a THIRD open (a plain Reset) reads the whole file back to confirm it holds
BOTH the original Rewrite content and the appended content, in order, with
nothing lost or reordered.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:line1
CHECK-NEXT:line2
CHECK-NEXT:appended1
CHECK-NEXT:appended2
CHECK-NEXT:read back 4 lines total
*)

var
  f: text;
  s: string;
  count: Integer;
begin
  assign(f, 'append-after-rewrite-preserves-and-extends-content.txt');
  rewrite(f);
  writeln(f, 'line1');
  writeln(f, 'line2');
  close(f);

  assign(f, 'append-after-rewrite-preserves-and-extends-content.txt');
  append(f);
  writeln(f, 'appended1');
  writeln(f, 'appended2');
  close(f);

  assign(f, 'append-after-rewrite-preserves-and-extends-content.txt');
  reset(f);
  count := 0;
  while not eof(f) do begin
    readln(f, s);
    writeln(s);
    count := count + 1;
  end;
  close(f);
  writeln('read back ', count, ' lines total');
end.
