#ifndef FILE_H
#define FILE_H
#include <vector>
#include <string>
#include <atomic>
#include <map>
#include <set>
#include <memory>
#include <stdexcept>
#include <stdint.h>
#include <fileapi.h>

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

struct Entry_Info
{
    wchar_t name[256];
    char short_name[11];
    uint32_t first_clus;
    BY_HANDLE_FILE_INFORMATION info;
    bool isdir() const noexcept { return info.dwFileAttributes & 0x10; }
};

typedef std::vector<uint32_t> file_alloc;

typedef std::vector<Entry_Info> dir_info;

typedef std::vector<std::wstring> path;

struct file_ref
{
    uint64_t fd;
};

struct file_node
{
    file_node(file_node *parent) : parent(parent), ref_count(0) {}
    file_alloc alloc;
    Entry_Info info;
    std::vector<DIR_Entry> entries;
    file_node *parent;
    std::map<std::wstring, std::unique_ptr<file_node>> children;
    std::atomic<uint64_t> ref_count;
};

class file_error : public std::runtime_error
{
public:
    enum error_t
    {
        FILE_NOT_FOUND,
        FILE_NOT_DIR,
        FILE_ALREADY_EXISTS,
        INVALID_FILE_DISCRIPTOR,
    };
    file_error(error_t err) noexcept : std::runtime_error(err_msg(err)), err(err) {}
    error_t get_error_type() const noexcept { return err; }

private:
    static const char *err_msg(error_t err)
    {
        static const char *msg_table[] =
            {
                "file not found",
                "file not dir",
                "file already exists",
                "invalid file discriptor",
            };
        return msg_table[static_cast<int>(err)];
    }

    error_t err;
};

} // namespace fat32

#endif