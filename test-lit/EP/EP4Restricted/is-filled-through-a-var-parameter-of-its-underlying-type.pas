(*
Compiled from a copy in %t.dir, not %s directly: this program declares a
module, and the compiler writes its .pmi beside whatever file it compiled
-- compiling %s in place would write the .pmi into the checked-in source
tree itself, on every test run.

RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

//--- test.pas
module m interface;
export m = (handle, seth, showh);
type rep = record n: integer end;
     handle = restricted rep;
procedure seth(var h: rep; v: integer);
procedure showh(h: rep);
end;
procedure seth; begin h.n := v end;
procedure showh; begin writeln(h.n) end;
end.
program p(output);
import m;
var x: handle;
begin seth(x, 7); showh(x) end.
