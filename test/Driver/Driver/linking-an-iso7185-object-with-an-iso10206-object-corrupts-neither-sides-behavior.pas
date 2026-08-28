(*
Turbo Pascal milestone prerequisite P7: libplang.a is a SINGLE archive linked
into every compiled program, no matter which -std= produced any particular
.o/.a input on the command line (lib/Driver/Driver.cpp never inspects a
precompiled object to learn which dialect made it). The design that makes
that safe never consults a process-global "current dialect" word at runtime;
each object's own compiled code always calls whichever runtime entry point
its own dialect requires. That claim has never actually been exercised:
linker-only-invocation-links-precompiled-object-files.pas links two objects
together twice, but every object in both of its scenarios shares one -std=
value throughout (the default/iso7185 for the first, -std=iso10206 for the
second). Neither scenario ever links an -std=iso7185 object together with an
-std=iso10206 object.

Extended Pascal's module system can't provide that directly: both `module`
and `import` are EPKEYWORD entries in TokenKinds.def, reserved only under
-std=iso10206, so a module -- and whatever program imports one -- can only
ever be compiled Extended Pascal. There is no way for an -std=iso7185
`program` (the only top-level unit ISO 7185 has at all) to import an
Extended Pascal module; the importing side would need EP too. So this test
proves the safety claim the way it is actually reachable, in two steps
sharing one Extended Pascal object file:

  1. fmt.o (-std=iso10206) is linked with a normal EP importer to show its
     use of `writestr` -- an Extended Pascal extension StandardGate rejects
     outright under -std=iso7185 (EveryExtensionIsTurnedAwayUnderIso7185's
     'writestr' case) -- is live code that produces the right string, not
     dead weight nobody runs.
  2. That *same* fmt.o is then linked together with a program compiled
     -std=iso7185 that redeclares `otherwise` as an ordinary variable.
     `otherwise` is reserved only by Extended Pascal (EPKEYWORD(Otherwise);
     it spells EP's case-statement default arm), so the program only
     compiles under -std=iso7185. The mixed link succeeds, and the ISO 7185
     program's own output is exactly what it is when linked alone -- proving
     that linking a genuinely Extended-Pascal-compiled object into the same
     binary, sharing the same single libplang.a, does not leak into or
     corrupt an ISO-7185-compiled object's behavior.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/fmt.pas -o %t.dir/fmt.o
RUN: %plang -std=iso10206 -I%t.dir -c %t.dir/ep_prog.pas -o %t.dir/ep_prog.o
RUN: %plang %t.dir/ep_prog.o %t.dir/fmt.o -o %t.dir/ep_linked
RUN: %run %t.dir/ep_linked | FileCheck --check-prefix=EP --strict-whitespace --match-full-lines %s
RUN: %plang -std=iso7185 -c %t.dir/iso_prog.pas -o %t.dir/iso_prog.o
RUN: %plang %t.dir/iso_prog.o %t.dir/fmt.o -o %t.dir/mixed_linked
RUN: %run %t.dir/mixed_linked | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
EP:[42]
CHECK:otherwise=41
*)

//--- fmt.pas
module Fmt;
procedure ShowFortyTwo;
var s: string(10);
begin writestr(s, 42); writeln('[', s, ']') end;
end.

//--- ep_prog.pas
program p;
import Fmt;
begin ShowFortyTwo end.

//--- iso_prog.pas
program p;
var otherwise: integer;
begin otherwise := 41; writeln('otherwise=', otherwise) end.
