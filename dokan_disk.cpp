#include <dokan/dokan.h>
#include <dokan/fileinfo.h>
#include <Winbase.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <map>
#include <queue>
#include <string>
#include <memory>
#include <mutex>
#include <utility>
#include "dokan_log.h"
#include "dev_io.h"

std::mutex global_mtx;

std::vector<std::wstring> parse_path(LPCWSTR FileName, bool *ads = nullptr)
{
    std::vector<std::wstring> res;
    wchar_t buf[256];
    int index = 0;
    while (*FileName != L'\0' && *FileName != L':')
    {
        if (*FileName == L'\\')
        {
            index = 0;
            memset(buf, 0, sizeof(buf));
        }
        else
        {
            buf[index++] = *FileName;
            if (FileName[1] == L'\\' || FileName[1] == L'\0')
            {
                res.emplace_back(buf);
            }
        }
        ++FileName;
    }
    if (*FileName == L':' && ads)
    {
        *ads = true;
    }
    return res;
}

dev_io::dev_t &get_dev()
{
    static dev_io::dev_t dev("test.img");
    if (!dev)
    {
        log_msg("open disk error");
        exit(EXIT_FAILURE);
    }
    return dev;
}

NTSTATUS DOKAN_CALLBACK VFATZwCreateFile(LPCWSTR FileName,
                                         PDOKAN_IO_SECURITY_CONTEXT SecurityContext,
                                         ACCESS_MASK DesiredAccess,
                                         ULONG FileAttributes,
                                         ULONG ShareAccess,
                                         ULONG CreateDisposition,
                                         ULONG CreateOptions,
                                         PDOKAN_FILE_INFO DokanFileInfo)
{
    std::lock_guard<std::mutex> g(global_mtx);
    LOG_ZwCreateFile();
    try
    {
        bool exist;
        bool isdir;
        bool ads = false;
        auto path = parse_path(FileName, &ads);
        if (ads)
        {
            log_pdokan_file_info("", DokanFileInfo);
            LOG_RETURN(ZwCreateFile, STATUS_OBJECT_NAME_INVALID);
        }
        ACCESS_MASK genericDesiredAccess;
        DWORD fileAttributesAndFlags;
        DWORD creationDisposition;
        DokanMapKernelToUserCreateFileFlags(
            DesiredAccess, FileAttributes, CreateOptions, CreateDisposition,
            &genericDesiredAccess, &fileAttributesAndFlags, &creationDisposition);
        log_uint32("    ", fileAttributesAndFlags);
        log_uint32("    ", creationDisposition);
        if (DokanFileInfo->IsDirectory)
        {
            fileAttributesAndFlags = 0x10;
        }
        else
        {
            fileAttributesAndFlags &= 0xffff;
        }
        auto file = get_dev().open(path, creationDisposition, fileAttributesAndFlags, exist, isdir);
        if (isdir && (CreateOptions & FILE_NON_DIRECTORY_FILE))
        {
            log_pdokan_file_info("", DokanFileInfo);
            LOG_RETURN(ZwCreateFile, STATUS_FILE_IS_A_DIRECTORY);
        }
        if (!isdir && (CreateOptions & FILE_DIRECTORY_FILE))
        {
            log_pdokan_file_info("", DokanFileInfo);
            LOG_RETURN(ZwCreateFile, STATUS_NOT_A_DIRECTORY);
        }
        DokanFileInfo->Context = file;
        DokanFileInfo->IsDirectory = isdir;
        if ((CreateDisposition == CREATE_ALWAYS || CreateDisposition == OPEN_ALWAYS) && exist)
        {
            log_pdokan_file_info("", DokanFileInfo);
            LOG_RETURN(ZwCreateFile, STATUS_OBJECT_NAME_COLLISION);
        }
    }
    catch (fat32::file_error &e)
    {
        switch (e.get_error_type())
        {
        case fat32::file_error::FILE_NOT_FOUND:
            log_pdokan_file_info("", DokanFileInfo);
            LOG_RETURN(ZwCreateFile, STATUS_OBJECT_NAME_NOT_FOUND);

        case fat32::file_error::FILE_NOT_DIR:
            log_pdokan_file_info("", DokanFileInfo);
            LOG_RETURN(ZwCreateFile, STATUS_NOT_A_DIRECTORY);

        case fat32::file_error::FILE_ALREADY_EXISTS:
            log_pdokan_file_info("", DokanFileInfo);
            LOG_RETURN(ZwCreateFile, STATUS_OBJECT_NAME_COLLISION);
        }
    }
    log_pdokan_file_info("", DokanFileInfo);
    LOG_RETURN(ZwCreateFile, STATUS_SUCCESS);
}

void DOKAN_CALLBACK VFATCleanup(LPCWSTR FileName,
                                PDOKAN_FILE_INFO DokanFileInfo)
{
    std::lock_guard<std::mutex> g(global_mtx);
    LOG_CleanUp();
    try
    {
        if (DokanFileInfo->DeleteOnClose)
        {
            get_dev().unlink(DokanFileInfo->Context);
        }
        get_dev().close(DokanFileInfo->Context);
    }
    catch (fat32::file_error &e)
    {
    }
}

void DOKAN_CALLBACK VFATCloseFile(LPCWSTR FileName,
                                  PDOKAN_FILE_INFO DokanFileInfo)
{
    return;
}

NTSTATUS DOKAN_CALLBACK VFATReadFile(LPCWSTR FileName,
                                     LPVOID Buffer,
                                     DWORD BufferLength,
                                     LPDWORD ReadLength,
                                     LONGLONG Offset,
                                     PDOKAN_FILE_INFO DokanFileInfo)
{
    std::lock_guard<std::mutex> g(global_mtx);
    LOG_ReadFile();
    try
    {
        *ReadLength = get_dev().read(DokanFileInfo->Context, Offset, BufferLength, Buffer);
        log_uint32("", *ReadLength);
        LOG_RETURN(ReadFile, STATUS_SUCCESS);
    }
    catch (fat32::file_error &e)
    {
        switch (e.get_error_type())
        {
        case fat32::file_error::FILE_NOT_FOUND:
            log_uint32("", *ReadLength);
            LOG_RETURN(ReadFile, STATUS_OBJECT_NAME_NOT_FOUND);

        case fat32::file_error::FILE_NOT_DIR:
            log_uint32("", *ReadLength);
            LOG_RETURN(ReadFile, STATUS_NOT_A_DIRECTORY);

        case fat32::file_error::FILE_ALREADY_EXISTS:
            log_uint32("", *ReadLength);
            LOG_RETURN(ReadFile, STATUS_OBJECT_NAME_COLLISION);

        default:
            try
            {
                bool exist;
                bool isdir;
                auto path = parse_path(FileName);
                auto file = get_dev().open(path, OPEN_EXISTING, 0, exist, isdir);
                DokanFileInfo->Context = file;
                DokanFileInfo->IsDirectory = isdir;
                *ReadLength = get_dev().read(DokanFileInfo->Context, Offset, BufferLength, Buffer);
                LOG_RETURN(ReadFile, STATUS_SUCCESS);
            }
            catch (fat32::file_error &e2)
            {
                switch (e2.get_error_type())
                {
                case fat32::file_error::FILE_NOT_FOUND:
                    log_uint32("", *ReadLength);
                    LOG_RETURN(ReadFile, STATUS_OBJECT_NAME_NOT_FOUND);

                case fat32::file_error::FILE_NOT_DIR:
                    log_uint32("", *ReadLength);
                    LOG_RETURN(ReadFile, STATUS_NOT_A_DIRECTORY);

                case fat32::file_error::FILE_ALREADY_EXISTS:
                    log_uint32("", *ReadLength);
                    LOG_RETURN(ReadFile, STATUS_OBJECT_NAME_COLLISION);

                default:
                    log_msg("ReadFile invalid file discriptor\n");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
}

NTSTATUS DOKAN_CALLBACK VFATWriteFile(LPCWSTR FileName,
                                      LPCVOID Buffer,
                                      DWORD NumberOfBytesToWrite,
                                      LPDWORD NumberOfBytesWritten,
                                      LONGLONG Offset,
                                      PDOKAN_FILE_INFO DokanFileInfo)
{
    std::lock_guard<std::mutex> g(global_mtx);
    LOG_WriteFile();
    try
    {
        BY_HANDLE_FILE_INFORMATION info;
        get_dev().fstat(DokanFileInfo->Context, &info);
        *NumberOfBytesWritten = get_dev().write(DokanFileInfo->Context, DokanFileInfo->WriteToEndOfFile ? info.nFileSizeLow : Offset, NumberOfBytesToWrite, Buffer);
        log_uint32("", *NumberOfBytesWritten);
        LOG_RETURN(WriteFile, STATUS_SUCCESS);
    }
    catch (fat32::file_error &e)
    {
        switch (e.get_error_type())
        {
        case fat32::file_error::FILE_NOT_FOUND:
            log_uint32("", *NumberOfBytesWritten);
            LOG_RETURN(WriteFile, STATUS_OBJECT_NAME_NOT_FOUND);

        case fat32::file_error::FILE_NOT_DIR:
            log_uint32("", *NumberOfBytesWritten);
            LOG_RETURN(WriteFile, STATUS_NOT_A_DIRECTORY);

        case fat32::file_error::FILE_ALREADY_EXISTS:
            log_uint32("", *NumberOfBytesWritten);
            LOG_RETURN(WriteFile, STATUS_OBJECT_NAME_COLLISION);

        default:
            try
            {
                bool exist;
                bool isdir;
                auto path = parse_path(FileName);
                auto file = get_dev().open(path, OPEN_EXISTING, 0, exist, isdir);
                DokanFileInfo->Context = file;
                DokanFileInfo->IsDirectory = isdir;
                BY_HANDLE_FILE_INFORMATION info;
                get_dev().fstat(DokanFileInfo->Context, &info);
                *NumberOfBytesWritten = get_dev().write(DokanFileInfo->Context, DokanFileInfo->WriteToEndOfFile ? info.nFileSizeLow : Offset, NumberOfBytesToWrite, Buffer);
                LOG_RETURN(WriteFile, STATUS_SUCCESS);
            }
            catch (fat32::file_error &e2)
            {
                switch (e2.get_error_type())
                {
                case fat32::file_error::FILE_NOT_FOUND:
                    log_uint32("", *NumberOfBytesWritten);
                    LOG_RETURN(WriteFile, STATUS_OBJECT_NAME_NOT_FOUND);

                case fat32::file_error::FILE_NOT_DIR:
                    log_uint32("", *NumberOfBytesWritten);
                    LOG_RETURN(WriteFile, STATUS_NOT_A_DIRECTORY);

                case fat32::file_error::FILE_ALREADY_EXISTS:
                    log_uint32("", *NumberOfBytesWritten);
                    LOG_RETURN(WriteFile, STATUS_OBJECT_NAME_COLLISION);

                default:
                    log_msg("WriteFile invalid file discriptor\n");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
}

NTSTATUS DOKAN_CALLBACK VFATGetFileInformation(LPCWSTR FileName,
                                               LPBY_HANDLE_FILE_INFORMATION Buffer,
                                               PDOKAN_FILE_INFO DokanFileInfo)
{
    std::lock_guard<std::mutex> g(global_mtx);
    LOG_GetFileInformation();
    try
    {
        get_dev().fstat(DokanFileInfo->Context, Buffer);
        log_lpby_handle_information("", Buffer);
        LOG_RETURN(GetFileInformation, STATUS_SUCCESS);
    }
    catch (fat32::file_error &e)
    {
        switch (e.get_error_type())
        {
        case fat32::file_error::FILE_NOT_FOUND:
            log_lpby_handle_information("", Buffer);
            LOG_RETURN(GetFileInformation, STATUS_OBJECT_NAME_NOT_FOUND);

        case fat32::file_error::FILE_NOT_DIR:
            log_lpby_handle_information("", Buffer);
            LOG_RETURN(GetFileInformation, STATUS_NOT_A_DIRECTORY);

        case fat32::file_error::FILE_ALREADY_EXISTS:
            log_lpby_handle_information("", Buffer);
            LOG_RETURN(GetFileInformation, STATUS_OBJECT_NAME_COLLISION);

        default:
            try
            {
                bool exist;
                bool isdir;
                auto path = parse_path(FileName);
                auto file = get_dev().open(path, OPEN_EXISTING, 0, exist, isdir);
                DokanFileInfo->Context = file;
                DokanFileInfo->IsDirectory = isdir;
                get_dev().fstat(DokanFileInfo->Context, Buffer);
                log_lpby_handle_information("", Buffer);
                LOG_RETURN(GetFileInformation, STATUS_SUCCESS);
            }
            catch (fat32::file_error &e2)
            {
                switch (e2.get_error_type())
                {
                case fat32::file_error::FILE_NOT_FOUND:
                    log_lpby_handle_information("", Buffer);
                    LOG_RETURN(GetFileInformation, STATUS_OBJECT_NAME_NOT_FOUND);

                case fat32::file_error::FILE_NOT_DIR:
                    log_lpby_handle_information("", Buffer);
                    LOG_RETURN(GetFileInformation, STATUS_NOT_A_DIRECTORY);

                case fat32::file_error::FILE_ALREADY_EXISTS:
                    log_lpby_handle_information("", Buffer);
                    LOG_RETURN(GetFileInformation, STATUS_OBJECT_NAME_COLLISION);

                default:
                    log_msg("GetFileInformation invalid file discriptor\n");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
}

NTSTATUS DOKAN_CALLBACK VFATFindFiles(LPCWSTR FileName,
                                      PFillFindData FillFindData,
                                      PDOKAN_FILE_INFO DokanFileInfo)
{
    std::lock_guard<std::mutex> g(global_mtx);
    LOG_FindFiles();
    WIN32_FIND_DATAW findData;
    try
    {
        auto ret = get_dev().opendir(DokanFileInfo->Context);
        for (const auto &entry : ret)
        {
            memset(&findData, 0, sizeof(WIN32_FIND_DATAW));
            wcsncpy(findData.cFileName, entry.name, MAX_PATH - 1);
            findData.dwFileAttributes = entry.info.dwFileAttributes;
            findData.ftCreationTime = entry.info.ftCreationTime;
            findData.ftLastAccessTime = entry.info.ftLastWriteTime;
            findData.ftLastWriteTime = entry.info.ftLastWriteTime;
            findData.nFileSizeHigh = entry.info.nFileSizeHigh;
            findData.nFileSizeLow = entry.info.nFileSizeLow;
            FillFindData(&findData, DokanFileInfo);
        }
        LOG_RETURN(FindFiles, STATUS_SUCCESS);
    }
    catch (fat32::file_error &e)
    {
        switch (e.get_error_type())
        {
        case fat32::file_error::FILE_NOT_FOUND:
            LOG_RETURN(FindFiles, STATUS_OBJECT_NAME_NOT_FOUND);

        case fat32::file_error::FILE_NOT_DIR:
            LOG_RETURN(FindFiles, STATUS_NOT_A_DIRECTORY);

        case fat32::file_error::FILE_ALREADY_EXISTS:
            LOG_RETURN(FindFiles, STATUS_OBJECT_NAME_COLLISION);

        default:
            try
            {
                bool exist;
                bool isdir;
                auto path = parse_path(FileName);
                auto file = get_dev().open(path, OPEN_EXISTING, 0, exist, isdir);
                DokanFileInfo->Context = file;
                DokanFileInfo->IsDirectory = isdir;
                auto ret = get_dev().opendir(DokanFileInfo->Context);
                for (const auto &entry : ret)
                {
                    memset(&findData, 0, sizeof(WIN32_FIND_DATAW));
                    wcsncpy(findData.cFileName, entry.name, MAX_PATH - 1);
                    findData.dwFileAttributes = entry.info.dwFileAttributes;
                    findData.ftCreationTime = entry.info.ftCreationTime;
                    findData.ftLastAccessTime = entry.info.ftLastWriteTime;
                    findData.ftLastWriteTime = entry.info.ftLastWriteTime;
                    findData.nFileSizeHigh = entry.info.nFileSizeHigh;
                    findData.nFileSizeLow = entry.info.nFileSizeLow;
                    FillFindData(&findData, DokanFileInfo);
                }
                LOG_RETURN(FindFiles, STATUS_SUCCESS);
            }
            catch (fat32::file_error &e2)
            {
                switch (e2.get_error_type())
                {
                case fat32::file_error::FILE_NOT_FOUND:
                    LOG_RETURN(FindFiles, STATUS_OBJECT_NAME_NOT_FOUND);

                case fat32::file_error::FILE_NOT_DIR:
                    LOG_RETURN(FindFiles, STATUS_NOT_A_DIRECTORY);

                case fat32::file_error::FILE_ALREADY_EXISTS:
                    LOG_RETURN(FindFiles, STATUS_OBJECT_NAME_COLLISION);

                default:
                    log_msg("FindFiles invalid file discriptor\n");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
}

NTSTATUS DOKAN_CALLBACK VFATFindFilesWithPattern(LPCWSTR PathName,
                                                 LPCWSTR SearchPattern,
                                                 PFillFindData FillFindData,
                                                 PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS DOKAN_CALLBACK VFATSetFileAttributes(LPCWSTR FileName,
                                              DWORD FileAttributes,
                                              PDOKAN_FILE_INFO DokanFileInfo)
{
    std::lock_guard<std::mutex> g(global_mtx);
    LOG_SetFileAttributes();
    try
    {
        get_dev().setattr(DokanFileInfo->Context, FileAttributes);
        LOG_RETURN(SetFileAttributes, STATUS_SUCCESS);
    }
    catch (fat32::file_error &e)
    {
        switch (e.get_error_type())
        {
        case fat32::file_error::FILE_NOT_FOUND:
            LOG_RETURN(SetFileAttributes, STATUS_OBJECT_NAME_NOT_FOUND);

        case fat32::file_error::FILE_NOT_DIR:
            LOG_RETURN(SetFileAttributes, STATUS_NOT_A_DIRECTORY);

        case fat32::file_error::FILE_ALREADY_EXISTS:
            LOG_RETURN(SetFileAttributes, STATUS_OBJECT_NAME_COLLISION);

        default:
            try
            {
                bool exist;
                bool isdir;
                auto path = parse_path(FileName);
                auto file = get_dev().open(path, OPEN_EXISTING, 0, exist, isdir);
                DokanFileInfo->Context = file;
                DokanFileInfo->IsDirectory = isdir;
                get_dev().setattr(DokanFileInfo->Context, FileAttributes);
                LOG_RETURN(SetFileAttributes, STATUS_SUCCESS);
            }
            catch (fat32::file_error &e2)
            {
                switch (e2.get_error_type())
                {
                case fat32::file_error::FILE_NOT_FOUND:
                    LOG_RETURN(SetFileAttributes, STATUS_OBJECT_NAME_NOT_FOUND);

                case fat32::file_error::FILE_NOT_DIR:
                    LOG_RETURN(SetFileAttributes, STATUS_NOT_A_DIRECTORY);

                case fat32::file_error::FILE_ALREADY_EXISTS:
                    LOG_RETURN(SetFileAttributes, STATUS_OBJECT_NAME_COLLISION);

                default:
                    log_msg("GetFileInformation invalid file discriptor\n");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
}

NTSTATUS DOKAN_CALLBACK VFATSetFileTime(LPCWSTR FileName,
                                        CONST FILETIME *CreationTime,
                                        CONST FILETIME *LastAccessTime,
                                        CONST FILETIME *LastWriteTime,
                                        PDOKAN_FILE_INFO DokanFileInfo)
{
    std::lock_guard<std::mutex> g(global_mtx);
    LOG_SetFileTime();
    try
    {
        get_dev().settime(DokanFileInfo->Context, CreationTime, LastAccessTime, LastWriteTime);
        LOG_RETURN(SetFileAttributes, STATUS_SUCCESS);
    }
    catch (fat32::file_error &e)
    {
        switch (e.get_error_type())
        {
        case fat32::file_error::FILE_NOT_FOUND:
            LOG_RETURN(SetFileTime, STATUS_OBJECT_NAME_NOT_FOUND);

        case fat32::file_error::FILE_NOT_DIR:
            LOG_RETURN(SetFileTime, STATUS_NOT_A_DIRECTORY);

        case fat32::file_error::FILE_ALREADY_EXISTS:
            LOG_RETURN(SetFileTime, STATUS_OBJECT_NAME_COLLISION);

        default:
            try
            {
                bool exist;
                bool isdir;
                auto path = parse_path(FileName);
                auto file = get_dev().open(path, OPEN_EXISTING, 0, exist, isdir);
                DokanFileInfo->Context = file;
                DokanFileInfo->IsDirectory = isdir;
                get_dev().settime(DokanFileInfo->Context, CreationTime, LastAccessTime, LastWriteTime);
                LOG_RETURN(SetFileTime, STATUS_SUCCESS);
            }
            catch (fat32::file_error &e2)
            {
                switch (e2.get_error_type())
                {
                case fat32::file_error::FILE_NOT_FOUND:
                    LOG_RETURN(SetFileTime, STATUS_OBJECT_NAME_NOT_FOUND);

                case fat32::file_error::FILE_NOT_DIR:
                    LOG_RETURN(SetFileTime, STATUS_NOT_A_DIRECTORY);

                case fat32::file_error::FILE_ALREADY_EXISTS:
                    LOG_RETURN(SetFileTime, STATUS_OBJECT_NAME_COLLISION);

                default:
                    log_msg("GetFileInformation invalid file discriptor\n");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
}

NTSTATUS DOKAN_CALLBACK VFATDeleteFile(LPCWSTR FileName,
                                       PDOKAN_FILE_INFO DokanFileInfo)
{
    std::lock_guard<std::mutex> g(global_mtx);
    LOG_DeleteFile();
    try
    {
        BY_HANDLE_FILE_INFORMATION info;
        get_dev().fstat(DokanFileInfo->Context, &info);
        if (info.dwFileAttributes & 0x10)
        {
            LOG_RETURN(DeleteFile, STATUS_ACCESS_DENIED);
        }
        DokanFileInfo->DeleteOnClose = TRUE;
        LOG_RETURN(DeleteFile, STATUS_SUCCESS);
    }
    catch (fat32::file_error &e)
    {
        switch (e.get_error_type())
        {
        case fat32::file_error::FILE_NOT_FOUND:
            LOG_RETURN(DeleteFile, STATUS_OBJECT_NAME_NOT_FOUND);

        case fat32::file_error::FILE_NOT_DIR:
            LOG_RETURN(DeleteFile, STATUS_NOT_A_DIRECTORY);

        case fat32::file_error::FILE_ALREADY_EXISTS:
            LOG_RETURN(DeleteFile, STATUS_OBJECT_NAME_COLLISION);

        default:
            try
            {
                bool exist;
                bool isdir;
                auto path = parse_path(FileName);
                auto file = get_dev().open(path, OPEN_EXISTING, 0, exist, isdir);
                DokanFileInfo->Context = file;
                DokanFileInfo->IsDirectory = isdir;
                BY_HANDLE_FILE_INFORMATION info;
                get_dev().fstat(DokanFileInfo->Context, &info);
                if (info.dwFileAttributes & 0x10)
                {
                    LOG_RETURN(DeleteFile, STATUS_ACCESS_DENIED);
                }
                DokanFileInfo->DeleteOnClose = TRUE;
                LOG_RETURN(DeleteFile, STATUS_SUCCESS);
            }
            catch (fat32::file_error &e2)
            {
                switch (e2.get_error_type())
                {
                case fat32::file_error::FILE_NOT_FOUND:
                    LOG_RETURN(DeleteFile, STATUS_OBJECT_NAME_NOT_FOUND);

                case fat32::file_error::FILE_NOT_DIR:
                    LOG_RETURN(DeleteFile, STATUS_NOT_A_DIRECTORY);

                case fat32::file_error::FILE_ALREADY_EXISTS:
                    LOG_RETURN(DeleteFile, STATUS_OBJECT_NAME_COLLISION);

                default:
                    log_msg("DeleteFile invalid file discriptor\n");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
}

NTSTATUS DOKAN_CALLBACK VFATDeleteDirectory(LPCWSTR FileName,
                                            PDOKAN_FILE_INFO DokanFileInfo)
{
    std::lock_guard<std::mutex> g(global_mtx);
    LOG_DeleteDirectory();
    try
    {
        BY_HANDLE_FILE_INFORMATION info;
        get_dev().fstat(DokanFileInfo->Context, &info);
        if (!(info.dwFileAttributes & 0x10))
        {
            LOG_RETURN(DeleteFile, STATUS_ACCESS_DENIED);
        }
        auto ret = get_dev().opendir(DokanFileInfo->Context);
        for (const auto &entry : ret)
        {
            if (wcscmp(entry.name, L".") != 0 && wcscmp(entry.name, L"..") != 0)
            {
                LOG_RETURN(DeleteDirectory, STATUS_DIRECTORY_NOT_EMPTY);
            }
        }
        DokanFileInfo->DeleteOnClose = TRUE;
        LOG_RETURN(DeleteDirectory, STATUS_SUCCESS);
    }
    catch (fat32::file_error &e)
    {
        switch (e.get_error_type())
        {
        case fat32::file_error::FILE_NOT_FOUND:
            LOG_RETURN(DeleteDirectory, STATUS_OBJECT_NAME_NOT_FOUND);

        case fat32::file_error::FILE_NOT_DIR:
            LOG_RETURN(DeleteDirectory, STATUS_NOT_A_DIRECTORY);

        case fat32::file_error::FILE_ALREADY_EXISTS:
            LOG_RETURN(DeleteDirectory, STATUS_OBJECT_NAME_COLLISION);

        default:
            try
            {
                bool exist;
                bool isdir;
                auto path = parse_path(FileName);
                auto file = get_dev().open(path, OPEN_EXISTING, 0, exist, isdir);
                DokanFileInfo->Context = file;
                DokanFileInfo->IsDirectory = isdir;
                BY_HANDLE_FILE_INFORMATION info;
                get_dev().fstat(DokanFileInfo->Context, &info);
                if (!(info.dwFileAttributes & 0x10))
                {
                    LOG_RETURN(DeleteFile, STATUS_ACCESS_DENIED);
                }
                auto ret = get_dev().opendir(DokanFileInfo->Context);
                for (const auto &entry : ret)
                {
                    if (wcscmp(entry.name, L".") != 0 && wcscmp(entry.name, L"..") != 0)
                    {
                        LOG_RETURN(DeleteDirectory, STATUS_DIRECTORY_NOT_EMPTY);
                    }
                }
                DokanFileInfo->DeleteOnClose = TRUE;
                LOG_RETURN(DeleteDirectory, STATUS_SUCCESS);
            }
            catch (fat32::file_error &e2)
            {
                switch (e2.get_error_type())
                {
                case fat32::file_error::FILE_NOT_FOUND:
                    LOG_RETURN(DeleteDirectory, STATUS_OBJECT_NAME_NOT_FOUND);

                case fat32::file_error::FILE_NOT_DIR:
                    LOG_RETURN(DeleteDirectory, STATUS_NOT_A_DIRECTORY);

                case fat32::file_error::FILE_ALREADY_EXISTS:
                    LOG_RETURN(DeleteDirectory, STATUS_OBJECT_NAME_COLLISION);

                default:
                    log_msg("DeleteDirectory invalid file discriptor\n");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
}

NTSTATUS DOKAN_CALLBACK VFATMoveFile(LPCWSTR FileName,
                                     LPCWSTR NewFileName,
                                     BOOL ReplaceIfExisting,
                                     PDOKAN_FILE_INFO DokanFileInfo)
{
    std::lock_guard<std::mutex> g(global_mtx);
    LOG_MoveFile();
    auto newpath = parse_path(NewFileName);
    try
    {
        bool ok = get_dev().rename(DokanFileInfo->Context, newpath, ReplaceIfExisting);
        if (!ok)
        {
            LOG_RETURN(MoveFile, STATUS_ACCESS_DENIED);
        }
        LOG_RETURN(MoveFile, STATUS_SUCCESS);
    }
    catch (fat32::file_error &e)
    {
        switch (e.get_error_type())
        {
        case fat32::file_error::FILE_NOT_FOUND:
            LOG_RETURN(MoveFile, STATUS_OBJECT_NAME_NOT_FOUND);

        case fat32::file_error::FILE_NOT_DIR:
            LOG_RETURN(MoveFile, STATUS_NOT_A_DIRECTORY);

        case fat32::file_error::FILE_ALREADY_EXISTS:
            LOG_RETURN(MoveFile, STATUS_OBJECT_NAME_COLLISION);

        default:
            try
            {
                bool exist;
                bool isdir;
                auto path = parse_path(FileName);
                auto file = get_dev().open(path, OPEN_EXISTING, 0, exist, isdir);
                DokanFileInfo->Context = file;
                DokanFileInfo->IsDirectory = isdir;
                get_dev().rename(DokanFileInfo->Context, newpath, ReplaceIfExisting);
                LOG_RETURN(MoveFile, STATUS_SUCCESS);
            }
            catch (fat32::file_error &e2)
            {
                switch (e2.get_error_type())
                {
                case fat32::file_error::FILE_NOT_FOUND:
                    LOG_RETURN(MoveFile, STATUS_OBJECT_NAME_NOT_FOUND);

                case fat32::file_error::FILE_NOT_DIR:
                    LOG_RETURN(MoveFile, STATUS_NOT_A_DIRECTORY);

                case fat32::file_error::FILE_ALREADY_EXISTS:
                    LOG_RETURN(MoveFile, STATUS_OBJECT_NAME_COLLISION);

                default:
                    log_msg("MoveFile invalid file discriptor\n");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
}

NTSTATUS DOKAN_CALLBACK VFATSetEndOfFile(LPCWSTR FileName,
                                         LONGLONG ByteOffset,
                                         PDOKAN_FILE_INFO DokanFileInfo)
{
    std::lock_guard<std::mutex> g(global_mtx);
    LOG_SetEndOfFile();
    try
    {
        get_dev().setend(DokanFileInfo->Context, ByteOffset);
        LOG_RETURN(SetEndOfFile, STATUS_SUCCESS);
    }
    catch (fat32::file_error &e)
    {
        switch (e.get_error_type())
        {
        case fat32::file_error::FILE_NOT_FOUND:
            LOG_RETURN(SetEndOfFile, STATUS_OBJECT_NAME_NOT_FOUND);

        case fat32::file_error::FILE_NOT_DIR:
            LOG_RETURN(SetEndOfFile, STATUS_NOT_A_DIRECTORY);

        case fat32::file_error::FILE_ALREADY_EXISTS:
            LOG_RETURN(SetEndOfFile, STATUS_OBJECT_NAME_COLLISION);

        default:
            try
            {
                bool exist;
                bool isdir;
                auto path = parse_path(FileName);
                auto file = get_dev().open(path, OPEN_EXISTING, 0, exist, isdir);
                DokanFileInfo->Context = file;
                DokanFileInfo->IsDirectory = isdir;
                get_dev().setend(DokanFileInfo->Context, ByteOffset);
                LOG_RETURN(SetEndOfFile, STATUS_SUCCESS);
            }
            catch (fat32::file_error &e2)
            {
                switch (e2.get_error_type())
                {
                case fat32::file_error::FILE_NOT_FOUND:
                    LOG_RETURN(SetEndOfFile, STATUS_OBJECT_NAME_NOT_FOUND);

                case fat32::file_error::FILE_NOT_DIR:
                    LOG_RETURN(SetEndOfFile, STATUS_NOT_A_DIRECTORY);

                case fat32::file_error::FILE_ALREADY_EXISTS:
                    LOG_RETURN(SetEndOfFile, STATUS_OBJECT_NAME_COLLISION);

                default:
                    log_msg("GetFileInformation invalid file discriptor\n");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
}

NTSTATUS DOKAN_CALLBACK VFATSetAllocationSize(LPCWSTR FileName,
                                              LONGLONG AllocSize,
                                              PDOKAN_FILE_INFO DokanFileInfo)
{
    std::lock_guard<std::mutex> g(global_mtx);
    LOG_SetAllocationSize();
    try
    {
        get_dev().setalloc(DokanFileInfo->Context, AllocSize);
        LOG_RETURN(SetAllocationSize, STATUS_SUCCESS);
    }
    catch (fat32::file_error &e)
    {
        switch (e.get_error_type())
        {
        case fat32::file_error::FILE_NOT_FOUND:
            LOG_RETURN(SetAllocationSize, STATUS_OBJECT_NAME_NOT_FOUND);

        case fat32::file_error::FILE_NOT_DIR:
            LOG_RETURN(SetAllocationSize, STATUS_NOT_A_DIRECTORY);

        case fat32::file_error::FILE_ALREADY_EXISTS:
            LOG_RETURN(SetAllocationSize, STATUS_OBJECT_NAME_COLLISION);

        default:
            try
            {
                bool exist;
                bool isdir;
                auto path = parse_path(FileName);
                auto file = get_dev().open(path, OPEN_EXISTING, 0, exist, isdir);
                DokanFileInfo->Context = file;
                DokanFileInfo->IsDirectory = isdir;
                get_dev().setalloc(DokanFileInfo->Context, AllocSize);
                LOG_RETURN(SetAllocationSize, STATUS_SUCCESS);
            }
            catch (fat32::file_error &e2)
            {
                switch (e2.get_error_type())
                {
                case fat32::file_error::FILE_NOT_FOUND:
                    LOG_RETURN(SetAllocationSize, STATUS_OBJECT_NAME_NOT_FOUND);

                case fat32::file_error::FILE_NOT_DIR:
                    LOG_RETURN(SetAllocationSize, STATUS_NOT_A_DIRECTORY);

                case fat32::file_error::FILE_ALREADY_EXISTS:
                    LOG_RETURN(SetAllocationSize, STATUS_OBJECT_NAME_COLLISION);

                default:
                    log_msg("GetFileInformation invalid file discriptor\n");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
}

NTSTATUS DOKAN_CALLBACK VFATGetDiskFreeSpace(PULONGLONG FreeBytesAvailable,
                                             PULONGLONG TotalNumberOfBytes,
                                             PULONGLONG TotalNumberOfFreeBytes,
                                             PDOKAN_FILE_INFO DokanFileInfo)
{
    get_dev().get_disk_info(FreeBytesAvailable, TotalNumberOfBytes, TotalNumberOfFreeBytes);
    return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK VFATUnmounted(PDOKAN_FILE_INFO DokanFileInfo)
{
    std::lock_guard<std::mutex> g(global_mtx);
    log_msg("Unmounted\nclearing...\n");
    get_dev().clear();
    return STATUS_SUCCESS;
}

DOKAN_OPERATIONS operations = {
    .ZwCreateFile = VFATZwCreateFile,
    .Cleanup = VFATCleanup,
    .CloseFile = VFATCloseFile,
    .ReadFile = VFATReadFile,
    .WriteFile = VFATWriteFile,
    .GetFileInformation = VFATGetFileInformation,
    .FindFiles = VFATFindFiles,
    .FindFilesWithPattern = VFATFindFilesWithPattern,
    .SetFileAttributes = VFATSetFileAttributes,
    .SetFileTime = VFATSetFileTime,
    .DeleteFile = VFATDeleteFile,
    .DeleteDirectory = VFATDeleteDirectory,
    .MoveFile = VFATMoveFile,
    .SetEndOfFile = VFATSetEndOfFile,
    .SetAllocationSize = VFATSetAllocationSize,
    .GetDiskFreeSpace = VFATGetDiskFreeSpace,
    .Unmounted = VFATUnmounted,
};

DOKAN_OPTIONS dokanOptions = {
    .Version = DOKAN_VERSION,
    .Options = DOKAN_OPTION_REMOVABLE | DOKAN_OPTION_ALT_STREAM,
    .MountPoint = L"E:\\",
};

int main(int argc, char *argv[])
{
    DokanMain(&dokanOptions, &operations);
}