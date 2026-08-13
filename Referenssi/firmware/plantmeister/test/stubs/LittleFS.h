#pragma once
// Minimal LittleFS stub for native unit tests.
//
// In production ArduinoJson::deserializeJson(doc, File&) reads from the
// file via Stream-like read()/available() methods. To keep the surface
// small, this stub exposes File as a thin wrapper around std::string and
// supports the operations plant_database.h actually calls:
//
//   LittleFS.exists(path)
//   LittleFS.open(path, "r" | "w")
//   File::operator bool()
//   File::size()
//   File::read(buf, n) / readBytes(buf, n) / available() / peek() / read()
//   File::write(buf, n) / print(char) / print(const char*)
//   File::close()
//
// Tests inject content with `littlefs_inject(path, "{...}")` and read
// back with `littlefs_read(path)` after the code under test has called
// LittleFS.open(path, "w").

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

class FakeFile {
public:
  FakeFile() : _valid(false), _writable(false), _pos(0), _backing(nullptr) {}
  // Read-mode constructor: owns a snapshot of the contents.
  FakeFile(const std::string& snapshot)
      : _valid(true), _writable(false), _pos(0), _data(snapshot), _backing(nullptr) {}
  // Write-mode constructor: writes back to a backing store on close.
  FakeFile(std::string* backing)
      : _valid(true), _writable(true), _pos(0), _backing(backing) {
    if (_backing) _backing->clear();
  }

  explicit operator bool() const { return _valid; }
  size_t size() const { return _data.size(); }

  // ── Stream-like read interface used by ArduinoJson ────────────────
  int available() {
    if (!_valid) return 0;
    return (int)(_data.size() - _pos);
  }
  int read() {
    if (!_valid || _pos >= _data.size()) return -1;
    return (unsigned char)_data[_pos++];
  }
  int peek() {
    if (!_valid || _pos >= _data.size()) return -1;
    return (unsigned char)_data[_pos];
  }
  size_t read(uint8_t* buf, size_t n) {
    if (!_valid || !buf) return 0;
    size_t avail = _data.size() - _pos;
    size_t toRead = (n < avail) ? n : avail;
    memcpy(buf, _data.data() + _pos, toRead);
    _pos += toRead;
    return toRead;
  }
  size_t readBytes(uint8_t* buf, size_t n) { return read(buf, n); }
  size_t readBytes(char* buf, size_t n) {
    return read(reinterpret_cast<uint8_t*>(buf), n);
  }

  // ── Write interface used by ArduinoJson serializeJson(doc, File&) ──
  size_t write(uint8_t b) {
    if (!_valid || !_writable) return 0;
    if (_backing) _backing->push_back((char)b);
    return 1;
  }
  size_t write(const uint8_t* buf, size_t n) {
    if (!_valid || !_writable || !buf) return 0;
    if (_backing) _backing->append(reinterpret_cast<const char*>(buf), n);
    return n;
  }
  size_t print(char c)             { return write((uint8_t)c); }
  size_t print(const char* s) {
    if (!s) return 0;
    size_t n = strlen(s);
    return write(reinterpret_cast<const uint8_t*>(s), n);
  }

  void close() {
    // No-op for reads. For writes, content was streamed to _backing as we went.
    _valid = false;
  }

private:
  bool _valid;
  bool _writable;
  size_t _pos;
  std::string _data;      // read snapshot
  std::string* _backing;  // write target (lives in LittleFSStub)
};

class LittleFSStub {
public:
  bool begin() { return true; }
  bool format() { _files.clear(); return true; }

  bool exists(const char* path) {
    return _files.find(path ? path : "") != _files.end();
  }

  FakeFile open(const char* path, const char* mode) {
    if (!path || !mode) return FakeFile();
    if (mode[0] == 'r') {
      auto it = _files.find(path);
      if (it == _files.end()) return FakeFile();
      return FakeFile(it->second);
    }
    if (mode[0] == 'w') {
      _files[path] = "";
      return FakeFile(&_files[path]);
    }
    return FakeFile();
  }

  bool remove(const char* path) {
    return _files.erase(path ? path : "") > 0;
  }

  // ── Test helpers ───────────────────────────────────────────────────
  void littlefs_clear()                          { _files.clear(); }
  void littlefs_inject(const char* path, const std::string& content) {
    _files[path] = content;
  }
  std::string littlefs_read(const char* path) {
    auto it = _files.find(path);
    return (it == _files.end()) ? std::string() : it->second;
  }
  bool littlefs_has(const char* path) { return exists(path); }
  size_t littlefs_fileCount() { return _files.size(); }

private:
  std::map<std::string, std::string> _files;
};

// Single global instance matching the real Arduino API.
static LittleFSStub LittleFS;

// File type alias to match Arduino: code uses `File f = LittleFS.open(...)`.
using File = FakeFile;
