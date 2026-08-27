(*
Issue #279: an extra file's own .o is only ever an intermediate needed to
link the final binary, exactly like the main file's own object -- but
unlike the main file's object (kept only under -save-temps, a real temp
file otherwise, removed once the link step is done with it), an extra
file's object was *always* written to a flattenedStem-derived name in the
cwd and never removed, -save-temps or not.  A plain multi-file build
("plang ok.pas aux.pas -o prog") left aux.o sitting in the cwd forever
after a successful link.

Fixed by giving the extra file's object the same OwnObj-style temp-file
treatment the main file's own object already had: a real temp file, cleaned
up once the link step no longer needs it, unless -save-temps asked for it
to be kept -- in which case it is still written under its (now
collision-disambiguated, issue #170) flattenedStem name and left alone, on
purpose, matching the main file's own -save-temps object.

The plain and -save-temps builds use separate split-file subdirectories
("plain/", "keep/"), not the same one in sequence: split-file re-extracts
its own known files on every run but does not clear anything else already
sitting in %t.dir, so the second (-save-temps) build's aux.o -- which this
test deliberately expects to survive -- would otherwise still be sitting
there on a second lit invocation reusing the same %t.dir, making the first
build's "must not exist" check pass or fail depending on unrelated prior
runs instead of on this run's own behavior.
*)

(*
RUN: split-file %s %t.dir
RUN: cd %t.dir/plain && %plang -std=iso10206 -I. main.pas aux.pas -o prog
RUN: not test -e %t.dir/plain/aux.o
RUN: %run %t.dir/plain/prog | FileCheck --strict-whitespace --match-full-lines %s
RUN: cd %t.dir/keep && %plang -std=iso10206 -save-temps -I. main.pas aux.pas -o prog
RUN: test -e %t.dir/keep/aux.o
*)

(*
CHECK:36
*)

//--- plain/aux.pas
module M;
function Square(x: integer): integer;
begin Square := x * x end;
end.

//--- plain/main.pas
program p;
import M;
begin writeln(Square(6)) end.

//--- keep/aux.pas
module M;
function Square(x: integer): integer;
begin Square := x * x end;
end.

//--- keep/main.pas
program p;
import M;
begin writeln(Square(6)) end.
