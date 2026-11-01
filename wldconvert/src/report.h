// Conversion report: what was converted, changed or dropped, grouped by
// section, with identical messages counted instead of repeated.
#pragma once

#include <map>
#include <string>
#include <vector>

class Report {
public:
  void Add(const std::string &strSection, const std::string &strMessage, int ct = 1) {
    auto &sec = Section(strSection);
    for (auto &item : sec) {
      if (item.first == strMessage) { item.second += ct; return; }
    }
    sec.push_back({strMessage, ct});
  }

  std::string Text() const {
    std::string str;
    for (const auto &sec : m_asections) {
      str += "\n" + sec.first + ":\n";
      for (const auto &item : sec.second) {
        if (item.second > 1) str += "  " + std::to_string(item.second) + " x " + item.first + "\n";
        else str += "  " + item.first + "\n";
      }
    }
    return str;
  }

private:
  typedef std::vector<std::pair<std::string, int>> Items;
  Items &Section(const std::string &strSection) {
    for (auto &sec : m_asections) {
      if (sec.first == strSection) return sec.second;
    }
    m_asections.push_back({strSection, Items()});
    return m_asections.back().second;
  }
  std::vector<std::pair<std::string, Items>> m_asections;
};
