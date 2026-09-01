(*
Issue #180 item 3 / issue #304's own negative cases: the new mtime staleness
check (see the companion positive test,
a-published-tui-warns-when-its-source-is-newer.pas) must not fire when
there is nothing stale to report, and must be entirely silent -- not merely
non-fatal -- when there is no companion source alongside the .tui at all,
which is the ordinary case for a vendored/shipped unit and must see zero
behavior change.

Two scenarios in one file: (1) compile the unit, then immediately compile
the consumer with the unit's .pas still sitting right there and untouched --
up to date by construction, no warning; (2) delete the .pas afterward and
recompile the consumer again -- no companion source at all, so the check
does not even run.

RUN: rm -rf %t.dir
RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/greetunit.pas -o %t.dir/greetunit.o
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/greetunit.o -o %t.exe1 2> %t.err1
RUN: FileCheck %s --allow-empty < %t.err1
RUN: rm %t.dir/greetunit.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/greetunit.o -o %t.exe2 2> %t.err2
RUN: FileCheck %s --allow-empty < %t.err2
*)

(*
CHECK-NOT: may be stale
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
