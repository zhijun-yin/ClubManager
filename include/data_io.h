#ifndef CLUB_DATA_IO_H
#define CLUB_DATA_IO_H
#include <string>
#include <vector>

namespace club_io {
// 单行 CSV：双引号字段允许逗号与转义双引号；不接受跨行字段。
bool parseCsvRow(const std::string& line, std::vector<std::string>& fields);
std::string csvCell(const std::string& value);
bool atomicWrite(const std::string& path, const std::string& content);
}
#endif
