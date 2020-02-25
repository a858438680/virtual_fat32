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
        log_uint32("    ", creationDisposition);
        auto file = get_dev().open(parse_path(FileName), creationDisposition, FileAttributes, exist, isdir);
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
                auto file = get_dev().open(parse_path(FileName), OPEN_EXISTING, 0, exist, isdir);
                DokanFileInfo->Context = file;
                DokanFileInfo->IsDirectory = isdir;
                *ReadLength = get_dev().read(DokanFileInfo->Context, Offset, BufferLength, Buffer);
                return STATUS_SUCCESS;
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
                auto file = get_dev().open(parse_path(FileName), OPEN_EXISTING, 0, exist, isdir);
                DokanFileInfo->Context = file;
                DokanFileInfo->IsDirectory = isdir;
                BY_HANDLE_FILE_INFORMATION info;
                get_dev().fstat(DokanFileInfo->Context, &info);
                *NumberOfBytesWritten = get_dev().write(DokanFileInfo->Context, DokanFileInfo->WriteToEndOfFile ? info.nFileSizeLow : Offset, NumberOfBytesToWrite, Buffer);
                return STATUS_SUCCESS;
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
                auto file = get_dev().open(parse_path(FileName), OPEN_EXISTING, 0, exist, isdir);
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
                auto file = get_dev().open(parse_path(FileName), OPEN_EXISTING, 0, exist, isdir);
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
                    return STATUS_OBJECT_NAME_NOT_FOUND;

                case fat32::file_error::FILE_NOT_DIR:
                    return STATUS_NOT_A_DIRECTORY;

                case fat32::file_error::FILE_ALREADY_EXISTS:
                    return STATUS_OBJECT_NAME_COLLISION;

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

NTSTATUS DOKAN_CALLBACK VFATUnmounted(PDOKAN_FILE_INFO DokanFileInfo)
{
    std::lock_guard<std::mutex> g(global_mtx);
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