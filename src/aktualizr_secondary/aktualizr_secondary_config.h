#ifndef AKTUALIZR_SECONDARY_CONFIG_H_
#define AKTUALIZR_SECONDARY_CONFIG_H_

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include "config/config.h"
#include "crypto/keymanager.h"
#include "crypto/p11engine.h"
#include "logging/logging.h"
#include "package_manager/packagemanagerconfig.h"

// Try to keep the order of config options the same as in
// AktualizrSecondaryConfig::writeToFile() and
// AktualizrSecondaryConfig::updateFromPropertyTree().

struct AktualizrSecondaryNetConfig {
  in_port_t port{9030};
  bool discovery{true};
  in_port_t discovery_port{9031};

  void updateFromPropertyTree(const boost::property_tree::ptree& pt);
  void writeToStream(std::ostream& out_stream) const;
};

struct AktualizrSecondaryUptaneConfig {
  std::string ecu_serial;
  std::string ecu_hardware_id;
  CryptoSource key_source{kFile};
  KeyType key_type{kRSA2048};

  void updateFromPropertyTree(const boost::property_tree::ptree& pt);
  void writeToStream(std::ostream& out_stream) const;
};

class AktualizrSecondaryConfig : public BaseConfig {
 public:
  AktualizrSecondaryConfig() = default;
  AktualizrSecondaryConfig(const boost::program_options::variables_map& cmd);
  explicit AktualizrSecondaryConfig(const boost::filesystem::path& filename);

  KeyManagerConfig keymanagerConfig() const;

  void postUpdateValues();
  void writeToFile(const boost::filesystem::path& filename);

  // from primary config
  LoggerConfig logger;

  AktualizrSecondaryNetConfig network;
  AktualizrSecondaryUptaneConfig uptane;

  // from primary config
  P11Config p11;
  PackageConfig pacman;
  StorageConfig storage;

 private:
  void updateFromCommandLine(const boost::program_options::variables_map& cmd);
  void updateFromPropertyTree(const boost::property_tree::ptree& pt) override;
};

#endif  // AKTUALIZR_SECONDARY_CONFIG_H_