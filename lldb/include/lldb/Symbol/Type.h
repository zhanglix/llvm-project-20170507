//===-- Type.h --------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Type_h_
#define liblldb_Type_h_

#include "lldb/lldb-private.h"
#include "lldb/Core/ClangForward.h"
#include "lldb/Core/UserID.h"
#include "lldb/Symbol/Declaration.h"
#include <set>

namespace lldb_private {

class Type : public UserID
{
public:
    typedef enum
    {
        eEncodingInvalid,
        eEncodingIsUID,         ///< This type is the type whose UID is m_encoding_uid
        eEncodingIsConstUID,            ///< This type is the type whose UID is m_encoding_uid with the const qualifier added
        eEncodingIsRestrictUID,         ///< This type is the type whose UID is m_encoding_uid with the restrict qualifier added
        eEncodingIsVolatileUID,         ///< This type is the type whose UID is m_encoding_uid with the volatile qualifier added
        eEncodingIsTypedefUID,          ///< This type is pointer to a type whose UID is m_encoding_uid
        eEncodingIsPointerUID,          ///< This type is pointer to a type whose UID is m_encoding_uid
        eEncodingIsLValueReferenceUID,  ///< This type is L value reference to a type whose UID is m_encoding_uid
        eEncodingIsRValueReferenceUID,  ///< This type is R value reference to a type whose UID is m_encoding_uid
        eEncodingIsSyntheticUID
    } EncodingDataType;

    Type (lldb::user_id_t uid,
          SymbolFile* symbol_file,
          const ConstString &name,
          uint32_t byte_size,
          SymbolContextScope *context,
          uintptr_t encoding_uid,
          EncodingDataType encoding_type,
          const Declaration& decl,
          lldb::clang_type_t clang_qual_type,
          bool is_forward_decl);
    
    // This makes an invalid type.  Used for functions that return a Type when they
    // get an error.
    Type();

    const Type&
    operator= (const Type& rhs);

    void
    Dump(Stream *s, bool show_context);

    void
    DumpTypeName(Stream *s);


    void
    GetDescription (Stream *s, lldb::DescriptionLevel level, bool show_name);

    SymbolFile *
    GetSymbolFile()
    {
        return m_symbol_file;
    }
    const SymbolFile *
    GetSymbolFile() const
    {
        return m_symbol_file;
    }

    TypeList*
    GetTypeList();

    const ConstString&
    GetName();

    uint64_t
    GetByteSize();

    uint32_t
    GetNumChildren (bool omit_empty_base_classes);

    bool
    IsAggregateType ();

    bool
    IsValidType ()
    {
        return m_encoding_uid_type != eEncodingInvalid;
    }

    void
    SetByteSize(uint32_t byte_size);

    const ConstString &
    GetName () const
    {
        return m_name;
    }

    void
    DumpValue(ExecutionContext *exe_ctx,
              Stream *s,
              const DataExtractor &data,
              uint32_t data_offset,
              bool show_type,
              bool show_summary,
              bool verbose,
              lldb::Format format = lldb::eFormatDefault);

    bool
    DumpValueInMemory(ExecutionContext *exe_ctx,
                      Stream *s,
                      lldb::addr_t address,
                      lldb::AddressType address_type,
                      bool show_types,
                      bool show_summary,
                      bool verbose);

    bool
    ReadFromMemory (ExecutionContext *exe_ctx,
                    lldb::addr_t address,
                    lldb::AddressType address_type,
                    DataExtractor &data);

    bool
    WriteToMemory (ExecutionContext *exe_ctx,
                   lldb::addr_t address,
                   lldb::AddressType address_type,
                   DataExtractor &data);

    bool
    GetIsDeclaration() const;

    void
    SetIsDeclaration(bool b);

    bool
    GetIsExternal() const;

    void
    SetIsExternal(bool b);

    lldb::Format
    GetFormat ();

    lldb::Encoding
    GetEncoding (uint32_t &count);

    SymbolContextScope *
    GetSymbolContextScope()
    {
        return m_context;
    }
    const SymbolContextScope *
    GetSymbolContextScope() const
    {
        return m_context;
    }
    void
    SetSymbolContextScope(SymbolContextScope *context)
    {
        m_context = context;
    }

    const lldb_private::Declaration &
    GetDeclaration () const;

    // Get the clang type, and resolve definitions for any 
    // class/struct/union/enum types completely.
    lldb::clang_type_t 
    GetClangType ();

    // Get the clang type and leave class/struct/union/enum types as forward
    // declarations if they haven't already been fully defined.
    lldb::clang_type_t 
    GetClangForwardType ();

    clang::ASTContext *
    GetClangAST ();

    ClangASTContext &
    GetClangASTContext ();

    lldb::clang_type_t 
    GetChildClangTypeAtIndex (const char *parent_name,
                              uint32_t idx,
                              bool transparent_pointers,
                              bool omit_empty_base_classes,
                              ConstString& name,
                              uint32_t &child_byte_size,
                              int32_t &child_byte_offset,
                              uint32_t &child_bitfield_bit_size,
                              uint32_t &child_bitfield_bit_offset,
                              bool &child_is_base_class);

    static int
    Compare(const Type &a, const Type &b);

protected:
    ConstString m_name;
    SymbolFile *m_symbol_file;
    SymbolContextScope *m_context; // The symbol context in which this type is defined
    Type *m_encoding_type;
    EncodingDataType m_encoding_uid_type;
    uint32_t m_encoding_uid;
    uint32_t m_byte_size;
    Declaration m_decl;
    lldb::clang_type_t m_clang_type;
    bool    m_is_forward_decl:1,
            m_encoding_type_forward_decl_resolved:1,
            m_encoding_type_decl_resolved:1;

    Type *
    GetEncodingType ();
    
    bool ResolveClangType(bool forward_decl_is_ok = false);
};

} // namespace lldb_private

#endif  // liblldb_Type_h_

