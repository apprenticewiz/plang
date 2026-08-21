#include "FileVarHelpers.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/StringUtil.h"

using namespace plang;

bool FileVarHelpers::isTextTypeName(const TypeNode* tn) {
    if (auto* nn = llvm::dyn_cast<NamedTypeNode>(tn))
        return eqCI(nn->Name, "text");
    return false;
}

bool FileVarHelpers::isFileVar(const ExprNode& e) {
    // ISO §6.6.5.2 takes a file-variable, and a variable is anything §6.5
    // calls one: an element of an array of text is as much a file as a
    // variable whose name is written on its own.  Reading only the name meant
    // `rewrite(avf[i])` found nothing and passed null to the runtime.
    if (e.ResolvedType && e.ResolvedType->Kind == TypeKind::File) return true;
    if (auto* id = llvm::dyn_cast<IdentExpr>(&e)) {
        auto* ve = SymTab.findVar(id->Name);
        if (!ve) return false;
        // Program file-parameters have no declaring TypeNode, so fall back to
        // the storage type, which is authoritative either way.
        if (ve->type == Types.fileStructType()) return true;
        if (ve->typeNode)
            return llvm::dyn_cast<FileTypeNode>(ve->typeNode) != nullptr
                || isTextTypeName(ve->typeNode);
    }
    return false;
}

llvm::Value* FileVarHelpers::fileVarPtr(const ExprNode& e) {
    if (auto* id = llvm::dyn_cast<IdentExpr>(&e)) {
        auto* ve = SymTab.findVar(id->Name);
        if (ve) return ve->ptr;
    }
    // A component of a structure, or whatever a pointer leads to: the address
    // is what the runtime wants and emitLValue is what produces one.
    return isFileVar(e) ? EmitLValue(e) : nullptr;
}

const Type* FileVarHelpers::fileTypeOf(const ExprNode& e) {
    if (const Type* T = e.ResolvedType.get())
        if (T->Kind == TypeKind::File) return T;
    if (auto* id = llvm::dyn_cast<IdentExpr>(&e))
        if (auto* ve = SymTab.findVar(id->Name))
            if (ve->typeNode && ve->typeNode->ResolvedType
                    && ve->typeNode->ResolvedType->Kind == TypeKind::File)
                return ve->typeNode->ResolvedType.get();
    return nullptr;
}

bool FileVarHelpers::isTypedBinaryFileVar(const ExprNode& e) {
    // Whether the file holds records or characters decides which of two
    // unrelated representations it is written in, so reading the denoter and
    // taking a name for a text file is not a near miss.  `file of integer`
    // under a type name went down the text path and wrote its numbers out as
    // digits, and read them back as one number with the digits run together.
    const Type* T = fileTypeOf(e);
    if (!T || !T->ElemType) return false;   // untyped: byte-level
    // ISO §6.4.3.5: a file of char is a text file.
    return T->ElemType->Kind != TypeKind::Char;
}

llvm::Value* FileVarHelpers::fileBufferPtr(const ExprNode& fileExpr) {
    auto* fp = fileVarPtr(fileExpr);
    if (!fp) return nullptr;
    // §6.4.3.5 gives f^ the value of a space when eoln(f), which only holds of
    // a text file: on a one-byte binary file the same byte is a component with
    // the value 10 and has to arrive intact.  The two are indistinguishable
    // from the component size alone.
    auto* fn = RtFns.getExternFnN("plang_file_buffer", PtrTy, {PtrTy, I64Ty, I8Ty});
    return B.CreateCall(
        fn,
        {fp, llvm::ConstantInt::get(I64Ty, getFileElemSize(fileExpr)),
         llvm::ConstantInt::get(I8Ty, isTypedBinaryFileVar(fileExpr) ? 0 : 1)},
        "file.buf");
}

llvm::Type* FileVarHelpers::getFileElemType(const ExprNode& fileExpr) {
    if (auto* id = llvm::dyn_cast<IdentExpr>(&fileExpr))
        if (auto* ve = SymTab.findVar(id->Name))
            if (auto* ftn = llvm::dyn_cast_or_null<FileTypeNode>(ve->typeNode))
                if (ftn->Element) return Types.llvmTypeOfNode(*ftn->Element);
    // The denoter recorded for the variable is a name when the file type was
    // declared under one, and a name says nothing about what the file holds.
    // Coming away with nothing here means an element size of one byte, so
    // every record read or written would be a byte of it.
    if (const Type* T = fileTypeOf(fileExpr))
        if (T->ElemType) return Types.llvmTypeOfSemaType(*T->ElemType);
    return nullptr;
}

int64_t FileVarHelpers::getFileElemSize(const ExprNode& fileExpr) {
    if (auto* elemTy = getFileElemType(fileExpr))
        return (int64_t)Mod.getDataLayout().getTypeAllocSize(elemTy);
    return 1; // text, or an untyped file: byte-level
}

int64_t FileVarHelpers::getFileIndexLow(const ExprNode& fileExpr) {
    if (const Type* T = fileTypeOf(fileExpr))
        if (T->IndexType) return T->IndexType->SubLo;
    return 0;
}
