#include "dockerappmanager.h"

data::InstallationResult DockerAppManager::pull(const boost::filesystem::path &sysroot_path,
                                                const std::string &ostree_server, const KeyManager &keys,
                                                const Uptane::Target &target, const std::function<void()> &pause_cb,
                                                OstreeProgressCb progress_cb) {
  auto res = OstreeManager::pull(sysroot_path, ostree_server, keys, target, pause_cb, progress_cb);

  LOG_INFO << "Looking for DockerApps to install/update";
  for (Json::ValueIterator i = target.custom_data().begin(); i != target.custom_data().end(); ++i) {
    // TODO - this operation now needs to be a class method so it can access the configuration
    // So we probably need to clean up the fetcher/packagemanager hack first
    //for (auto app : config.docker_apps) {
      //if (i.key().asString() == app) {
        LOG_INFO << "TODO pull down app " << i.key().asString() << " -> " << (*i).asString();
      //}
    //}
  }

  return res;
}

data::InstallationResult DockerAppManager::install(const Uptane::Target &target) const {
  auto res = OstreeManager::install(target);
  for (Json::ValueIterator i = target.custom_data().begin(); i != target.custom_data().end(); ++i) {
    for (auto app : config.docker_apps) {
      if (i.key().asString() == app) {
        LOG_INFO << "TODO install/update app " << i.key().asString() << " -> " << (*i).asString();
      }
    }
  }
  return res;
}

data::InstallationResult DockerAppManager::finalizeInstall(const Uptane::Target &target) const {
  auto res = OstreeManager::finalizeInstall(target);
  LOG_INFO << "TODO persist state of docker-app and target";
  return res;
}
