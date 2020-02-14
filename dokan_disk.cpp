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
#include "file.h"
#include "dokan_log.h"
#include "dev_io.h"

std::map<uint64_t, fat32::file_info> open_file_table;
std::mutex mtx;

std::vector<std::wstring> parse_path(LPCWSTR FileName)
{
    std::vector<std::wstring> res;
    wchar_t buf[256];
    int index = 0;
    while (*FileName != L'\0')
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
    return res;
}

std::string wide2local(const wchar_t *s)
{
    int size = WideCharToMultiByte(CP_ACP, 0, s, -1, NULL, 0, NULL, FALSE);
    std::string res;
    res.resize(size);
    size = WideCharToMultiByte(CP_ACP, 0, s, -1, res.data(), size, NULL, FALSE);
    return std::move(res);
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
    log_msg("ZwCreateFile:\nFileName: %s\n", wide2local(FileName).c_str());
    return STATUS_SUCCESS;
}

void DOKAN_CALLBACK VFATCleanup(LPCWSTR FileName,
                                PDOKAN_FILE_INFO DokanFileInfo)
{
}

void DOKAN_CALLBACK VFATCloseFile(LPCWSTR FileName,
                                  PDOKAN_FILE_INFO DokanFileInfo)
{
}

NTSTATUS DOKAN_CALLBACK VFATReadFile(LPCWSTR FileName,
                                     LPVOID Buffer,
                                     DWORD BufferLength,
                                     LPDWORD ReadLength,
                                     LONGLONG Offset,
                                     PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK VFATWriteFile(LPCWSTR FileName,
                                      LPCVOID Buffer,
                                      DWORD NumberOfBytesToWrite,
                                      LPDWORD NumberOfBytesWritten,
                                      LONGLONG Offset,
                                      PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK VFATGetFileInformation(LPCWSTR FileName,
                                               LPBY_HANDLE_FILE_INFORMATION Buffer,
                                               PDOKAN_FILE_INFO DokanFileInfo)
{
    log_msg("GetFileInformation:\nFileName: %s\n", wide2local(FileName).c_str());
    auto path = parse_path(FileName);
    auto ret = fat32::get_info_by_path(get_dev(), path, Buffer);
    if (ret == -1)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    else
    {
        return STATUS_SUCCESS;
    }
}

NTSTATUS DOKAN_CALLBACK VFATFindFiles(LPCWSTR FileName,
                                      PFillFindData FillFindData,
                                      PDOKAN_FILE_INFO DokanFileInfo)
{
    log_msg("FindFiles:\nFileName: %s\n", wide2local(FileName).c_str());
    WIN32_FIND_DATAW findData;
    auto path = parse_path(FileName);
    int isdir;
    auto first_clus = fat32::get_first_clus_by_path(get_dev(), path, &isdir);
    if (first_clus == 0xffffffff)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (!isdir)
    {
        return STATUS_NOT_A_DIRECTORY;
    }
    auto dir = fat32::open_dir(get_dev(), first_clus);
    for (const auto &entry : dir)
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
    return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK VFATFindFilesWithPattern(LPCWSTR PathName,
                                                 LPCWSTR SearchPattern,
                                                 PFillFindData FillFindData,
                                                 PDOKAN_FILE_INFO DokanFileInfo)
{
    return STATUS_NOT_IMPLEMENTED;
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