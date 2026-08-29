(*
TP7 ch.19's storage-width-selection rule also narrows an ENUM's own
storage by its member count under -std=turbo (SemaType.cpp's
EnumTypeNode arm, via TypeContext::narrowestStorage fed the enum's
implicit 0..count-1 range): ISO §6.4.2.2 numbers an enumeration's values
from zero, so an enum of at most 256 members fits an unsigned byte
(0..255) and one with more needs a second byte.  Verified against a real
Turbo-Pascal-mode compiler (`fpc -Mtp`), which narrows a 300-member enum
to 2 bytes on a local install exactly the same way.

`Small` (3 members) and `Big257` (257 members, one more than a byte can
number) sit side by side in a record so the two widths -- 1 byte and 2 --
show up together in a single struct literal.  That struct spelling can
only ever appear as real LLVM output, never as valid Pascal, so it lives
outside the compiled chunk -- see split-file below.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang -std=turbo -emit-llvm %t.dir/test.pas -o %t.ll
RUN: FileCheck %s < %t.ll
RUN: %plang -std=turbo %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --check-prefix=RUNS --strict-whitespace --match-full-lines %s
*)

(*
CHECK: { i8, i16 }
RUNS:1 256
*)

//--- test.pas
program p;
type
  Small = (s0, s1, s2);
  Big257 = (
    m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11,
    m12, m13, m14, m15, m16, m17, m18, m19, m20, m21, m22, m23,
    m24, m25, m26, m27, m28, m29, m30, m31, m32, m33, m34, m35,
    m36, m37, m38, m39, m40, m41, m42, m43, m44, m45, m46, m47,
    m48, m49, m50, m51, m52, m53, m54, m55, m56, m57, m58, m59,
    m60, m61, m62, m63, m64, m65, m66, m67, m68, m69, m70, m71,
    m72, m73, m74, m75, m76, m77, m78, m79, m80, m81, m82, m83,
    m84, m85, m86, m87, m88, m89, m90, m91, m92, m93, m94, m95,
    m96, m97, m98, m99, m100, m101, m102, m103, m104, m105, m106, m107,
    m108, m109, m110, m111, m112, m113, m114, m115, m116, m117, m118, m119,
    m120, m121, m122, m123, m124, m125, m126, m127, m128, m129, m130, m131,
    m132, m133, m134, m135, m136, m137, m138, m139, m140, m141, m142, m143,
    m144, m145, m146, m147, m148, m149, m150, m151, m152, m153, m154, m155,
    m156, m157, m158, m159, m160, m161, m162, m163, m164, m165, m166, m167,
    m168, m169, m170, m171, m172, m173, m174, m175, m176, m177, m178, m179,
    m180, m181, m182, m183, m184, m185, m186, m187, m188, m189, m190, m191,
    m192, m193, m194, m195, m196, m197, m198, m199, m200, m201, m202, m203,
    m204, m205, m206, m207, m208, m209, m210, m211, m212, m213, m214, m215,
    m216, m217, m218, m219, m220, m221, m222, m223, m224, m225, m226, m227,
    m228, m229, m230, m231, m232, m233, m234, m235, m236, m237, m238, m239,
    m240, m241, m242, m243, m244, m245, m246, m247, m248, m249, m250, m251,
    m252, m253, m254, m255, m256
  );
  Rec = record
    sm: Small;
    lg: Big257;
  end;
var v: Rec;
begin
  v.sm := s1;
  v.lg := m256;
  writeln(ord(v.sm), ' ', ord(v.lg));
end.
