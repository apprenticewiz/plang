#include "plang/Sema/DumpVmt.h"

#include "plang/AST/Ast.h"
#include "plang/Sema/Type.h"

using namespace plang;

namespace {

void printOneObject(const std::string& DeclName, const Type& T, std::ostream& Os) {
    Os << "(vmt " << DeclName;
    if (T.Parent) Os << " (ancestor " << T.Parent->Name << ")";
    Os << "\n  (fields";
    for (const auto& F : T.RecordFields) {
        Os << " (" << F.Name << " " << (F.Ty ? F.Ty->Name : std::string("?"))
           << (F.IsPrivate ? " private" : " public") << ")";
    }
    Os << ")\n";
    Os << "  (methods";
    for (const auto& M : T.ObjectMethods) {
        Os << " (" << M.Name;
        if (M.IsConstructor)      Os << " constructor";
        else if (M.IsDestructor)  Os << " destructor";
        else if (M.IsFunction)    Os << " function";
        else                      Os << " procedure";
        if (M.IsVirtual)  Os << " virtual";
        if (M.IsAbstract) Os << " abstract";
        if (M.VmtSlot >= 0) Os << " slot=" << M.VmtSlot;
        Os << ")";
    }
    Os << ")\n";
    Os << "  (slots";
    for (size_t I = 0; I < T.VmtSlots.size(); ++I) {
        const auto& Slot = T.VmtSlots[I];
        Os << " (" << I << " " << Slot.MethodName << " " << Slot.ImplementingType << ")";
    }
    Os << "))\n";
}

/// Walks one block's own Types list, printing every object type found in
/// declaration order.  Anything that did not resolve to TypeKind::Object
/// (an ordinary type, or an object type Sema itself rejected -- see
/// DumpVmt.h's own comment on the TyErr case) is silently skipped.
void printBlockObjects(const BlockNode& Block, std::ostream& Os) {
    for (const auto& Td : Block.Types) {
        if (!Td.Type || !Td.Type->ResolvedType) continue;
        const Type& T = *Td.Type->ResolvedType;
        if (T.Kind != TypeKind::Object) continue;
        printOneObject(Td.Name, T, Os);
    }
}

} // namespace

void plang::printVmt(const ProgramNode& Program, std::ostream& Os) {
    if (Program.BareUnit) {
        if (Program.BareUnit->InterfaceBlock)
            printBlockObjects(*Program.BareUnit->InterfaceBlock, Os);
        if (Program.BareUnit->ImplementationBlock)
            printBlockObjects(*Program.BareUnit->ImplementationBlock, Os);
        return;
    }
    for (const auto* Mod : Program.Modules)
        if (Mod->Body) printBlockObjects(*Mod->Body, Os);
    if (Program.Block) printBlockObjects(*Program.Block, Os);
}
