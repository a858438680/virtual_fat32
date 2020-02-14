#ifndef FILE_H
#define FILE_H
#include <vector>
#include <string>
#include <stdint.h>
#include <Fileapi.h>
#include "dev_io.h"

namespace fat32
{

struct Entry_Node
{
    wchar_t name[256];
    uint32_t first_clus;
    BY_HANDLE_FILE_INFORMATION info;
};

typedef std::vector<uint32_t> file_info;

typedef std::vector<Entry_Node> dir_info;

file_info open_file(const dev_io::dev_t &device, uint32_t first_clus);

dir_info open_dir(const dev_io::dev_t &device, uint32_t first_clus);

uint32_t get_first_clus_by_path(const dev_io::dev_t &device, const std::vector<std::wstring> &path, int *isdir = nullptr);

int get_info_by_path(const dev_io::dev_t &device, const std::vector<std::wstring> &path, LPBY_HANDLE_FILE_INFORMATION buffer);

} // namespace fat32

#endif