#ifndef DEV_IO_H
#define DEV_IO_H
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
    dev_t(dev_t &&dev) noexcept;
    dev_t(const dev_t &dev) = delete;
    dev_t &operator=(dev_t &&dev) noexcept;
    dev_t &operator=(const dev_t &dev) = delete;
    ~dev_t();

    operator bool() const noexcept;

    uint16_t get_block_size() const noexcept;
    uint32_t get_count_of_clus() const noexcept;
    uint32_t get_clus_size() const noexcept;
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

    void get_info(const fat32::path &path, void *buffer);
    fat32::file open_file(const fat32::path &path);
    fat32::dir_info open_dir(const fat32::path &path);

private:
    static int32_t dev_read(void *handle, uint64_t offset, uint32_t size, void *buf);
    static int32_t dev_write(void *handle, uint64_t offset, uint32_t size, const void *buf);
    void format(uint32_t tot_block, uint16_t block_size);
    void clac_info();

    fat32::file_alloc open_file(uint32_t clus_no);
    fat32::dir_info open_dir(uint32_t clus_no);
    uint32_t get_first_clus(const fat32::path &path, int *isdir = nullptr);

    void *dev_img;
    fat32::BPB_t BPB;
    fat32::BPB_t BPB_Backup;
    fat32::FSInfo_t FSInfo;
    std::vector<uint32_t> FAT_Table;
    uint32_t data_begin;
    uint32_t count_of_cluster;
    uint32_t clus_size;
};

} // namespace dev_io

#endif