#ifndef DEV_IO_H
#define DEV_IO_H
#include <vector>
#include <stdexcept>
#include <stdint.h>

namespace fat32
{

struct BPB_t
{
    uint8_t BS_jmpBoot[3];
    char BS_OEMName[8];
    uint16_t BPB_BytsPerSec;
    uint8_t BPB_SecPerClus;
    uint16_t BPB_RsvdSecCnt;
    uint8_t BPB_NumFATs;
    uint16_t BPB_RootEntCnt;
    uint16_t BPB_TotSec16;
    uint8_t BPB_Media;
    uint16_t BPB_FATSz16;
    uint16_t BPB_SecPerTrk;
    uint16_t BPB_NumHeads;
    uint32_t BPB_HiddSec;
    uint32_t BPB_TotSec32;
    uint32_t BPB_FATSz32;
    uint16_t BPB_ExtFlags;
    uint16_t BPB_FSVer;
    uint32_t BPB_RootClus;
    uint16_t BPB_FSInfo;
    uint16_t BPB_BkBootSec;
    uint8_t BPB_Reserved[12];
    uint8_t BS_DrvNum;
    uint8_t BS_Reserved1[1];
    uint8_t BS_BootSig;
    uint32_t BS_VolID;
    char BS_VolLab[11];
    char BS_FilSysType[8];
    char Empty[420];
    uint16_t Signature_word;
} __attribute__((packed));

struct FSInfo_t
{
    uint32_t FSI_LeadSig;
    uint8_t FSI_Reserved1[480];
    uint32_t FSI_StrucSig;
    uint32_t FSI_FreeCount;
    uint32_t FSI_Nxt_Free;
    uint8_t FSI_Reserved2[12];
    uint32_t FSI_TrailSig;
} __attribute__((packed));

struct DIR_Entry
{
    char DIR_Name[11];
    uint8_t DIR_Attr;
    uint8_t DIR_NTRes;
    uint8_t DIR_CrtTimeTenth;
    uint16_t DIR_CrtTime;
    uint16_t DIR_CrtDate;
    uint16_t DIR_LstAccDate;
    uint16_t DIR_FstClusHI;
    uint16_t DIR_WrtTime;
    uint16_t DIR_WrtDate;
    uint16_t DIR_FstClusLO;
    uint32_t DIR_FileSize;
} __attribute__((packed));

struct LDIR_Entry
{
    uint8_t LDIR_Ord;
    wchar_t LDIR_Name1[5];
    uint8_t LDIR_Attr;
    uint8_t LDIR_Type;
    uint8_t LDIR_Chksum;
    wchar_t LDIR_Name2[6];
    uint16_t LDIR_FstClusLO;
    wchar_t LDIR_Name3[2];
} __attribute__((packed));

int fat32_format(dev_t device);

} // namespace fat32

namespace dev_io
{

class disk_error : public std::runtime_error
{
public:
    enum error_t
    {
        DISK_NOT_FOUND,
        DISK_OPEN_ERROR,
        DISK_SEEK_ERROR,
        DISK_EXTEND_ERROR,
        DISK_GET_SIZE_ERROR,
        DISK_READ_ERROR,
        DISK_WRITE_ERROR,
        DISK_SIGNATURE_ERROR,
    };
    disk_error(error_t err) noexcept : std::runtime_error(err_msg(err)), err(err) {}
    error_t get_error_type() const noexcept { return err; }

private:
    static const char *err_msg(error_t err)
    {
        static const char *msg_table[] =
            {
                "disk not found",
                "disk open error",
                "disk seek error",
                "disk extend error",
                "disk get size error",
                "disk read error",
                "disk write error",
                "disk signature error",
            };
        return msg_table[static_cast<int>(err)];
    }

    error_t err;
};

class dev_t
{
public:
    dev_t(const char *dev_name, uint32_t tot_block, uint16_t block_size);
    dev_t(const char *dev_name);
    dev_t(dev_t &&dev) noexcept;
    dev_t(const dev_t &dev) = delete;
    dev_t &operator=(dev_t &&dev) noexcept;
    dev_t &operator=(const dev_t &dev) = delete;
    ~dev_t();

    operator bool() const noexcept;

    uint16_t get_block_size() const noexcept;
    uint32_t get_count_of_clus() const noexcept;
    uint32_t get_root_clus() const noexcept;
    uint8_t get_sec_per_clus() const noexcept;
    uint32_t get_total_block() const noexcept;
    uint32_t get_vol_id() const noexcept;
    uint32_t get_fat(uint32_t fat_no) const;
    void set_fat(uint32_t fat_no, uint32_t value);

    int32_t read_block(uint32_t block_no, void *buf) const;
    int32_t write_block(uint32_t block_no, const void *buf) const;
    int32_t read_clus(uint32_t clus_no, void *buf) const;
    int32_t write_clus(uint32_t clus_no, const void *buf) const;

private:
    static int32_t dev_read(void *handle, uint64_t offset, uint32_t size, void *buf);
    static int32_t dev_write(void *handle, uint64_t offset, uint32_t size, const void *buf);
    void format(uint32_t tot_block, uint16_t block_size);
    void clac_info();

    void *dev_img;
    fat32::BPB_t BPB;
    fat32::BPB_t BPB_Backup;
    fat32::FSInfo_t FSInfo;
    std::vector<uint32_t> FAT_Table;
    uint32_t data_begin;
    uint32_t count_of_cluster;
};

} // namespace dev_io

#endif