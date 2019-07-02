#include <string>

#include <string.h>

#include "uptane/tuf.h"

struct Version {
  std::string raw_ver;
  Version(std::string version) : raw_ver(std::move(version)) {}

  bool operator<(const Version& other) { return strverscmp(raw_ver.c_str(), other.raw_ver.c_str()) < 0; }
};

bool target_has_tags(const Uptane::Target& t, const std::vector<std::string>& config_tags) {
  if (!config_tags.empty()) {
    auto tags = t.custom_data()["tags"];
    for (Json::ValueIterator i = tags.begin(); i != tags.end(); ++i) {
      auto tag = (*i).asString();
      if (std::find(config_tags.begin(), config_tags.end(), tag) != config_tags.end()) {
        return true;
      }
    }
    return false;
  }
  return true;
}

bool targets_eq(const Uptane::Target& t1, const Uptane::Target& t2, bool compareDockerApps) {
  // target equality check looks at hashes
  if (t1 == t2) {
    if (compareDockerApps) {
      auto t1_apps = t1.custom_data()["docker_apps"];
      auto t2_apps = t2.custom_data()["docker_apps"];
      for (Json::ValueIterator i = t1_apps.begin(); i != t1_apps.end(); ++i) {
        auto app = i.key().asString();
        if (!t2_apps.isMember(app)) {
          return false;  // an app has been removed
        }
        if ((*i)["filename"].asString() != t2_apps[app]["filename"].asString()) {
          return false;  // tuf target filename changed
        }
        t2_apps.removeMember(app);
      }
      if (t2_apps.size() > 0) {
        return false;  // an app has been added
      }
    }
    return true;  // docker apps are the same, or there are none
  }
  return false;
}
