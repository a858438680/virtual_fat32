#include <mutex>
#include <vector>
#include <string>
#include <utility>
#include <stdlib.h>
#include <time.h>
#include <windows.h>
#include "dev_io.h"

std::mutex m;

std::wstring local2wide(const char *s)
{
    auto wide_size = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    std::wstring res;
    res.resize(wide_size);
    wide_size = MultiByteToWideChar(CP_ACP, 0, s, -1, res.data(), wide_size);
    return std::move(res);
}

namespace dev_io
{

struct DSKSZTOSECPERCLUS
{
    DWORD DiskSize;
    BYTE SecPerClusVal;
};

DSKSZTOSECPERCLUS DskTableFAT32[] = {
    {66600, 0},      /* disks up to 32.5 MB, the 0 value for SecPerClusVal trips an error */
    {532480, 1},     /* disks up to 260 MB, .5k cluster */
    {16777216, 8},   /* disks up to 8 GB, 4k cluster */
    {33554432, 16},  /* disks up to 16 GB, 8k cluster */
    {67108864, 32},  /* disks up to 32 GB, 16k cluster */
    {0xFFFFFFFF, 64} /* disks greater than 32GB, 32k cluster */
};

void set_BPB_SecPerClus(fat32::BPB_t *pBPB)
{
    for (int i = 0; i < 6; ++i)
    {
        if (pBPB->BPB_TotSec32 <= DskTableFAT32[i].DiskSize)
        {
            pBPB->BPB_SecPerClus = DskTableFAT32[i].SecPerClusVal;
            return;
        }
    }
}

void set_BPB_FATSz32(fat32::BPB_t *pBPB)
{
    uint32_t TmpVal1 = pBPB->BPB_TotSec32 - pBPB->BPB_RsvdSecCnt;
    uint32_t TmpVal2 = (256 * pBPB->BPB_SecPerClus) + pBPB->BPB_NumFATs;
    TmpVal2 /= 2;
    pBPB->BPB_FATSz32 = (TmpVal1 + (TmpVal2 - 1)) / TmpVal2;
}

void set_BPB(uint32_t tot_block, uint16_t block_size, fat32::BPB_t *pBPB)
{
    memset(pBPB, 0, sizeof(fat32::BPB_t));
    pBPB->BS_jmpBoot[0] = 0xEB;
    pBPB->BS_jmpBoot[1] = 0x58;
    pBPB->BS_jmpBoot[2] = 0x90;
    strcpy(pBPB->BS_OEMName, "ALAN.FAT");
    pBPB->BPB_BytsPerSec = block_size;
    pBPB->BPB_TotSec32 = tot_block;
    set_BPB_SecPerClus(pBPB);
    pBPB->BPB_RsvdSecCnt = (31 + pBPB->BPB_SecPerClus) / pBPB->BPB_SecPerClus * pBPB->BPB_SecPerClus;
    pBPB->BPB_NumFATs = 2;
    // pBPB->BPB_RootEntCnt = 0;
    // pBPB->BPB_TotSec16 = 0;
    pBPB->BPB_Media = 0xF8;
    // pBPB->BPB_FATSz16 = 0;
    // pBPB->BPB_SecPerTrk = 0;
    // pBPB->BPB_NumHeads= 0;
    // pBPB->BPB_HiddSec = 0;
    set_BPB_FATSz32(pBPB);
    // pBPB->BPB_ExtFlags = 0;
    // pBPB->BPB_FSVer = 0;
    pBPB->BPB_RootClus = 2;
    pBPB->BPB_FSInfo = 1;
    pBPB->BPB_BkBootSec = 6;
    pBPB->BS_DrvNum = 0x80;
    pBPB->BS_BootSig = 0x29;
    auto t = time(NULL);
    auto pt = localtime(&t);
    pBPB->BS_VolID = ((uint32_t)(pt->tm_yday - 80) << 25) +
                     ((uint32_t)(pt->tm_mon + 1) << 21) +
                     ((uint32_t)(pt->tm_mday + 1) << 16) +
                     ((uint32_t)(pt->tm_hour) << 11) +
                     ((uint32_t)(pt->tm_min) << 5) +
                     ((uint32_t)(pt->tm_sec / 2));
    strcpy(pBPB->BS_VolLab, "NO NAME");
    strcpy(pBPB->BS_FilSysType, "FAT32");
    pBPB->Signature_word = 0xAA55;
}

void set_FSInfo(fat32::FSInfo_t *pFSInfo, fat32::BPB_t *pBPB)
{
    memset(pFSInfo, 0, sizeof(fat32::FSInfo_t));
    pFSInfo->FSI_LeadSig = 0x41615252;
    pFSInfo->FSI_StrucSig = 0x61417272;
    pFSInfo->FSI_Nxt_Free = 3;
    uint32_t data_begin = pBPB->BPB_FATSz32 * pBPB->BPB_NumFATs + pBPB->BPB_RsvdSecCnt;
    pFSInfo->FSI_FreeCount = (pBPB->BPB_TotSec32 - data_begin) / pBPB->BPB_SecPerClus - 1;
    pFSInfo->FSI_TrailSig = 0xAA550000;
}

dev_t::dev_t(const char *dev_name, uint32_t tot_block, uint16_t block_size)
    : dev_img(INVALID_HANDLE_VALUE), cleared(true)
{
    try
    {
        auto wdev_name = local2wide(dev_name);
        dev_img = CreateFileW(wdev_name.c_str(),
                              GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
                              NULL);
        if (dev_img == INVALID_HANDLE_VALUE)
            throw disk_error(disk_error::DISK_OPEN_ERROR);
        LARGE_INTEGER file_size;
        file_size.QuadPart = tot_block * block_size;
        if (!SetFilePointerEx(dev_img, file_size, NULL, FILE_BEGIN))
            throw disk_error(disk_error::DISK_SEEK_ERROR);
        if (!SetEndOfFile(dev_img))
            throw disk_error(disk_error::DISK_EXTEND_ERROR);
        format(tot_block, block_size);
    }
    catch (disk_error &e)
    {
        if (dev_img != NULL && dev_img != INVALID_HANDLE_VALUE)
            CloseHandle(dev_img);
        throw e;
    }
}

dev_t::dev_t(const char *dev_name)
    : dev_img(INVALID_HANDLE_VALUE), cleared(true)
{
    try
    {
        auto wdev_name = local2wide(dev_name);
        dev_img = CreateFileW(wdev_name.c_str(),
                              GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
                              NULL);
        if (dev_img == INVALID_HANDLE_VALUE)
        {
            if (GetLastError() == ERROR_FILE_NOT_FOUND)
                throw disk_error(disk_error::DISK_NOT_FOUND);
            else
                throw disk_error(disk_error::DISK_OPEN_ERROR);
        }
        dev_read(dev_img, 0, sizeof(fat32::BPB_t), &BPB);
        if (BPB.Signature_word != 0xaa55)
        {
            throw disk_error(disk_error::DISK_SIGNATURE_ERROR);
        }
        dev_read(dev_img, BPB.BPB_FSInfo * BPB.BPB_BytsPerSec,
                 sizeof(fat32::FSInfo_t), &FSInfo);
        if (FSInfo.FSI_LeadSig != 0x41615252 || FSInfo.FSI_StrucSig != 0x61417272 ||
            FSInfo.FSI_TrailSig != 0xAA550000)
        {
            throw disk_error(disk_error::DISK_SIGNATURE_ERROR);
        }
        clac_info();
    }
    catch (disk_error &e)
    {
        CloseHandle(dev_img);
        throw e;
    }
}

dev_t::~dev_t()
{
    if (dev_img != NULL && dev_img != INVALID_HANDLE_VALUE)
    {
        clear();
        CloseHandle(dev_img);
    }
}

dev_t::operator bool() const noexcept
{
    return (dev_img != NULL && dev_img != INVALID_HANDLE_VALUE);
}

uint32_t dev_t::get_root_clus() const noexcept
{
    return BPB.BPB_RootClus;
}

uint32_t dev_t::get_vol_id() const noexcept
{
    return BPB.BS_VolID;
}

uint32_t dev_t::get_fat(uint32_t fat_no) const
{
    return FAT_Table[fat_no & 0x0fffffff] & 0x0fffffff;
}

void dev_t::set_fat(uint32_t fat_no, uint32_t value)
{
    FAT_Table[fat_no & 0x0fffffff] = (value & 0x0fffffff);
}

int32_t dev_t::read_block(uint32_t block_no, void *buf) const
{
    return dev_read(dev_img, block_no * block_size, block_size, buf);
}

int32_t dev_t::write_block(uint32_t block_no, const void *buf) const
{
    return dev_write(dev_img, block_no * block_size, block_size, buf);
}

int32_t dev_t::read_clus(uint32_t clus_no, void *buf) const
{
    clus_no -= 2;
    return dev_read(dev_img, (data_begin + sec_per_clus * clus_no) * block_size, clus_size, buf);
}

int32_t dev_t::write_clus(uint32_t clus_no, const void *buf) const
{
    clus_no -= 2;
    return dev_write(dev_img, (data_begin + sec_per_clus * clus_no) * block_size, clus_size, buf);
}

int32_t dev_t::dev_read(void *handle, uint64_t offset, uint32_t size,
                        void *buf)
{
    LARGE_INTEGER start_pos;
    start_pos.QuadPart = offset;
    std::lock_guard<std::mutex> g(m);
    if (!SetFilePointerEx(handle, start_pos, NULL, FILE_BEGIN))
        throw disk_error(disk_error::DISK_SEEK_ERROR);
    DWORD read_count;
    if (ReadFile(handle, buf, size, &read_count, NULL))
        return read_count;
    else
        throw disk_error(disk_error::DISK_READ_ERROR);
}

int32_t dev_t::dev_write(void *handle, uint64_t offset, uint32_t size,
                         const void *buf)
{
    LARGE_INTEGER start_pos;
    start_pos.QuadPart = offset;
    std::lock_guard<std::mutex> g(m);
    if (!SetFilePointerEx(handle, start_pos, NULL, FILE_BEGIN))
        throw disk_error(disk_error::DISK_SEEK_ERROR);
    DWORD write_count;
    if (WriteFile(handle, buf, size, &write_count, NULL))
        return write_count;
    else
        throw disk_error(disk_error::DISK_WRITE_ERROR);
}

void dev_t::format(uint32_t tot_block, uint16_t block_size)
{
    set_BPB(tot_block, block_size, &BPB);
    set_FSInfo(&FSInfo, &BPB);
    std::vector<uint32_t> FirstSec(block_size / sizeof(uint32_t), 0);
    std::vector<uint8_t> EmptySec(block_size * BPB.BPB_SecPerClus, 0);
    FirstSec[0] = 0x0ffffff8;
    FirstSec[1] = 0x0fffffff;
    FirstSec[2] = 0x0ffffff8;

    uint32_t data_begin = BPB.BPB_FATSz32 * BPB.BPB_NumFATs + BPB.BPB_RsvdSecCnt;
    for (uint32_t i = 0; i < data_begin; ++i)
    {
        dev_write(dev_img, i * block_size, block_size, EmptySec.data());
    }
    dev_write(dev_img, 0 * block_size, 512, &BPB);
    dev_write(dev_img, 6 * block_size, 512, &BPB);
    dev_write(dev_img, 1 * block_size, 512, &FSInfo);
    dev_write(dev_img, 7 * block_size, 512, &FSInfo);
    dev_write(dev_img, BPB.BPB_RsvdSecCnt * block_size, block_size, FirstSec.data());
    dev_write(dev_img, (BPB.BPB_FATSz32 + BPB.BPB_RsvdSecCnt) * block_size, block_size, FirstSec.data());
    dev_write(dev_img, data_begin * block_size, BPB.BPB_SecPerClus * block_size, EmptySec.data());
}

void dev_t::clac_info()
{
    FAT_Table.resize(BPB.BPB_FATSz32 * BPB.BPB_BytsPerSec / sizeof(uint32_t));
    for (uint32_t i = 0; i < BPB.BPB_FATSz32; ++i)
    {
        auto byte = i * BPB.BPB_BytsPerSec;
        dev_read(dev_img, (i + BPB.BPB_RsvdSecCnt) * BPB.BPB_BytsPerSec, BPB.BPB_BytsPerSec,
                 FAT_Table.data() + byte / sizeof(uint32_t));
    }
    tot_block = BPB.BPB_TotSec32;
    block_size = BPB.BPB_BytsPerSec;
    sec_per_clus = BPB.BPB_SecPerClus;
    clus_size = block_size * sec_per_clus;
    data_begin = BPB.BPB_RsvdSecCnt + BPB.BPB_NumFATs * BPB.BPB_FATSz32;
    count_of_cluster = (tot_block - data_begin) / sec_per_clus;
    memset(&root_info, 0, sizeof(fat32::Entry_Info));
    root_info.first_clus = get_root_clus();
    root_info.info.dwFileAttributes = 0x10;
    root_info.info.dwVolumeSerialNumber = get_vol_id();
    ULARGE_INTEGER file_index;
    file_index.QuadPart = get_root_clus();
    root_info.info.nFileIndexHigh = file_index.HighPart;
    root_info.info.nFileIndexLow = file_index.LowPart;
    root_info.info.nNumberOfLinks = 2;
    root = open_file(nullptr, &root_info);
    open_file_table.insert((uint64_t)root.get());
}

void dev_t::clear()
{
    if (!cleared)
    {
        write_block(0, &BPB);
        fat32::BPB_t BPB_Backup;
        memcpy(&BPB_Backup, &BPB, sizeof(BPB));
        BPB_Backup.BPB_BkBootSec = 0;
        write_block(0, &BPB_Backup);
        write_block(BPB.BPB_FSInfo, &FSInfo);
        for (uint32_t i = 0; i < BPB.BPB_FATSz32; ++i)
        {
            write_block(i + BPB.BPB_RsvdSecCnt, FAT_Table.data() + i * block_size / sizeof(uint32_t));
            write_block(i + BPB.BPB_RsvdSecCnt + BPB.BPB_FATSz32, FAT_Table.data() + i * block_size / sizeof(uint32_t));
        }
        save(root.get());
        flush();
        cleared = true;
    }
}

} // namespace dev_io