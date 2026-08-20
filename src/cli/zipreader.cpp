#include "zipreader.hpp"
#include <miniz.h>
#include <pugixml.hpp>
#include <fmt/format.h>
#include <cstring>
#include <fstream>

void set_error(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
}

class ZipReader {
 public:
  explicit ZipReader(std::string_view bytes) {
    std::memset(&zip_, 0, sizeof(zip_));
    ok_ = mz_zip_reader_init_mem(&zip_, bytes.data(), bytes.size(), 0) != MZ_FALSE;
  }
  ~ZipReader() {
    if (ok_) mz_zip_reader_end(&zip_);
  }
  ZipReader(const ZipReader&) = delete;
  ZipReader& operator=(const ZipReader&) = delete;

  [[nodiscard]] bool ok() const { return ok_; }

  [[nodiscard]] std::vector<std::string> names() {
    std::vector<std::string> out;
    if (!ok_) return out;

    const mz_uint count = mz_zip_reader_get_num_files(&zip_);
    for (mz_uint i = 0; i < count; ++i) {
      mz_zip_archive_file_stat stat;
      if (mz_zip_reader_file_stat(&zip_, i, &stat) == MZ_FALSE) continue;
      if (mz_zip_reader_is_file_a_directory(&zip_, i) != MZ_FALSE) continue;
      out.emplace_back(stat.m_filename);
    }
    return out;
  }

  [[nodiscard]] std::optional<std::string> read(std::string_view name) {
    if (!ok_) return std::nullopt;

    const std::string name_str(name);
    const int index = mz_zip_reader_locate_file(&zip_, name_str.c_str(), nullptr, 0);
    if (index < 0) return std::nullopt;

    std::size_t size = 0;
    void* raw = mz_zip_reader_extract_to_heap(&zip_, static_cast<mz_uint>(index), &size, 0);
    if (raw == nullptr) return std::nullopt;

    std::string content(static_cast<const char*>(raw), size);
    mz_free(raw);
    return content;
  }

 private:
  mz_zip_archive zip_{};
  bool ok_ = false;
};

bool ends_with_mscx(std::string_view name) {
  constexpr std::string_view kExt = ".mscx";
  return name.size() >= kExt.size() && name.substr(name.size() - kExt.size()) == kExt;
}

/// Finds the main document's name from META-INF/container.xml.
/// The container lists several rootfiles (style sheet, thumbnail, audio settings); the
/// score is the one ending in .mscx.
std::optional<std::string> mscx_name_from_container(const std::string& container_xml) {
  pugi::xml_document doc;
  if (!doc.load_buffer(container_xml.data(), container_xml.size())) return std::nullopt;

  for (const auto rootfile : doc.child("container").child("rootfiles").children("rootfile")) {
    const std::string full_path = rootfile.attribute("full-path").as_string();
    if (ends_with_mscx(full_path)) return full_path;
  }
  return std::nullopt;
}


std::vector<std::string> zip_entries(std::string_view bytes) {
  ZipReader zip(bytes);
  return zip.names();
}

std::optional<std::string> extract_mscx(std::string_view bytes, std::string* error) {
  if (bytes.empty()) {
    set_error(error, "empty score file");
    return std::nullopt;
  }

  ZipReader zip(bytes);
  if (!zip.ok()) {
    set_error(error, "could not read the .mscz zip archive");
    return std::nullopt;
  }

  if (const auto container = zip.read("META-INF/container.xml")) {
    if (const auto name = mscx_name_from_container(*container)) {
      if (auto content = zip.read(*name)) return content;
      set_error(error, fmt::format("container.xml names '{}' but it is not in the archive",
                                   *name));
      return std::nullopt;
    }
  }

  // No usable container manifest: fall back to the sole .mscx entry.
  for (const auto& name : zip.names()) {
    if (ends_with_mscx(name)) {
      if (auto content = zip.read(name)) return content;
    }
  }

  set_error(error, "no .mscx document found in the archive");
  return std::nullopt;
}

std::optional<std::string> extract_mscx_from_file(const std::filesystem::path& path,
                                                  std::string* error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    set_error(error, fmt::format("cannot open '{}'", path.string()));
    return std::nullopt;
  }

  std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return extract_mscx(bytes, error);
}