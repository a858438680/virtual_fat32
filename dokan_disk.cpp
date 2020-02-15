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

std::map<uint64_t, fat32::file> open_file_table;
uint64_t last_index;
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
    log_msg("ZwCreateFile:\n"
            "    FileName: %s\n",
            wide2local(FileName).c_str());
    try
    {
        auto file = get_dev().open_file(parse_path(FileName));
        bool isdir;
        uint64_t index = last_index;
        {
            std::lock_guard<std::mutex> g(mtx);
            while (open_file_table.find(index) != open_file_table.end())
                ++index;
            open_file_table.insert(std::make_pair(index, std::move(file)));
            last_index = index + 1;
            isdir = file.isdir();
        }
        DokanFileInfo->Context = index;
        DokanFileInfo->IsDirectory = isdir;
    }
    catch (fat32::file_error &e)
    {
        switch (e.get_error_type())
        {
        case fat32::file_error::FILE_NOT_FOUND:
            return STATUS_OBJECT_NAME_NOT_FOUND;

        default:
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }
    }
    return STATUS_SUCCESS;
}

void DOKAN_CALLBACK VFATCleanup(LPCWSTR FileName,
                                PDOKAN_FILE_INFO DokanFileInfo)
{
    open_file_table.erase(DokanFileInfo->Context);
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
    auto &dev = get_dev();
    decltype(open_file_table.begin()) itr;
    if ((itr = open_file_table.find(DokanFileInfo->Context)) != open_file_table.end())
    {
        auto &file = itr->second;
        ULARGE_INTEGER tmp;
        tmp.HighPart = file.info.nFileSizeHigh;
        tmp.LowPart = file.info.nFileSizeLow;
        uint64_t file_size = tmp.QuadPart;
        int64_t left_border = Offset > 0 ? Offset : 0;
        int64_t right_border = (Offset + BufferLength) < file_size ? (Offset + BufferLength) : file_size;
        if (left_border >= right_border)
        {
            *ReadLength = 0;
            return STATUS_SUCCESS;
        }
        auto clus_size = dev.get_clus_size();
        std::vector<char> buf(clus_size);
        uint32_t begin_clus = left_border / clus_size;
        uint32_t end_clus = (right_border + clus_size - 1) / clus_size;
        uint32_t index = 0;
        for (auto i = begin_clus; i < end_clus; ++i)
        {
            dev.read_clus(file.alloc[i], buf.data());
            uint32_t begin = 0, end = clus_size;
            if (i == begin_clus)
            {
                begin = left_border % clus_size;
            }
            if (i == end_clus)
            {
                end = (right_border - 1) % clus_size + 1;
            }
            auto size = end - begin;
            memcpy((char*)Buffer + index, buf.data() + begin, size);
            index += size;
        }
        *ReadLength = index;
        return STATUS_SUCCESS;
    }
    *ReadLength = 0;
    return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK VFATWriteFile(LPCWSTR FileName,
                                      LPCVOID Buffer,
                                      DWORD NumberOfBytesToWrite,
                                      LPDWORD NumberOfBytesWritten,
                                      LONGLONG Offset,
                                      PDOKAN_FILE_INFO DokanFileInfo)
{
    auto &dev = get_dev();
    decltype(open_file_table.begin()) itr;
    if ((itr = open_file_table.find(DokanFileInfo->Context)) != open_file_table.end())
    {
        if (!NumberOfBytesToWrite)
        {
            *NumberOfBytesWritten = 0;
            return STATUS_SUCCESS;
        }
        auto &file = itr->second;
        ULARGE_INTEGER tmp;
        tmp.HighPart = file.info.nFileSizeHigh;
        tmp.LowPart = file.info.nFileSizeLow;
        uint64_t file_size = tmp.QuadPart;
        uint64_t left_border = DokanFileInfo->WriteToEndOfFile ? file_size : (Offset > 0 ? Offset : 0);
        uint64_t right_border = left_border + NumberOfBytesToWrite;
        auto clus_size = dev.get_clus_size();
        std::vector<char> buf(clus_size);
        uint32_t begin_clus = left_border / clus_size;
        uint32_t end_clus = (right_border + clus_size - 1) / clus_size;
        for (auto i = begin_clus; i < end_clus; ++i)
        {
            
        }
        return STATUS_SUCCESS;
    }
    return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK VFATGetFileInformation(LPCWSTR FileName,
                                               LPBY_HANDLE_FILE_INFORMATION Buffer,
                                               PDOKAN_FILE_INFO DokanFileInfo)
{
    log_msg("GetFileInformation:\nFileName: %s\n", wide2local(FileName).c_str());
    decltype(open_file_table.begin()) itr;
    if ((itr = open_file_table.find(DokanFileInfo->Context)) != open_file_table.end())
    {
        memcpy(Buffer, &itr->second.info, sizeof(BY_HANDLE_FILE_INFORMATION));
        return STATUS_SUCCESS;
    }
    else
    {
        auto path = parse_path(FileName);
        try
        {
            get_dev().get_info(path, Buffer);
        }
        catch (fat32::file_error &e)
        {
            switch (e.get_error_type())
            {
            case fat32::file_error::FILE_NOT_FOUND:
                return STATUS_OBJECT_NAME_NOT_FOUND;

            default:
                return STATUS_OBJECT_NAME_NOT_FOUND;
            }
        }
        return STATUS_SUCCESS;
    }
}

NTSTATUS DOKAN_CALLBACK VFATFindFiles(LPCWSTR FileName,
                                      PFillFindData FillFindData,
                                      PDOKAN_FILE_INFO DokanFileInfo)
{
    log_msg("FindFiles:\nFileName: %s\n", wide2local(FileName).c_str());
    WIN32_FIND_DATAW findData;

    decltype(open_file_table.begin()) itr;
    if ((itr = open_file_table.find(DokanFileInfo->Context)) != open_file_table.end())
    {
        for (const auto &entry : itr->second.entries)
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
    else
    {
        auto path = parse_path(FileName);
        try
        {
            auto dir = get_dev().open_dir(path);
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
        }
        catch (const fat32::file_error &e)
        {
            switch (e.get_error_type())
            {
            case fat32::file_error::FILE_NOT_FOUND:
                return STATUS_OBJECT_NAME_NOT_FOUND;

            case fat32::file_error::FILE_NOT_DIR:
                return STATUS_NOT_A_DIRECTORY;
            }
        }

        return STATUS_SUCCESS;
    }
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