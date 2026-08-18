#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

[[nodiscard]] std::optional<std::string> extract_mscx(std::string_view bytes, std::string* error = nullptr);

[[nodiscard]] std::optional<std::string> extract_mscx_from_file(const std::filesystem::path& path, std::string* error = nullptr);

[[nodiscard]] std::vector<std::string> zip_entries(std::string_view bytes);
