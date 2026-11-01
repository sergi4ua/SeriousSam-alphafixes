// Binary streams in the Serious Engine world format: little-endian values,
// four-character chunk IDs, length-prefixed strings and the filename
// dictionary (DPOS/DICT) that replaces file names with indices.
#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

typedef std::vector<uint8_t> Bytes;

struct FormatError : std::runtime_error {
  explicit FormatError(const std::string &str) : std::runtime_error(str) {}
};

class Reader {
public:
  explicit Reader(const Bytes &data) : m_pub(data.data()), m_ct(data.size()) {}

  size_t Pos() const { return m_pos; }
  void SetPos(size_t pos) { if (pos > m_ct) Fail("seek past end"); m_pos = pos; }
  size_t Size() const { return m_ct; }

  void Raw(void *pv, size_t ct) {
    Need(ct);
    memcpy(pv, m_pub + m_pos, ct);
    m_pos += ct;
  }
  Bytes Raw(size_t ct) {
    Need(ct);
    Bytes b(m_pub + m_pos, m_pub + m_pos + ct);
    m_pos += ct;
    return b;
  }
  void Skip(size_t ct) { Need(ct); m_pos += ct; }
  uint8_t  U8()  { uint8_t v;  Raw(&v, 1); return v; }
  uint16_t U16() { uint16_t v; Raw(&v, 2); return v; }
  uint32_t U32() { uint32_t v; Raw(&v, 4); return v; }
  int32_t  I32() { int32_t v;  Raw(&v, 4); return v; }
  float    F32() { float v;    Raw(&v, 4); return v; }

  // a count that must fit in the rest of the stream (guards against garbage)
  int32_t Count(size_t ctBytesEach) {
    int32_t ct = I32();
    if (ct < 0 || (ctBytesEach > 0 && (size_t)ct > (m_ct - m_pos) / ctBytesEach)) Fail("bad count " + std::to_string(ct));
    return ct;
  }

  std::string PeekID() const {
    if (m_pos + 4 > m_ct) return std::string();
    return std::string((const char *)m_pub + m_pos, 4);
  }
  bool IsID(const char *strID) const { return PeekID() == strID; }
  void ExpectID(const char *strID) {
    std::string str = PeekID();
    if (str != strID) Fail(std::string("expected chunk '") + strID + "', found '" + Printable(str) + "'");
    m_pos += 4;
  }
  // skip the chunk ID if it is there
  bool OptionalID(const char *strID) {
    if (!IsID(strID)) return false;
    m_pos += 4;
    return true;
  }

  std::string String() {
    int32_t ct = Count(1);
    std::string str((const char *)m_pub + m_pos, ct);
    m_pos += ct;
    return str;
  }

  // CTFileName: a dictionary index while a dictionary is active, else "DFNM" and a string
  std::string FileName() {
    if (m_pastrDictionary != nullptr) {
      int32_t i = I32();
      if (i < 0 || (size_t)i >= m_pastrDictionary->size()) Fail("bad dictionary index " + std::to_string(i));
      return (*m_pastrDictionary)[i];
    }
    ExpectID("DFNM");
    return String();
  }

  // DictionaryReadBegin_t: "DPOS" and the offset of the DICT chunk.  Returns
  // the position where the dictionary ends (where reading continues after
  // DictionaryReadEnd_t).
  size_t BeginDictionary(std::vector<std::string> &astr) {
    ExpectID("DPOS");
    size_t posDict = (size_t)U32();
    size_t posContinue = m_pos;
    SetPos(posDict);
    ExpectID("DICT");
    int32_t ct = Count(8);
    astr.clear();
    for (int32_t i = 0; i < ct; i++) {
      ExpectID("DFNM");
      astr.push_back(String());
    }
    ExpectID("DEND");
    size_t posEnd = m_pos;
    SetPos(posContinue);
    m_pastrDictionary = &astr;
    return posEnd;
  }
  void EndDictionary(size_t posEnd) {
    m_pastrDictionary = nullptr;
    SetPos(posEnd);
  }

  [[noreturn]] void Fail(const std::string &str) const {
    throw FormatError(str + " (at offset " + std::to_string(m_pos) + ")");
  }

  static std::string Printable(const std::string &str) {
    std::string out;
    for (char ch : str) out += (ch >= 32 && ch < 127) ? ch : '?';
    return out;
  }

private:
  void Need(size_t ct) const { if (ct > m_ct - m_pos) Fail("unexpected end of file"); }

  const uint8_t *m_pub;
  size_t m_ct;
  size_t m_pos = 0;
  std::vector<std::string> *m_pastrDictionary = nullptr;
};

class Writer {
public:
  const Bytes &Data() const { return m_ab; }
  size_t Pos() const { return m_ab.size(); }

  void Raw(const void *pv, size_t ct) {
    const uint8_t *pub = (const uint8_t *)pv;
    m_ab.insert(m_ab.end(), pub, pub + ct);
  }
  void Raw(const Bytes &ab) { m_ab.insert(m_ab.end(), ab.begin(), ab.end()); }
  void U8(uint8_t v)   { Raw(&v, 1); }
  void U32(uint32_t v) { Raw(&v, 4); }
  void I32(int32_t v)  { Raw(&v, 4); }
  void F32(float v)    { Raw(&v, 4); }
  void ID(const char *strID) { Raw(strID, 4); }
  void String(const std::string &str) { I32((int32_t)str.size()); Raw(str.data(), str.size()); }
  void PatchU32(size_t pos, uint32_t v) { memcpy(&m_ab[pos], &v, 4); }

  void FileName(const std::string &str) {
    if (!m_bDictionary) {
      ID("DFNM");
      String(str);
      return;
    }
    auto it = m_mapDictionary.find(str);
    int32_t i;
    if (it == m_mapDictionary.end()) {
      i = (int32_t)m_astrDictionary.size();
      m_astrDictionary.push_back(str);
      m_mapDictionary[str] = i;
    } else {
      i = it->second;
    }
    I32(i);
  }

  // DictionaryWriteBegin_t / DictionaryWriteEnd_t
  void BeginDictionary() {
    ID("DPOS");
    m_posDictionaryPos = Pos();
    U32(0);
    m_bDictionary = true;
    m_astrDictionary.clear();
    m_mapDictionary.clear();
  }
  void EndDictionary() {
    m_bDictionary = false;
    PatchU32(m_posDictionaryPos, (uint32_t)Pos());
    ID("DICT");
    I32((int32_t)m_astrDictionary.size());
    for (const std::string &str : m_astrDictionary) {
      ID("DFNM");
      String(str);
    }
    ID("DEND");
  }

private:
  Bytes m_ab;
  bool m_bDictionary = false;
  size_t m_posDictionaryPos = 0;
  std::vector<std::string> m_astrDictionary;
  std::map<std::string, int32_t> m_mapDictionary;
};
