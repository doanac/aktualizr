#include <iostream>

#include <openssl/ssl.h>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include "config/config.h"
#include "package_manager/ostreemanager.h"
#include "primary/sotauptaneclient.h"

namespace bpo = boost::program_options;

static int initialize_main(Config &config, bpo::variables_map) {
  if (config.provision.primary_ecu_hardware_id.length() == 0) {
    LOG_INFO << "Probing TUF targets to find the primary ECU's hardware-id";
    auto storage = INvStorage::newStorage(config.storage);
    auto client = SotaUptaneClient::newDefaultClient(config, storage);

    GObjectUniquePtr<OstreeSysroot> sysroot_smart = OstreeManager::LoadSysroot(config.pacman.sysroot);
    OstreeDeployment *deployment = ostree_sysroot_get_booted_deployment(sysroot_smart.get());
    auto active = ostree_deployment_get_csum(deployment);

    auto targets = client->GetRepoTargets();
    for (auto i = targets.rbegin(); i != targets.rend(); ++i) {
      if ((*i).sha256Hash() == active) {
        config.provision.primary_ecu_hardware_id = (*i).hardwareIds().front().ToString();
        LOG_INFO << "Probed primary ECU hardware-id: " << config.provision.primary_ecu_hardware_id;
        break;
      }
    }
    if (config.provision.primary_ecu_hardware_id.length() == 0) {
      throw std::runtime_error("Unable to find current active image in targets.json");
    }
  }
  boost::filesystem::path p = config.storage.path / "lite.toml";
  std::ofstream os(p.c_str(), std::ofstream::out);
  config.writeToStream(os);
  os.close();
  LOG_INFO << "Configuration written to: " << p;
  return 0;
}

static int status_main(Config &config, bpo::variables_map) {
  GObjectUniquePtr<OstreeSysroot> sysroot_smart = OstreeManager::LoadSysroot(config.pacman.sysroot);
  OstreeDeployment *deployment = ostree_sysroot_get_booted_deployment(sysroot_smart.get());
  LOG_INFO << "Active image is: " << ostree_deployment_get_csum(deployment);
  return 0;
}

static int list_main(Config &config, bpo::variables_map) {
  auto storage = INvStorage::newStorage(config.storage);
  auto client = SotaUptaneClient::newDefaultClient(config, storage);
  auto targets = client->GetRepoTargets();
  Uptane::HardwareIdentifier hwid(config.provision.primary_ecu_hardware_id);

  LOG_INFO << "Updates for available to " << hwid << ":";
  for (auto i = targets.rbegin(); i != targets.rend(); ++i) {
    for (auto const& it : (*i).hardwareIds()) {
      if (it == hwid) {
        LOG_INFO << (*i).custom_version() << "\tsha256:" << (*i).sha256Hash();
        break;
      }
    }
  }
  return 0;
}

static Uptane::Target latest_version(std::shared_ptr<SotaUptaneClient> client, Uptane::HardwareIdentifier &hwid) {
  auto targets = client->GetRepoTargets();
  for (auto i = targets.rbegin(); i != targets.rend(); ++i) {
    for (auto const& it : (*i).hardwareIds()) {
      if (it == hwid) {
        return *i;
      }
    }
  }
  throw std::runtime_error("Unable to find update");
}

static int update_main(Config &config, bpo::variables_map) {
  auto storage = INvStorage::newStorage(config.storage);
  auto client = SotaUptaneClient::newDefaultClient(config, storage);
  Uptane::HardwareIdentifier hwid(config.provision.primary_ecu_hardware_id);

  LOG_INFO << "Finding latest version to update to...";
  auto target = latest_version(client, hwid);
  LOG_INFO << "Updating to: " << target;

  std::vector<Uptane::Target> targets{target};
  auto result = client->downloadImages(targets);
  if (result.status != result::DownloadStatus::kSuccess && result.status != result::DownloadStatus::kNothingToDownload) {
    LOG_ERROR << "Unable to download update: " + result.message;
    return 1;
  }
  auto outcome = client->PackageInstall(target);
  if (outcome.first != data::UpdateResultCode::kOk && outcome.first != data::UpdateResultCode::kNeedCompletion) {
    LOG_ERROR << "Unable to install update: " + outcome.second;
    return 1;
  }
  LOG_INFO << outcome.second;
  return 0;
}

struct SubCommand {
  const char *name;
  int (*main)(Config&, bpo::variables_map);
};
static SubCommand commands[] = {
  {"initialize", initialize_main},
  {"status", status_main},
  {"list", list_main},
  {"update", update_main},
};


void check_info_options(const bpo::options_description &description, const bpo::variables_map &vm) {
  if (vm.count("help") != 0 || vm.count("command") == 0) {
    std::cout << description << '\n';
    exit(EXIT_SUCCESS);
  }
  if (vm.count("version") != 0) {
    std::cout << "Current aktualizr version is: " << AKTUALIZR_VERSION << "\n";
    exit(EXIT_SUCCESS);
  }
}

bpo::variables_map parse_options(int argc, char *argv[]) {
  std::string subs("Command to execute: ");
  for(size_t i=0; i<sizeof(commands)/sizeof(SubCommand); i++) {
    if(i) subs += ", ";
    subs += commands[i].name;
  }
  bpo::options_description description("aktualizr-lite command line options");
  // clang-format off
  // Try to keep these options in the same order as Config::updateFromCommandLine().
  // The first three are commandline only.
  description.add_options()
      ("help,h", "print usage")
      ("version,v", "Current aktualizr version")
      ("config,c", bpo::value<std::vector<boost::filesystem::path> >()->composing(), "configuration file or directory")
      ("loglevel", bpo::value<int>(), "set log level 0-5 (trace, debug, info, warning, error, fatal)")
      ("repo-server", bpo::value<std::string>(), "url of the uptane repo repository")
      ("ostree-server", bpo::value<std::string>(), "url of the ostree repository")
      ("primary-ecu-hardware-id", bpo::value<std::string>(), "hardware ID of primary ecu")
      ("command", bpo::value<std::string>(), subs.c_str());
  // clang-format on

  // consider the first positional argument as the aktualizr run mode
  bpo::positional_options_description pos;
  pos.add("command", 1);

  bpo::variables_map vm;
  std::vector<std::string> unregistered_options;
  try {
    bpo::basic_parsed_options<char> parsed_options =
        bpo::command_line_parser(argc, argv).options(description).positional(pos).allow_unregistered().run();
    bpo::store(parsed_options, vm);
    check_info_options(description, vm);
    bpo::notify(vm);
    unregistered_options = bpo::collect_unrecognized(parsed_options.options, bpo::exclude_positional);
    if (vm.count("help") == 0 && !unregistered_options.empty()) {
      std::cout << description << "\n";
      exit(EXIT_FAILURE);
    }
  } catch (const bpo::required_option &ex) {
    // print the error and append the default commandline option description
    std::cout << ex.what() << std::endl << description;
    exit(EXIT_FAILURE);
  } catch (const bpo::error &ex) {
    check_info_options(description, vm);

    // log boost error
    LOG_ERROR << "boost command line option error: " << ex.what();

    // print the error message to the standard output too, as the user provided
    // a non-supported commandline option
    std::cout << ex.what() << '\n';

    // set the returnValue, thereby ctest will recognize
    // that something went wrong
    exit(EXIT_FAILURE);
  }

  return vm;
}

int main(int argc, char *argv[]) {
  logger_init();
  logger_set_threshold(boost::log::trivial::info);

  bpo::variables_map commandline_map = parse_options(argc, argv);

  int r = -1;
  try {
    if (geteuid() != 0) {
      LOG_WARNING << "\033[31mRunning as non-root and may not work as expected!\033[0m\n";
    }

    Config config(commandline_map);
    config.storage.uptane_metadata_path = BasedPath(config.storage.path / "metadata");
    if (config.logger.loglevel <= boost::log::trivial::debug) {
      SSL_load_error_strings();
    }
    LOG_DEBUG << "Current directory: " << boost::filesystem::current_path().string();

    std::string cmd = commandline_map["command"].as<std::string>();
    for(size_t i=0; i<sizeof(commands)/sizeof(SubCommand); i++) {
      if (cmd == commands[i].name) {
        return commands[i].main(config, commandline_map);
      }
    }
    throw bpo::invalid_option_value(cmd);
  } catch (const std::exception &ex) {
    LOG_ERROR << ex.what();
    r = -1;
  }
  return r;
}

