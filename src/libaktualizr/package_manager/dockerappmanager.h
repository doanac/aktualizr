#ifndef DOCKERAPPMGR_H_
#define DOCKERAPPMGR_H_

#include "ostreemanager.h"

class DockerAppManager : public OstreeManager {
 public:
  DockerAppManager(PackageConfig pconfig, std::shared_ptr<INvStorage> storage, std::shared_ptr<Bootloader> bootloader)
      : OstreeManager(pconfig, storage, bootloader) {}
  std::string name() const override { return "ostree+docker-app"; }
};

#endif  // DOCKERAPPMGR_H_
