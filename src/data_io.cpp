#include "data_io.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

bool club_io::parseCsvRow(const std::string& line, std::vector<std::string>& fields) {
    fields.clear();
    size_t i = 0;
    do {
        std::string cell;
        if (i < line.size() && line[i] == '"') {
            ++i;
            bool closed = false;
            while (i < line.size()) {
                char c = line[i++];
                if (c != '"') { cell += c; continue; }
                if (i < line.size() && line[i] == '"') { cell += '"'; ++i; }
                else { closed = true; break; }
            }
            if (!closed || (i < line.size() && line[i] != ',')) return false;
        } else {
            while (i < line.size() && line[i] != ',') {
                if (line[i] == '"') return false;
                cell += line[i++];
            }
        }
        fields.push_back(cell);
        if (i == line.size()) break;
        ++i;
        if (i == line.size()) { fields.push_back(""); break; }
    } while (true);
    return true;
}

std::string club_io::csvCell(const std::string& value) {
    // 表格软件不得把用户输入的姓名当作公式执行。
    std::string out = "\"";
    size_t first = value.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && std::string("=+-@").find(value[first]) != std::string::npos) out += '\'';
    for (char c : value) { if (c == '"') out += '"'; out += c; }
    return out + '"';
}

bool club_io::atomicWrite(const std::string& path, const std::string& content) {
    namespace fs = std::filesystem;
    fs::path temp;
    try {
        auto target = fs::u8path(path);
        temp = target;
        temp += ".tmp." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        bool ok = out.good();
        out.close();
        if (ok && !out.fail()) {
#ifdef _WIN32
            if (MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
#else
            std::error_code error;
            fs::rename(temp, target, error);
            if (!error) return true;
#endif
        }
    } catch (const std::exception&) {}
    std::error_code ignored;
    if (!temp.empty()) fs::remove(temp, ignored);
    return false;
}
