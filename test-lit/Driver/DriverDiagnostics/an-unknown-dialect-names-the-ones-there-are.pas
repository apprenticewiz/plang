(*
RUN: not %plang_ir -std=klingon /dev/null > %t.out 2>&1
RUN: FileCheck --check-prefix=OUT %s < %t.out
*)

(*
OUT-DAG: plang: error: unknown Pascal dialect 'klingon'
OUT-DAG: iso7185
*)
