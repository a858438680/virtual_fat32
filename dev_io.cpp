#include "dev_io.h"
#include <Windows.h>
#include <mutex>
#include <stdlib.h>
#include <string>
#include <utility>
#include <vector>

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
    pBPB->BS_VolID = time(NULL);
    strcpy(pBPB->BS_VolLab, "NO NAME");
    strcpy(pBPB->BS_FilSysType, "FAT32");
    pBPB->Signature_word = 0xAA55;
}

void set_FSInfo(fat32::FSInfo_t *pFSInfo, fat32::BPB_t *pBPB)
{
    memset(pFSInfo, 0, sizeof(fat32::FSInfo_t));
    pFSInfo->FSI_LeadSig = 0x41615252;
    pFSInfo->FSI_StrucSig = 0x61417272;
    pFSInfo->FSI_Nxt_Free = 2;
    uint32_t data_begin = pBPB->BPB_FATSz32 * pBPB->BPB_NumFATs + pBPB->BPB_RsvdSecCnt;
    pFSInfo->FSI_FreeCount = (pBPB->BPB_TotSec32 - data_begin) / pBPB->BPB_SecPerClus;
    pFSInfo->FSI_TrailSig = 0xAA550000;
}

dev_t::dev_t(const char *dev_name, uint32_t tot_block, uint16_t block_size)
    : dev_img(INVALID_HANDLE_VALUE)
{
    try
    {
        auto wdev_name = local2wide(dev_name);
        dev_img = CreateFileW(wdev_name.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, NULL);
        if (dev_img == INVALID_HANDLE_VALUE)
            throw disk_error(disk_error::DISK_OPEN_ERROR);
        LARGE_INTEGER file_size;
        file_size.QuadPart = tot_block * block_size;
        if (!SetFilePointerEx(dev_img, file_size, NULL, FILE_BEGIN))
            throw disk_error(disk_error::DISK_SEEK_ERROR);
        if (!SetEndOfFile(dev_img))
            throw disk_error(disk_error::DISK_EXTEND_ERROR);
        format(tot_block, block_size);
        clac_info();
    }
    catch (disk_error &e)
    {
        if (dev_img != NULL && dev_img != INVALID_HANDLE_VALUE)
            CloseHandle(dev_img);
        throw e;
    }
}

dev_t::dev_t(const char *dev_name) : dev_img(INVALID_HANDLE_VALUE)
{
    try
    {
        auto wdev_name = local2wide(dev_name);
        dev_img = CreateFileW(wdev_name.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, NULL);
        if (dev_img == INVALID_HANDLE_VALUE)
        {
            if (GetLastError() == ERROR_FILE_NOT_FOUND)
                throw disk_error(disk_error::DISK_NOT_FOUND);
            else
                throw disk_error(disk_error::DISK_OPEN_ERROR);
        }
        LARGE_INTEGER file_size;
        if (!GetFileSizeEx(dev_img, &file_size))
            throw disk_error(disk_error::DISK_GET_SIZE_ERROR);
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

dev_t::dev_t(dev_t &&dev) noexcept
    : dev_img(dev.dev_img), BPB(dev.BPB), BPB_Backup(dev.BPB_Backup),
      FSInfo(dev.FSInfo), FAT_Table(std::move(FAT_Table)),
      data_begin(dev.data_begin), count_of_cluster(dev.count_of_cluster)
{
    dev.dev_img = INVALID_HANDLE_VALUE;
}

dev_t &dev_t::operator=(dev_t &&dev) noexcept
{
    auto tmp = dev_img;
    dev_img = dev.dev_img;
    BPB = dev.BPB;
    BPB_Backup = dev.BPB_Backup;
    FSInfo = dev.FSInfo;
    FAT_Table = std::move(dev.FAT_Table);
    data_begin = dev.data_begin;
    count_of_cluster = dev.count_of_cluster;
    dev.dev_img = INVALID_HANDLE_VALUE;
    if (tmp == NULL || tmp == INVALID_HANDLE_VALUE)
    {
        CloseHandle(tmp);
    }
    return *this;
}

dev_t::~dev_t()
{
    if (dev_img != NULL && dev_img != INVALID_HANDLE_VALUE)
    {
        CloseHandle(dev_img);
    }
}

dev_t::operator bool() const noexcept
{
    return (dev_img != NULL && dev_img != INVALID_HANDLE_VALUE);
}

uint16_t dev_t::get_block_size() const noexcept
{
    return BPB.BPB_BytsPerSec;
}

uint32_t dev_t::get_count_of_clus() const noexcept
{
    return count_of_cluster;
}

uint32_t dev_t::get_root_clus() const noexcept
{
    return BPB.BPB_RootClus;
}

uint8_t dev_t::get_sec_per_clus() const noexcept
{
    return BPB.BPB_SecPerClus;
}

uint32_t dev_t::get_total_block() const noexcept
{
    return BPB.BPB_TotSec32;
}

uint32_t dev_t::get_vol_id() const noexcept
{
    return BPB.BS_VolID;
}

uint32_t dev_t::get_fat(uint32_t fat_no) const
{
    return FAT_Table[fat_no & 0x7fffffff];
}

void dev_t::set_fat(uint32_t fat_no, uint32_t value)
{
    FAT_Table[fat_no & 0x7fffffff] = (value & 0x7fffffff);
}

int32_t dev_t::read_block(uint32_t block_no, void *buf) const
{
    return dev_read(dev_img, block_no * get_block_size(), get_block_size(), buf);
}

int32_t dev_t::write_block(uint32_t block_no, const void *buf) const
{
    return dev_write(dev_img, block_no * get_block_size(), get_block_size(), buf);
}

int32_t dev_t::read_clus(uint32_t clus_no, void *buf) const
{
    clus_no -= 2;
    return dev_read(dev_img, (data_begin + get_sec_per_clus() * clus_no) * get_block_size(), get_sec_per_clus() * get_block_size(), buf);
}

int32_t dev_t::write_clus(uint32_t clus_no, const void *buf) const
{
    clus_no -= 2;
    return dev_write(dev_img, (data_begin + get_sec_per_clus() * clus_no) * get_block_size(), get_sec_per_clus() * get_block_size(), buf);
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
    memcpy(&BPB_Backup, &BPB, sizeof(fat32::BPB_t));
    BPB_Backup.BPB_BkBootSec = 0;
    FAT_Table.resize(BPB.BPB_FATSz32 * BPB.BPB_BytsPerSec / sizeof(uint32_t));
    for (uint32_t i = 0; i < BPB.BPB_FATSz32; ++i)
    {
        auto byte = i * BPB.BPB_BytsPerSec;
        dev_read(dev_img, (i + BPB.BPB_RsvdSecCnt) * BPB.BPB_BytsPerSec, BPB.BPB_BytsPerSec,
                 FAT_Table.data() + byte / sizeof(uint32_t));
    }
    data_begin = BPB.BPB_RsvdSecCnt + BPB.BPB_NumFATs * BPB.BPB_FATSz32;
    count_of_cluster = (BPB.BPB_TotSec32 - data_begin) / BPB.BPB_SecPerClus;
}

} // namespace dev_io