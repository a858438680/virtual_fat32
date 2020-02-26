#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <timezoneapi.h>
#include <sysinfoapi.h>
#include "dev_io.h"
#include "dokan_log.h"

namespace dev_io
{

unsigned char ChkSum(unsigned char *pFcbName)
{
    short FcbNameLen;
    unsigned char Sum;
    Sum = 0;
    for (FcbNameLen = 11; FcbNameLen != 0; FcbNameLen--)
    {
        // NOTE: The operation is an unsigned char rotate right
        Sum = ((Sum & 1) ? 0x80 : 0) + (Sum >> 1) + *pFcbName++;
    }
    return (Sum);
}

DWORD FatAttr2FileAttr(uint8_t attr)
{
    DWORD res = 0;
    res |= (attr & 0x37);
    return res;
}

void EntryInfo2DirEntry(fat32::Entry_Info *pinfo, fat32::DIR_Entry *pentry, int have_long)
{
    if (have_long)
    {
        size_t index = 0;
        auto len = (wcslen(pinfo->name) + 13 - 1) / 13;
        auto ldir = (fat32::LDIR_Entry *)pentry;
        pentry += len;
        auto chksum = ChkSum((unsigned char *)pinfo->short_name);
        memset(ldir, 0xff, len * sizeof(fat32::LDIR_Entry));
        for (size_t i = 0; i < len; ++i)
        {
            ldir[len - i - 1].LDIR_Ord = i + 1;
            memcpy(ldir[len - i - 1].LDIR_Name1, pinfo->name + index, sizeof(ldir[len - i - 1].LDIR_Name1));
            ldir[len - i - 1].LDIR_Attr = 0xf;
            ldir[len - i - 1].LDIR_Type = 0;
            ldir[len - i - 1].LDIR_Chksum = chksum;
            memcpy(ldir[len - i - 1].LDIR_Name2, pinfo->name + index + 5, sizeof(ldir[len - i - 1].LDIR_Name2));
            ldir[len - i - 1].LDIR_FstClusLO = 0;
            memcpy(ldir[len - i - 1].LDIR_Name3, pinfo->name + index + 11, sizeof(ldir[len - i - 1].LDIR_Name3));
            //need change to correctly implement padding and null termination
        }
        ldir[0].LDIR_Ord |= 0x40;
        memcpy(pentry->DIR_Name, pinfo->short_name, sizeof(pinfo->short_name));
    }
    else
    {
        static char this_folder[] = {0x2e, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
        static char parent_folder[] = {0x2e, 0x2e, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
        if (wcslen(pinfo->name) == 1)
        {
            memcpy(pentry->DIR_Name, this_folder, sizeof(this_folder));
        }
        else
        {
            memcpy(pentry->DIR_Name, parent_folder, sizeof(parent_folder));
        }
    }
    pentry->DIR_Attr = (pinfo->info.dwFileAttributes & 0x37);
    pentry->DIR_FstClusHI = ((pinfo->first_clus >> 16) & 0xffff);
    pentry->DIR_FstClusLO = (pinfo->first_clus & 0xffff);
    ULARGE_INTEGER int_tmp;
    int_tmp.HighPart = pinfo->info.nFileSizeHigh;
    int_tmp.LowPart = pinfo->info.nFileSizeLow;
    pentry->DIR_FileSize = int_tmp.QuadPart;
    SYSTEMTIME systime;
    FILETIME filetime;
    FileTimeToLocalFileTime(&pinfo->info.ftCreationTime, &filetime);
    FileTimeToSystemTime(&filetime, &systime);
    pentry->DIR_CrtDate = ((systime.wYear - 1980) << 9) + (systime.wMonth << 5) + systime.wDay;
    pentry->DIR_CrtTime = (systime.wHour << 11) + (systime.wMinute << 5) + (systime.wSecond / 2);
    pentry->DIR_CrtTimeTenth = (systime.wSecond % 2) * 100 + systime.wMilliseconds / 10;
    FileTimeToLocalFileTime(&pinfo->info.ftLastWriteTime, &filetime);
    FileTimeToSystemTime(&filetime, &systime);
    pentry->DIR_WrtDate = ((systime.wYear - 1980) << 9) + (systime.wMonth << 5) + systime.wDay;
    pentry->DIR_WrtTime = (systime.wHour << 11) + (systime.wMinute << 5) + (systime.wSecond / 2);
    FileTimeToLocalFileTime(&pinfo->info.ftLastAccessTime, &filetime);
    FileTimeToSystemTime(&filetime, &systime);
    pentry->DIR_LstAccDate = ((systime.wYear - 1980) << 9) + (systime.wMonth << 5) + systime.wDay;
}

uint64_t dev_t::open(const fat32::path &path, uint32_t create_disposition, uint32_t file_attr, bool &exist, bool &isdir)
{
    cleared = false;
    if (path.empty())
    {
        if (create_disposition == CREATE_NEW)
        {
            throw fat32::file_error(fat32::file_error::FILE_ALREADY_EXISTS);
        }
        exist = true;
        isdir = true;
        root->ref_count.fetch_add(1, std::memory_order_relaxed);
        return (uint64_t)root.get();
    }
    else
    {
        auto last = root.get();
        decltype(last->children.begin()) itr;
        bool end = false;
        for (const auto &name : path)
        {
            if (!end)
            {
                end = (itr = last->children.find(name)) == last->children.end();
            }
            if (!end)
            {
                last = itr->second.get();
            }
            else
            {
                if (last->info.info.dwFileAttributes & 0x10)
                {
                    fat32::Entry_Info buf;
                    size_t index = 0;
                    auto find = false;
                    int32_t ret;
                    while (index < last->entries.size() && (ret = DirEntry2EntryInfo(last->entries.data() + index, &buf)))
                    {
                        if (ret < 0)
                        {
                            index -= ret;
                        }
                        else
                        {
                            if (name == buf.name)
                            {
                                auto ptr = open_file(last, &buf);
                                auto temp = ptr.get();
                                last->children.insert(std::make_pair(name, std::move(ptr)));
                                open_file_table.insert((uint64_t)temp);
                                last = temp;
                                find = true;
                                break;
                            }
                            index += ret;
                        }
                    }
                    if (!find)
                    {
                        if ((create_disposition == CREATE_ALWAYS || create_disposition == CREATE_NEW) && &name == &path.back())
                        {
                            exist = false;
                            isdir = file_attr & 0x10;
                            fat32::Entry_Info info;
                            wcscpy(info.name, name.c_str());
                            memset(info.short_name, 0x20, sizeof(info.short_name));
                            auto ret = name.find_last_of(L'.');
                            if (ret == std::wstring::npos)
                            {
                                size_t index = 0;
                                for (auto wc : name)
                                {
                                    if (wc >= 256)
                                    {
                                        if (index + 2 <= 6)
                                        {
                                            *(wchar_t *)(info.short_name + index) = wc;
                                            index += 2;
                                        }
                                        else
                                        {
                                            break;
                                        }
                                    }
                                    else
                                    {
                                        if (index + 1 <= 6)
                                        {
                                            info.short_name[index] = toupper(wc);
                                            index += 1;
                                        }
                                        else
                                        {
                                            break;
                                        }
                                    }
                                }
                                info.short_name[index++] = '~';
                                info.short_name[index++] = '1';
                            }
                            else
                            {
                                size_t index = 0;
                                for (size_t i = 0; i < ret; ++i)
                                {
                                    auto wc = name[i];
                                    if (wc >= 256)
                                    {
                                        if (index + 2 <= 6)
                                        {
                                            *(wchar_t *)(info.short_name + index) = wc;
                                            index += 2;
                                        }
                                        else
                                        {
                                            break;
                                        }
                                    }
                                    else
                                    {
                                        if (index + 1 <= 6)
                                        {
                                            info.short_name[index] = toupper(wc);
                                            index += 1;
                                        }
                                        else
                                        {
                                            break;
                                        }
                                    }
                                }
                                info.short_name[index++] = '~';
                                info.short_name[index++] = '1';
                                index = 8;
                                for (size_t i = ret + 1; i < name.size(); ++i)
                                {
                                    auto wc = name[i];
                                    if (wc >= 256)
                                    {
                                        if (index + 2 <= 11)
                                        {
                                            *(wchar_t *)(info.short_name + index) = wc;
                                            index += 2;
                                        }
                                        else
                                        {
                                            break;
                                        }
                                    }
                                    else
                                    {
                                        if (index + 1 <= 11)
                                        {
                                            info.short_name[index] = toupper(wc);
                                            index += 1;
                                        }
                                        else
                                        {
                                            break;
                                        }
                                    }
                                }
                            }
                            info.first_clus = 0;
                            info.info.dwFileAttributes = (file_attr & 0x37);
                            info.info.dwVolumeSerialNumber = get_vol_id();
                            SYSTEMTIME time;
                            GetSystemTime(&time);
                            SystemTimeToFileTime(&time, &info.info.ftCreationTime);
                            time.wMilliseconds = 0;
                            time.wSecond &= 0xfffffffe;
                            SystemTimeToFileTime(&time, &info.info.ftLastWriteTime);
                            time.wHour = 0;
                            time.wMinute = 0;
                            time.wSecond = 0;
                            SystemTimeToFileTime(&time, &info.info.ftLastAccessTime);
                            info.info.nFileIndexHigh = 0;
                            info.info.nFileIndexLow = 0;
                            info.info.nFileSizeHigh = 0;
                            info.info.nFileSizeLow = 0;
                            info.info.nNumberOfLinks = 1;
                            add_entry(last, &info);
                            auto ptr = open_file(last, &info);
                            ptr->ref_count.fetch_add(1, std::memory_order_relaxed);
                            auto temp = ptr.get();
                            last->children.insert(std::make_pair(name, std::move(ptr)));
                            open_file_table.insert((uint64_t)temp);
                            return (uint64_t)temp;
                        }
                        else
                        {
                            clear_node(last);
                            throw fat32::file_error(fat32::file_error::FILE_NOT_FOUND);
                        }
                    }
                }
                else
                {
                    clear_node(last);
                    throw fat32::file_error(fat32::file_error::FILE_NOT_FOUND);
                }
            }
        }
        if (create_disposition == CREATE_NEW)
        {
            clear_node(last);
            throw fat32::file_error(fat32::file_error::FILE_ALREADY_EXISTS);
        }
        if (create_disposition == CREATE_ALWAYS || create_disposition == TRUNCATE_EXISTING)
        {
            ULARGE_INTEGER int_tmp;
            int_tmp.QuadPart = 0;
            last->info.info.nFileSizeHigh = int_tmp.HighPart;
            last->info.info.nFileSizeLow = int_tmp.LowPart;
        }
        exist = true;
        isdir = (last->info.info.dwFileAttributes & 0x10);
        last->ref_count.fetch_add(1, std::memory_order_relaxed);
        open_file_table.insert((uint64_t)last);
        return (uint64_t)last;
    }
    return 0;
}

uint64_t dev_t::unlink(const fat32::path &path) { return 0; }

void dev_t::rename(const fat32::path &oldpath, const fat32::path &newpath) {}

void dev_t::mkdir(const fat32::path &path) {}

void dev_t::rmdir(const fat32::path &path) {}

void dev_t::close(uint64_t fd)
{
    if (open_file_table.find(fd) != open_file_table.end())
    {
        auto p = (fat32::file_node *)fd;
        auto ref = p->ref_count.fetch_sub(std::memory_order_acquire);
        clear_node(p);
    }
    else
    {
        throw fat32::file_error(fat32::file_error::INVALID_FILE_DISCRIPTOR);
    }
}

uint32_t dev_t::read(uint64_t fd, int64_t offset, uint32_t len, void *buffer)
{
    cleared = false;
    if (open_file_table.find(fd) != open_file_table.end())
    {
        auto p = (fat32::file_node *)fd;
        ULARGE_INTEGER tmp;
        tmp.HighPart = p->info.info.nFileSizeHigh;
        tmp.LowPart = p->info.info.nFileSizeLow;
        uint64_t file_size = tmp.QuadPart;
        int64_t left_border = offset > 0 ? offset : 0;
        int64_t right_border = (offset + len) < file_size ? (offset + len) : file_size;
        if (left_border >= right_border)
        {
            return 0;
        }
        std::vector<char> buf(clus_size);
        uint32_t begin_clus = left_border / clus_size;
        uint32_t end_clus = (right_border + clus_size - 1) / clus_size;
        uint32_t index = 0;
        for (auto i = begin_clus; i < end_clus; ++i)
        {
            read_clus(p->alloc[i], buf.data());
            uint32_t begin = 0, end = clus_size;
            if (i == begin_clus)
            {
                begin = left_border % clus_size;
            }
            if (i == end_clus - 1)
            {
                end = (right_border - 1) % clus_size + 1;
            }
            auto size = end - begin;
            memcpy((char *)buffer + index, buf.data() + begin, size);
            index += size;
        }
        SYSTEMTIME time;
        GetSystemTime(&time);
        time.wMilliseconds = 0;
        time.wHour = 0;
        time.wMinute = 0;
        time.wSecond = 0;
        SystemTimeToFileTime(&time, &p->info.info.ftLastAccessTime);
        return index;
    }
    else
    {
        throw fat32::file_error(fat32::file_error::INVALID_FILE_DISCRIPTOR);
    }
}

uint32_t dev_t::write(uint64_t fd, int64_t offset, uint32_t len, const void *buffer)
{
    cleared = false;
    if (open_file_table.find(fd) != open_file_table.end())
    {
        auto p = (fat32::file_node *)fd;
        if (!len)
        {
            return 0;
        }
        ULARGE_INTEGER tmp;
        tmp.HighPart = p->info.info.nFileSizeHigh;
        tmp.LowPart = p->info.info.nFileSizeLow;
        uint64_t file_size = tmp.QuadPart;
        uint64_t left_border = (offset > 0 ? offset : 0);
        uint64_t right_border = left_border + len;
        std::vector<char> buf(clus_size, 0);
        uint32_t begin_clus = left_border / clus_size;
        uint32_t end_clus = (right_border + clus_size - 1) / clus_size;
        uint32_t index = 0;
        if (end_clus > p->alloc.size())
        {
            auto origin = p->alloc.size();
            extend(p, end_clus);
            if (begin_clus >= origin)
            {
                for (auto i = origin; i < begin_clus; ++i)
                {
                    write_clus(p->alloc[i], buf.data());
                }
            }
        }
        for (auto i = begin_clus; i < end_clus; ++i)
        {
            int read = 0;
            uint32_t begin = 0, end = clus_size;
            if (i == begin_clus)
            {
                begin = left_border % clus_size;
                read = 1;
            }
            if (i == end_clus - 1)
            {
                end = (right_border - 1) % clus_size + 1;
                read = 1;
            }
            auto size = end - begin;
            if (read)
            {
                read_clus(p->alloc[i], buf.data());
                memcpy(buf.data() + begin, (char *)buffer + i * clus_size + begin, size);
                write_clus(p->alloc[i], buf.data());
            }
            else
            {
                write_clus(p->alloc[i], (char *)buffer + i * clus_size);
            }
            index += size;
        }
        if (right_border > file_size)
        {
            tmp.QuadPart = right_border;
            p->info.info.nFileSizeHigh = tmp.HighPart;
            p->info.info.nFileSizeLow = tmp.LowPart;
        }
        SYSTEMTIME time;
        GetSystemTime(&time);
        time.wMilliseconds = 0;
        time.wSecond &= 0xfffffffe;
        SystemTimeToFileTime(&time, &p->info.info.ftLastWriteTime);
        time.wHour = 0;
        time.wMinute = 0;
        time.wSecond = 0;
        SystemTimeToFileTime(&time, &p->info.info.ftLastAccessTime);
        return index;
    }
    else
    {
        throw fat32::file_error(fat32::file_error::INVALID_FILE_DISCRIPTOR);
    }
}

void dev_t::fstat(uint64_t fd, LPBY_HANDLE_FILE_INFORMATION statbuf)
{
    cleared = false;
    if (open_file_table.find(fd) != open_file_table.end())
    {
        auto p = (fat32::file_node *)fd;
        memcpy(statbuf, &p->info.info, sizeof(BY_HANDLE_FILE_INFORMATION));
    }
    else
    {
        throw fat32::file_error(fat32::file_error::INVALID_FILE_DISCRIPTOR);
    }
}

fat32::dir_info dev_t::opendir(uint64_t fd)
{
    cleared = false;
    if (open_file_table.find(fd) != open_file_table.end())
    {
        auto p = (fat32::file_node *)fd;
        if (!(p->info.info.dwFileAttributes & 0x10))
        {
            throw fat32::file_error(fat32::file_error::FILE_NOT_DIR);
        }
        for (const auto &c : p->children)
        {
            add_entry(p, &c.second->info);
        }
        fat32::dir_info res;
        fat32::Entry_Info buf;
        size_t index = 0;
        int32_t ret;
        while (index < p->entries.size() && (ret = DirEntry2EntryInfo(p->entries.data() + index, &buf)))
        {
            if (ret < 0)
            {
                index -= ret;
            }
            else
            {
                res.push_back(buf);
                index += ret;
            }
        }
        return std::move(res);
    }
    else
    {
        throw fat32::file_error(fat32::file_error::INVALID_FILE_DISCRIPTOR);
    }
}

void dev_t::flush() {}

std::unique_ptr<fat32::file_node> dev_t::open_file(fat32::file_node *parent, fat32::Entry_Info *pinfo)
{
    auto res = std::make_unique<fat32::file_node>(parent);
    auto first_clus = pinfo->first_clus;
    while (first_clus >= 2 && first_clus < count_of_cluster + 2)
    {
        res->alloc.push_back(first_clus);
        first_clus = get_fat(first_clus);
    }
    memcpy(&res->info, parent ? pinfo : &root_info, sizeof(fat32::Entry_Info));
    if (res->info.info.dwFileAttributes & 0x10)
    {
        res->entries.resize(res->alloc.size() * clus_size / sizeof(fat32::DIR_Entry));
        size_t count = 0;
        for (auto clus_no : res->alloc)
        {
            read_clus(clus_no, res->entries.data() + count);
            count += clus_size / sizeof(fat32::DIR_Entry);
        }
    }
    return std::move(res);
}

void dev_t::save(fat32::file_node *node)
{
    if (node)
    {
        if (node->parent)
        {
            add_entry(node->parent, &node->info);
        }
        if (node->info.info.dwFileAttributes & 0x10)
        {
            for (auto &p : node->children)
            {
                save(p.second.get());
            }
            size_t index = 0;
            for (auto clus : node->alloc)
            {
                write_clus(clus, node->entries.data() + index * (clus_size / sizeof(fat32::DIR_Entry)));
                ++index;
            }
        }
    }
}

void dev_t::clear_node(fat32::file_node *node)
{
    if (!node->ref_count.load(std::memory_order_acquire) && node->children.empty())
    {
        if (node->parent)
        {
            std::wstring name = node->info.name;
            auto parent = node->parent;
            add_entry(parent, &node->info);
            if (node->info.info.dwFileAttributes & 0x10)
            {
                size_t index = 0;
                for (auto clus : node->alloc)
                {
                    write_clus(clus, node->entries.data() + index * (clus_size / sizeof(fat32::DIR_Entry)));
                    ++index;
                }
            }
            open_file_table.erase((uint64_t)node);
            parent->children.erase(name);
            clear_node(parent);
        }
        else
        {
            size_t index = 0;
            for (auto clus : node->alloc)
            {
                write_clus(clus, node->entries.data() + index * (clus_size / sizeof(fat32::DIR_Entry)));
                ++index;
            }
        }
    }
}

uint32_t dev_t::next_free()
{
    auto clus = FSInfo.FSI_Nxt_Free;
    while (get_fat(clus))
    {
        ++clus;
    }
    set_fat(clus, 0x0fffffff);
    FSInfo.FSI_Nxt_Free = clus + 1;
    --FSInfo.FSI_FreeCount;
    return clus;
}

void dev_t::free_clus(uint32_t clus_no)
{
    set_fat(clus_no, 0);
    if (clus_no < FSInfo.FSI_Nxt_Free)
    {
        FSInfo.FSI_Nxt_Free = clus_no;
    }
    ++FSInfo.FSI_FreeCount;
}

void dev_t::extend(fat32::file_node *node, uint32_t clus_count)
{
    if (node->alloc.size() < clus_count)
    {
        size_t extend_size = clus_count - node->alloc.size();
        for (size_t i = 0; i < extend_size; ++i)
        {
            auto next = next_free();
            node->alloc.push_back(next);
            if (node->alloc.empty())
            {
                node->info.first_clus = next;
            }
            else
            {
                set_fat(node->alloc.back(), next);
            }
        }
    }
}

void dev_t::shrink(fat32::file_node *node, uint32_t clus_count)
{
    if (node->alloc.size() > clus_count)
    {
        size_t shrink_size = node->alloc.size() - clus_count;
        for (size_t i = 0; i < shrink_size; ++i)
        {
            free_clus(node->alloc.back());
            node->alloc.pop_back();
        }
        if (node->alloc.empty())
        {
            node->info.first_clus = 0;
        }
        else
        {
            set_fat(node->alloc.back(), 0x0fffffff);
        }
    }
}

void dev_t::add_entry(fat32::file_node *node, fat32::Entry_Info *pinfo)
{
    log_msg("add_entry: %s\n", wide2local(pinfo->name).c_str());
    log_pointer("    ", pinfo);
    int have_long = 1;
    size_t entry_len;
    if (wcscmp(pinfo->name, L".") == 0 || wcscmp(pinfo->name, L"..") == 0)
    {
        have_long = 0;
        entry_len = 1;
    }
    else
    {
        entry_len = (wcslen(pinfo->name) + 26 - 1) / 13;
    }
    size_t index = 0;
    fat32::Entry_Info buf;
    int32_t ret;
    while (index < node->entries.size() && (ret = DirEntry2EntryInfo(node->entries.data() + index, &buf)))
    {
        if (ret < 0)
        {
            index -= ret;
        }
        else
        {
            if (wcscmp(buf.name, pinfo->name) == 0)
            {
                if (ret >= entry_len)
                {
                    EntryInfo2DirEntry(pinfo, node->entries.data() + index, have_long);
                    for (size_t i = index + entry_len; i < index + ret; ++i)
                    {
                        *(uint8_t *)node->entries[i].DIR_Name = 0xe5;
                    }
                    return;
                }
                else
                {
                    for (size_t i = index; i < index + ret; ++i)
                    {
                        *(uint8_t *)node->entries[i].DIR_Name = 0xe5;
                    }
                }
            }
            index += ret;
        }
    }
    if (node->entries.size() - index < entry_len)
    {
        size_t new_clus_count = ((index + entry_len) * sizeof(fat32::DIR_Entry) + clus_size - 1) / clus_size;
        extend(node, new_clus_count);
        node->entries.resize(new_clus_count * clus_size / sizeof(fat32::DIR_Entry));
        memset(node->entries.data() + index + entry_len, 0, (node->entries.size() - index - entry_len) * sizeof(fat32::DIR_Entry));
    }
    EntryInfo2DirEntry(pinfo, node->entries.data() + index, have_long);
}

void dev_t::remove_entry(fat32::file_node *node, std::wstring_view name)
{
    size_t index = 0;
    fat32::Entry_Info buf;
    int32_t ret;
    while (index < node->entries.size() && (ret = DirEntry2EntryInfo(node->entries.data() + index, &buf)))
    {
        if (ret < 0)
        {
            index -= ret;
        }
        else
        {
            if (buf.name == name)
            {
                for (size_t i = index; i < index + ret; ++i)
                {
                    *(uint8_t *)node->entries[i].DIR_Name = 0xe5;
                }
                return;
            }
            index += ret;
        }
    }
}

int32_t dev_t::DirEntry2EntryInfo(fat32::DIR_Entry *pdir, fat32::Entry_Info *pinfo)
{
    if (pdir[0].DIR_Name[0] == 0)
    {
        return 0;
    }
    int count = 0;
    while ((uint8_t)pdir[count].DIR_Name[0] == 0xe5)
    {
        ++count;
    }
    if (count)
    {
        if (pdir[count].DIR_Name[0] == 0)
        {
            return 0;
        }
        return -count;
    }
    wchar_t name[256];
    int name_pos = 255;
    int have_long_name = 0;
    while ((pdir[count].DIR_Attr & 0x3f) == 0x0f)
    {
        fat32::LDIR_Entry *ldir = (fat32::LDIR_Entry *)pdir;
        if (ldir[count].LDIR_Ord & 0x40)
        {
            wchar_t tmp[13];
            memcpy(tmp + 11, ldir[count].LDIR_Name3, sizeof(ldir[count].LDIR_Name3));
            memcpy(tmp + 5, ldir[count].LDIR_Name2, sizeof(ldir[count].LDIR_Name2));
            memcpy(tmp, ldir[count].LDIR_Name1, sizeof(ldir[count].LDIR_Name1));
            size_t len = wcsnlen(tmp, 13);
            name_pos = 255 - len;
            wcsncpy(name + name_pos, tmp, 13);
            have_long_name = 1;
        }
        else
        {
            memcpy(name + name_pos - 2, ldir[count].LDIR_Name3, sizeof(ldir[count].LDIR_Name3));
            memcpy(name + name_pos - 8, ldir[count].LDIR_Name2, sizeof(ldir[count].LDIR_Name2));
            memcpy(name + name_pos - 13, ldir[count].LDIR_Name1, sizeof(ldir[count].LDIR_Name1));
            name_pos -= 13;
        }
        ++count;
    }
    memset(pinfo, 0, sizeof(fat32::Entry_Info));
    if (have_long_name)
    {
        wcscpy(pinfo->name, name + name_pos);
        memcpy(pinfo->short_name, pdir[count].DIR_Name, sizeof(pinfo->short_name));
    }
    else
    {
        wchar_t c;
        int index = 0;
        for (int k = 0; k < 8 && (c = pdir[count].DIR_Name[k]) != 0x20; ++k)
        {
            pinfo->name[index++] = c;
        }
        if (pdir[count].DIR_Name[8] != 0x20)
        {
            pinfo->name[index++] = L'.';
            for (int k = 8; k < 11 && (c = pdir[count].DIR_Name[k]) != 0x20; ++k)
            {
                pinfo->name[index++] = c;
            }
        }
    }
    pinfo->first_clus = ((uint32_t)(pdir[count].DIR_FstClusHI) << 16) + pdir[count].DIR_FstClusLO;
    pinfo->info.dwFileAttributes = FatAttr2FileAttr(pdir[count].DIR_Attr);
    pinfo->info.dwVolumeSerialNumber = get_vol_id();
    SYSTEMTIME systime;
    FILETIME filetime;
    memset(&systime, 0, sizeof(SYSTEMTIME));
    systime.wYear = (pdir[count].DIR_CrtDate >> 9) + 1980;
    systime.wMonth = ((pdir[count].DIR_CrtDate & 0x1e0) >> 5);
    systime.wDay = (pdir[count].DIR_CrtDate & 0x1f);
    systime.wHour = (pdir[count].DIR_CrtTime >> 11);
    systime.wMinute = ((pdir[count].DIR_CrtTime & 0x7e0) >> 5);
    systime.wSecond = (pdir[count].DIR_CrtTime & 0x1f) * 2 + pdir[count].DIR_CrtTimeTenth / 100;
    systime.wMilliseconds = (pdir[count].DIR_CrtTimeTenth % 100) * 10;
    SystemTimeToFileTime(&systime, &filetime);
    LocalFileTimeToFileTime(&filetime, &pinfo->info.ftCreationTime);
    memset(&systime, 0, sizeof(SYSTEMTIME));
    systime.wYear = (pdir[count].DIR_WrtDate >> 9) + 1980;
    systime.wMonth = ((pdir[count].DIR_WrtDate & 0x1e0) >> 5);
    systime.wDay = (pdir[count].DIR_WrtDate & 0x1f);
    systime.wHour = (pdir[count].DIR_WrtTime >> 11);
    systime.wMinute = ((pdir[count].DIR_WrtTime & 0x7e0) >> 5);
    systime.wSecond = (pdir[count].DIR_WrtTime & 0x1f) * 2;
    SystemTimeToFileTime(&systime, &filetime);
    LocalFileTimeToFileTime(&filetime, &pinfo->info.ftLastWriteTime);
    memset(&systime, 0, sizeof(SYSTEMTIME));
    systime.wYear = (pdir[count].DIR_LstAccDate >> 9) + 1980;
    systime.wMonth = ((pdir[count].DIR_LstAccDate & 0x1e0) >> 5);
    systime.wDay = (pdir[count].DIR_LstAccDate & 0x1f);
    SystemTimeToFileTime(&systime, &filetime);
    LocalFileTimeToFileTime(&filetime, &pinfo->info.ftLastAccessTime);
    ULARGE_INTEGER file_index;
    file_index.QuadPart = pinfo->first_clus;
    pinfo->info.nFileIndexHigh = file_index.HighPart;
    pinfo->info.nFileIndexLow = file_index.LowPart;
    ULARGE_INTEGER file_size;
    file_size.QuadPart = pdir[count].DIR_FileSize;
    pinfo->info.nFileSizeHigh = file_size.HighPart;
    pinfo->info.nFileSizeLow = file_size.LowPart;
    pinfo->info.nNumberOfLinks = (pdir[count].DIR_Attr & 0x10) ? 2 : 1;
    return count + 1;
}

} // namespace dev_io