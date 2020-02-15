#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <timezoneapi.h>
#include "dev_io.h"

namespace dev_io
{

DWORD FatAttr2FileAttr(uint8_t attr)
{
    DWORD res = 0;
    res |= (attr & 0x37);
    return res;
}

void dev_t::get_info(const fat32::path &path, void *buffer)
{
    LPBY_HANDLE_FILE_INFORMATION fbuffer = (LPBY_HANDLE_FILE_INFORMATION)buffer;
    if (path.empty())
    {
        memset(fbuffer, 0, sizeof(BY_HANDLE_FILE_INFORMATION));
        fbuffer->dwFileAttributes = 0x10;
        fbuffer->dwVolumeSerialNumber = get_vol_id();
        ULARGE_INTEGER file_index;
        file_index.QuadPart = get_root_clus();
        fbuffer->nFileIndexHigh = file_index.HighPart;
        fbuffer->nFileIndexHigh = file_index.LowPart;
        fbuffer->nNumberOfLinks = 2;
        return;
    }
    auto copy = path;
    copy.pop_back();
    int isdir;
    auto first_clus = get_first_clus(copy, &isdir);
    if (!isdir)
    {
        throw fat32::file_error(fat32::file_error::FILE_NOT_FOUND);
    }
    auto dir = open_dir(first_clus);
    for (const auto &entry : dir)
    {
        if (path.back() == entry.name)
        {
            memcpy(fbuffer, &entry.info, sizeof(BY_HANDLE_FILE_INFORMATION));
            return;
        }
    }
    throw fat32::file_error(fat32::file_error::FILE_NOT_FOUND);
}

fat32::file dev_t::open_file(const fat32::path &path)
{
    fat32::file res;
    get_info(path, &res.info);
    auto clus = get_first_clus(path);
    res.alloc = open_file(clus);
    if (res.isdir())
    {
        res.entries = open_dir(clus);
    }
    return res;
}

fat32::dir_info dev_t::open_dir(const fat32::path &path)
{
    fat32::dir_info res;
    int isdir;
    auto clus = get_first_clus(path, &isdir);
    if (!isdir)
    {
        throw fat32::file_error(fat32::file_error::FILE_NOT_DIR);
    }
    return res;
}

fat32::file_alloc dev_t::open_file(uint32_t first_clus)
{
    fat32::file_alloc res;
    res.push_back(first_clus);
    uint32_t next_clus;
    while ((next_clus = (get_fat(next_clus) & 0x0fffffff)) >= 2 && next_clus < get_count_of_clus() + 2)
    {
        res.push_back(next_clus);
    }

    return std::move(res);
}

fat32::dir_info dev_t::open_dir(uint32_t first_clus)
{
    fat32::dir_info res;
    fat32::file_alloc file = open_file(first_clus);
    std::vector<fat32::DIR_Entry> dir(get_sec_per_clus() * get_block_size() / sizeof(fat32::DIR_Entry));
    wchar_t name[256];
    int name_pos = 255;
    int have_long_name = 0;
    int end = 0;
    for (auto clus : file)
    {
        if (end == 1)
            break;
        if (clus >= 2 && clus < get_count_of_clus() + 2)
        {
            read_clus(clus, dir.data());
            for (size_t j = 0; j < dir.size(); ++j)
            {
                if (dir[j].DIR_Name[0] == 0)
                {
                    end = 1;
                    break;
                }
                else if ((unsigned char)dir[j].DIR_Name[0] == 0xe5)
                {
                    continue;
                }
                else
                {
                    if ((dir[j].DIR_Attr & 0x3f) == 0x0f)
                    {
                        fat32::LDIR_Entry *ldir = (fat32::LDIR_Entry *)dir.data();
                        if (ldir[j].LDIR_Ord & 0x40)
                        {
                            wchar_t tmp[13];
                            memcpy(tmp + 11, ldir[j].LDIR_Name3, sizeof(ldir[j].LDIR_Name3));
                            memcpy(tmp + 5, ldir[j].LDIR_Name2, sizeof(ldir[j].LDIR_Name2));
                            memcpy(tmp, ldir[j].LDIR_Name1, sizeof(ldir[j].LDIR_Name1));
                            //problem possible
                            size_t len = wcslen(tmp);
                            name_pos = 255 - len;
                            wcscpy(name + name_pos, tmp);
                            have_long_name = 1;
                        }
                        else
                        {
                            memcpy(name + name_pos - 2, ldir[j].LDIR_Name3, sizeof(ldir[j].LDIR_Name3));
                            memcpy(name + name_pos - 8, ldir[j].LDIR_Name2, sizeof(ldir[j].LDIR_Name2));
                            memcpy(name + name_pos - 13, ldir[j].LDIR_Name1, sizeof(ldir[j].LDIR_Name1));
                            name_pos -= 13;
                        }
                    }
                    else
                    {
                        fat32::Entry_Node entry;
                        memset(&entry, 0, sizeof(fat32::Entry_Node));
                        if (have_long_name)
                        {
                            wcscpy(entry.name, name + name_pos);
                            have_long_name = 0;
                        }
                        else
                        {
                            wchar_t c;
                            int index = 0;
                            for (int k = 0; k < 8 && (c = dir[j].DIR_Name[k]) != 0x20; ++k)
                            {
                                entry.name[index++] = c;
                            }
                            if (dir[j].DIR_Name[8] != 0x20)
                            {
                                entry.name[index++] = L'.';
                                for (int k = 8; k < 11 && (c = dir[j].DIR_Name[k]) != 0x20; ++k)
                                {
                                    entry.name[index++] = c;
                                }
                            }
                        }
                        entry.first_clus = ((uint32_t)(dir[j].DIR_FstClusHI) << 16) + dir[j].DIR_FstClusLO;
                        entry.info.dwFileAttributes = FatAttr2FileAttr(dir[j].DIR_Attr);
                        entry.info.dwVolumeSerialNumber = get_vol_id();
                        SYSTEMTIME systime;
                        memset(&systime, 0, sizeof(SYSTEMTIME));
                        systime.wYear = (dir[j].DIR_CrtDate >> 9) + 1980;
                        systime.wMonth = ((dir[j].DIR_CrtDate & 0x1e0) >> 5);
                        systime.wDay = (dir[j].DIR_CrtDate & 0x1f);
                        systime.wHour = (dir[j].DIR_CrtTime >> 11);
                        systime.wMinute = ((dir[j].DIR_CrtTime & 0x7e0) >> 5);
                        systime.wSecond = (dir[j].DIR_CrtTime & 0x1f) * 2 + dir[j].DIR_CrtTimeTenth / 100;
                        systime.wMilliseconds = (dir[j].DIR_CrtTimeTenth % 100) * 10;
                        SystemTimeToFileTime(&systime, &entry.info.ftCreationTime);
                        memset(&systime, 0, sizeof(SYSTEMTIME));
                        systime.wYear = (dir[j].DIR_WrtDate >> 9) + 1980;
                        systime.wMonth = ((dir[j].DIR_WrtDate & 0x1e0) >> 5);
                        systime.wDay = (dir[j].DIR_WrtDate & 0x1f);
                        systime.wHour = (dir[j].DIR_WrtTime >> 11);
                        systime.wMinute = ((dir[j].DIR_WrtTime & 0x7e0) >> 5);
                        systime.wSecond = (dir[j].DIR_WrtTime & 0x1f) * 2;
                        SystemTimeToFileTime(&systime, &entry.info.ftLastWriteTime);
                        memset(&systime, 0, sizeof(SYSTEMTIME));
                        systime.wYear = (dir[j].DIR_LstAccDate >> 9) + 1980;
                        systime.wMonth = ((dir[j].DIR_LstAccDate & 0x1e0) >> 5);
                        systime.wDay = (dir[j].DIR_LstAccDate & 0x1f);
                        SystemTimeToFileTime(&systime, &entry.info.ftLastAccessTime);
                        ULARGE_INTEGER file_index;
                        file_index.QuadPart = entry.first_clus;
                        entry.info.nFileIndexHigh = file_index.HighPart;
                        entry.info.nFileIndexLow = file_index.LowPart;
                        ULARGE_INTEGER file_size;
                        file_size.QuadPart = dir[j].DIR_FileSize;
                        entry.info.nFileSizeHigh = file_size.HighPart;
                        entry.info.nFileSizeLow = file_size.LowPart;
                        entry.info.nNumberOfLinks = (dir[j].DIR_Attr & 0x10) ? 2 : 1;
                        res.push_back(entry);
                    }
                }
            }
        }
        else
        {
            break;
        }
    }
    return std::move(res);
}

uint32_t dev_t::get_first_clus(const fat32::path &path, int *isdir)
{
    uint32_t parent_dir_clus = get_root_clus();
    int is_dir = 1;
    for (const auto &name : path)
    {
        auto dir = open_dir(parent_dir_clus);
        int find = 0;
        for (const auto &entry : dir)
        {
            if (name == entry.name)
            {
                if (!(entry.info.dwFileAttributes & 0x10))
                {
                    if (&name == &path.back())
                    {
                        is_dir = 0;
                    }
                    else
                    {
                        throw fat32::file_error(fat32::file_error::FILE_NOT_FOUND);
                    }
                }
                parent_dir_clus = entry.first_clus;
                find = 1;
                break;
            }
        }
        if (!find)
        {
            throw fat32::file_error(fat32::file_error::FILE_NOT_FOUND);
        }
    }
    if (isdir)
        *isdir = is_dir;
    return parent_dir_clus;
}

} // namespace dev_io