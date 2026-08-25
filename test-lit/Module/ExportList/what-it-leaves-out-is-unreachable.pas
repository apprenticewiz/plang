(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR-DAG: secret
ERR-DAG: hidden
*)

module m interface;
  export m = (visible);
  function visible: integer;
end.
module m;
  var hidden: integer;
  function visible: integer; begin visible := 7 end;
  procedure secret; begin hidden := 1 end;
end.
program p;
  import m;
begin secret; writeln(hidden) end.
