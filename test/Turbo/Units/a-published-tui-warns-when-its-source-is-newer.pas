(*
Issue #180 item 3 / issue #304: neither a .tui nor its paired .o carries any
version/fingerprint metadata that would catch this on its own (#180's own
re-triage comment), so this is the cheap mtime-based substitute that closes
the actual reported bug.

Empirically reproduced by hand before this test was written: compile
GreetUnit, build+run a program against it (correct output), edit ONLY the
unit's own implementation (its interface text is byte-identical) without
recompiling the unit, then recompile only the consuming program.  Before
this change: silent, wrong output, zero diagnostic.  After: a warning names
both files, though the object linked is (correctly, since this is a warning
and not a hard error) still the stale one -- recompiling the unit is left to
the user, exactly like a stale .o after editing a header and forgetting to
rebuild in any C/C++ build system without dependency tracking.

`touch -d` (not a real edit + a `sleep`) pins the source's mtime far in the
future, so the pass/fail here depends on `>` comparison logic alone, never
on how fast this machine's clock ticks relative to filesystem mtime
resolution.

RUN: rm -rf %t.dir
RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/greetunit.pas -o %t.dir/greetunit.o
RUN: touch -d "2099-01-01 00:00:00" %t.dir/greetunit.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/greetunit.o -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: warning: '{{.*}}greetunit.tui' may be stale: its source '{{.*}}greetunit.pas' was modified more recently; recompile '{{.*}}greetunit.pas' to refresh it
*)

//--- greetunit.pas
unit GreetUnit;

interface

function Greeting: string;

implementation

function Greeting: string;
begin
  Greeting := 'hello';
end;

end.

//--- main.pas
program Main;
uses GreetUnit;
begin
  Writeln(Greeting);
end.
