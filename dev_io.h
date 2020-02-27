#ifndef DEV_IO_H
#define DEV_IO_H
#include <mutex>
#include <map>
#include <stdexcept>
#include <stdint.h>
#include "fat32.h"

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
    dev_t(dev_t &&dev) = delete;
    dev_t(const dev_t &dev) = delete;
    dev_t &operator=(dev_t &&dev) = delete;
    dev_t &operator=(const dev_t &dev) = delete;
    ~dev_t();

    operator bool() const noexcept;

    uint64_t open(const fat32::path &path, uint32_t create_disposition, uint32_t file_attr, bool &exist, bool &isdir);
    void unlink(uint64_t fd);
    bool rename(uint64_t fd, const fat32::path &newpath, bool replace);
    fat32::dir_info opendir(const fat32::path &path);
    void close(uint64_t fd);
    uint32_t read(uint64_t fd, int64_t offset, uint32_t len, void *buf);
    uint32_t write(uint64_t fd, int64_t offset, uint32_t len, const void *buf);
    void fstat(uint64_t fd, LPBY_HANDLE_FILE_INFORMATION statbuf);
    void setattr(uint64_t fd, uint32_t attr);
    void settime(uint64_t fd, CONST FILETIME *CreationTime, CONST FILETIME *LastAccessTime, CONST FILETIME *LastWriteTime);
    void setend(uint64_t fd, int64_t offset);
    void setalloc(uint64_t fd, int64_t alloc);

    fat32::dir_info opendir(uint64_t fd);
    void get_disk_info(uint64_t *free_avilable, uint64_t *tot_size, uint64_t *tot_free);
    void flush();
    void clear();

    uint32_t get_root_clus() const noexcept;
    uint32_t get_vol_id() const noexcept;
    uint32_t get_fat(uint32_t fat_no) const;
    void set_fat(uint32_t fat_no, uint32_t value);

private:
    static int32_t dev_read(void *handle, uint64_t offset, uint32_t size, void *buf);
    static int32_t dev_write(void *handle, uint64_t offset, uint32_t size, const void *buf);
    int32_t read_block(uint32_t block_no, void *buf) const;
    int32_t write_block(uint32_t block_no, const void *buf) const;
    int32_t read_clus(uint32_t clus_no, void *buf) const;
    int32_t write_clus(uint32_t clus_no, const void *buf) const;
    void format(uint32_t tot_block, uint16_t block_size);
    void clac_info();

    std::unique_ptr<fat32::file_node> open_file(fat32::file_node *parent, fat32::Entry_Info *pinfo);
    void save(fat32::file_node *node);
    void clear_node(fat32::file_node *node);
    uint32_t next_free();
    void free_clus(uint32_t clus_no);
    void extend(fat32::file_node *node, uint32_t clus_count);
    void shrink(fat32::file_node *node, uint32_t clus_count);
    void add_entry(fat32::file_node *node, fat32::Entry_Info *pinfo, int have_long, bool replace);
    void remove_entry(fat32::file_node *node, std::wstring_view name);
    void gen_short(std::wstring_view name, fat32::file_node *node, char *short_name);
    int32_t DirEntry2EntryInfo(const fat32::DIR_Entry *pdir, fat32::Entry_Info *pinfo);

    void *dev_img;
    fat32::BPB_t BPB;
    fat32::FSInfo_t FSInfo;
    std::vector<uint32_t> FAT_Table;
    std::set<uint64_t> open_file_table;
    std::unique_ptr<fat32::file_node> root;
    uint32_t tot_block;
    uint16_t block_size;
    uint8_t sec_per_clus;
    uint32_t clus_size;
    uint32_t data_begin;
    uint32_t count_of_cluster;
    fat32::Entry_Info root_info;
    bool cleared;
};

} // namespace dev_io

#endif