#ifndef DOKAN_LOG_H
#define DOKAN_LOG_H
#include <string>

#define LOG_RETURN(func_name, ret_val)             \
    log_msg(#func_name " return: " #ret_val "\n"); \
    return (ret_val)

#define log_struct(indent, st, field, format, ...) \
    log_msg(indent "(" #st ")->" #field " = " format "\n", __VA_ARGS__(st->field))

#define log_wstring(indent, name) log_msg(indent #name " = %s\n", wide2local(name).c_str())

#define log_int32(indent, name) log_msg(indent #name " = %d\n", name)

#define log_int64(indent, name) log_msg(indent #name " = %lld\n", name)

#define log_uint32(indent, name) log_msg(indent #name " = 0x%08x\n", name)

#define log_uint64(indent, name) log_msg(indent #name " = 0x%016x\n", name)

#define log_bool(indent, name) log_msg(indent #name " = %s\n", name ? "true" : "false");

#define log_pointer(indent, name) log_msg(indent #name " = %p\n", name)

#define log_pdokan_file_info(indent, name)                         \
    do                                                             \
    {                                                              \
        log_pointer(indent, name);                                 \
        log_struct(indent "    ", name, Context, "0x%016x");       \
        log_struct(indent "    ", name, DokanContext, "0x%016x");  \
        log_struct(indent "    ", name, ProcessId, "0x%08x");      \
        log_struct(indent "    ", name, IsDirectory, "%hhu");      \
        log_struct(indent "    ", name, DeleteOnClose, "%hhu");    \
        log_struct(indent "    ", name, PagingIo, "%hhu");         \
        log_struct(indent "    ", name, SynchronousIo, "%hhu");    \
        log_struct(indent "    ", name, Nocache, "%hhu");          \
        log_struct(indent "    ", name, WriteToEndOfFile, "%hhu"); \
    } while (0)

#define log_lpby_handle_information(indent, name)                        \
    do                                                                   \
    {                                                                    \
        log_pointer(indent, name);                                       \
        log_struct(indent "    ", name, dwFileAttributes, "0x%08x");     \
        log_struct(indent "    ", name, dwVolumeSerialNumber, "0x%08x"); \
        log_struct(indent "    ", name, nFileSizeHigh, "0x%08x");        \
        log_struct(indent "    ", name, nFileSizeLow, "0x%08x");         \
        log_struct(indent "    ", name, nNumberOfLinks, "0x%08x");       \
        log_struct(indent "    ", name, nFileIndexHigh, "0x%08x");       \
        log_struct(indent "    ", name, nFileIndexLow, "0x%08x");        \
    } while (0)

#define LOG_ZwCreateFile()                           \
    do                                               \
    {                                                \
        log_msg("ZwCreateFile:\n");                  \
        log_wstring("    ", FileName);               \
        log_uint32("    ", DesiredAccess);           \
        log_uint32("    ", FileAttributes);          \
        log_uint32("    ", ShareAccess);             \
        log_uint32("    ", CreateDisposition);       \
        log_uint32("    ", CreateOptions);           \
        log_pdokan_file_info("    ", DokanFileInfo); \
    } while (0)

#define LOG_CleanUp()                                \
    do                                               \
    {                                                \
        log_msg("CleanUp:\n");                       \
        log_wstring("    ", FileName);               \
        log_pdokan_file_info("    ", DokanFileInfo); \
    } while (0)

#define LOG_ReadFile()                               \
    do                                               \
    {                                                \
        log_msg("ReadFile:\n");                      \
        log_wstring("    ", FileName);               \
        log_pointer("    ", Buffer);                 \
        log_uint32("    ", BufferLength);            \
        log_pointer("    ", ReadLength);             \
        log_int64("    ", Offset);                   \
        log_pdokan_file_info("    ", DokanFileInfo); \
    } while (0)

#define LOG_WriteFile()                              \
    do                                               \
    {                                                \
        log_msg("WriteFile:\n");                     \
        log_wstring("    ", FileName);               \
        log_pointer("    ", Buffer);                 \
        log_uint32("    ", NumberOfBytesToWrite);    \
        log_pointer("    ", NumberOfBytesWritten);   \
        log_int64("    ", Offset);                   \
        log_pdokan_file_info("    ", DokanFileInfo); \
    } while (0)

#define LOG_GetFileInformation()                     \
    do                                               \
    {                                                \
        log_msg("GetFileInformation:\n");            \
        log_wstring("    ", FileName);               \
        log_pdokan_file_info("    ", DokanFileInfo); \
    } while (0)

#define LOG_FindFiles()                              \
    do                                               \
    {                                                \
        log_msg("FindFiles:\n");                     \
        log_wstring("    ", FileName);               \
        log_pdokan_file_info("    ", DokanFileInfo); \
    } while (0)

#define LOG_SetFileAttributes()                      \
    do                                               \
    {                                                \
        log_msg("SetFileAttributes:\n");             \
        log_wstring("    ", FileName);               \
        log_uint32("    ", FileAttributes);          \
        log_pdokan_file_info("    ", DokanFileInfo); \
    } while (0)

#define LOG_SetFileTime()                            \
    do                                               \
    {                                                \
        log_msg("SetFileTime:\n");                   \
        log_wstring("    ", FileName);               \
        log_pdokan_file_info("    ", DokanFileInfo); \
    } while (0)

#define LOG_DeleteFile()                             \
    do                                               \
    {                                                \
        log_msg("DeleteFile:\n");                    \
        log_wstring("    ", FileName);               \
        log_pdokan_file_info("    ", DokanFileInfo); \
    } while (0)

#define LOG_DeleteDirectory()                        \
    do                                               \
    {                                                \
        log_msg("DeleteDirectory:\n");               \
        log_wstring("    ", FileName);               \
        log_pdokan_file_info("    ", DokanFileInfo); \
    } while (0)

#define LOG_MoveFile()                               \
    do                                               \
    {                                                \
        log_msg("MoveFile:\n");                      \
        log_wstring("    ", FileName);               \
        log_wstring("    ", NewFileName);            \
        log_bool("    ", ReplaceIfExisting);         \
        log_pdokan_file_info("    ", DokanFileInfo); \
    } while (0)

#define LOG_SetEndOfFile()                           \
    do                                               \
    {                                                \
        log_msg("SetEndOfFile:\n");                  \
        log_wstring("    ", FileName);               \
        log_int64("    ", ByteOffset);               \
        log_pdokan_file_info("    ", DokanFileInfo); \
    } while (0)

#define LOG_SetAllocationSize()                           \
    do                                               \
    {                                                \
        log_msg("SetAllocationSize:\n");                  \
        log_wstring("    ", FileName);               \
        log_int64("    ", AllocSize);               \
        log_pdokan_file_info("    ", DokanFileInfo); \
    } while (0)

std::string wide2local(const wchar_t *s);

const char *set_log_name(const char *name);

void log_msg(const char *format, ...);

#endif