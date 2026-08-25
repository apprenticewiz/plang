(*
ISO section 6.6.3.7.2 permits an import clause to list several
specifications, and each must keep its OWN qualifier/rename independent
of the others in the list -- not fall back to whatever the previous
specification asked for.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/moda.pas -o %t.dir/moda.o
RUN: %plang -std=iso10206 -c %t.dir/modb.pas -o %t.dir/modb.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/moda.o %t.dir/modb.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

//--- moda.pas
module Alpha;
function AlphaVal: integer;
begin AlphaVal := 10 end;
end.

//--- modb.pas
module Beta;
function BetaVal: integer;
begin BetaVal := 32 end;
end.

//--- prog.pas
program p(output);
import Alpha qualified; Beta only (BetaVal);
begin writeln(Alpha.AlphaVal + BetaVal) end.
