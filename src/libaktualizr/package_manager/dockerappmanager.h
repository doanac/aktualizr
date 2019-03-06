#ifndef DOCKERAPPMGR_H_
#define DOCKERAPPMGR_H_

#include "ostreemanager.h"

class DockerAppManager : public OstreeManager {
 public:
  DockerAppManager(PackageConfig pconfig, std::shared_ptr<INvStorage> storage, std::shared_ptr<Bootloader> bootloader)
      : OstreeManager(pconfig, storage, bootloader) {}
  std::string name() const override { return "ostree+docker-app"; }
  data::InstallationResult install(const Uptane::Target &target) const override;
  data::InstallationResult finalizeInstall(const Uptane::Target &target) const override;

  static data::InstallationResult pull(const boost::filesystem::path &sysroot_path, const std::string &ostree_server,
                                       const KeyManager &keys, const Uptane::Target &target,
                                       const std::function<void()> &pause_cb = {},
                                       OstreeProgressCb progress_cb = nullptr);
};
#endif  // DOCKERAPPMGR_H_
