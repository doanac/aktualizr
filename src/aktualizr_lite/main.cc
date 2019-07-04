#include <unistd.h>
#include <iostream>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "config/config.h"
#include "helpers.h"
#include "package_manager/ostreemanager.h"
#include "primary/sotauptaneclient.h"

#include "utilities/aktualizr_version.h"

namespace bpo = boost::program_options;

#ifdef BUILD_DOCKERAPP
#define should_compare_docker_apps(config) \
  (config.pacman.type == PackageManager::kOstreeDockerApp && !config.pacman.docker_apps.empty())
static void add_apps_header(std::vector<std::string> &headers, PackageConfig &config) {
  if (config.type == PackageManager::kOstreeDockerApp) {
    headers.emplace_back("x-ats-dockerapps: " + boost::algorithm::join(config.docker_apps, ","));
  }
}
#else
#define should_compare_docker_apps(config) (false)
#define add_apps_header(headers, config) \
  {}
#endif

static Uptane::Target finalizeIfNeeded(INvStorage &storage, PackageConfig &config) {
  boost::optional<Uptane::Target> pending_version;
  storage.loadInstalledVersions("", nullptr, &pending_version);

  GObjectUniquePtr<OstreeSysroot> sysroot_smart = OstreeManager::LoadSysroot(config.sysroot);
  OstreeDeployment *booted_deployment = ostree_sysroot_get_booted_deployment(sysroot_smart.get());
  std::string current_hash = ostree_deployment_get_csum(booted_deployment);
  if (booted_deployment == nullptr) {
    throw std::runtime_error("Could not get booted deployment in " + config.sysroot.string());
  }

  if (!!pending_version) {
    const Uptane::Target &target = *pending_version;
    if (current_hash == target.sha256Hash()) {
      LOG_INFO << "Marking target install complete for: " << target;
      storage.saveInstalledVersion("", target, InstalledVersionUpdateMode::kCurrent);
      return target;
    }
  }

  std::vector<Uptane::Target> installed_versions;
  storage.loadPrimaryInstallationLog(&installed_versions, false);

  // Version should be in installed versions. Its possible that multiple
  // targets could have the same sha256Hash. In this case the safest assumption
  // is that the most recent (the reverse of the vector) target is what we
  // should return.
  std::vector<Uptane::Target>::reverse_iterator it;
  for (it = installed_versions.rbegin(); it != installed_versions.rend(); it++) {
    if (it->sha256Hash() == current_hash) {
      return *it;
    }
  }
  return Uptane::Target::Unknown();
}

static std::shared_ptr<SotaUptaneClient> liteClient(Config &config, std::shared_ptr<INvStorage> storage) {
  std::string pkey;
  if (storage == nullptr) {
    storage = INvStorage::newStorage(config.storage);
  }
  storage->importData(config.import);

  EcuSerials ecu_serials;
  if (!storage->loadEcuSerials(&ecu_serials)) {
    // Set a "random" serial so we don't get warning messages.
    std::string serial = config.provision.primary_ecu_serial;
    std::string hwid = config.provision.primary_ecu_hardware_id;
    if (hwid.empty()) {
      hwid = Utils::getHostname();
    }
    if (serial.empty()) {
      boost::uuids::uuid tmp = boost::uuids::random_generator()();
      serial = boost::uuids::to_string(tmp);
    }
    ecu_serials.emplace_back(Uptane::EcuSerial(serial), Uptane::HardwareIdentifier(hwid));
    storage->storeEcuSerials(ecu_serials);
  }

  std::vector<std::string> headers;
  GObjectUniquePtr<OstreeSysroot> sysroot_smart = OstreeManager::LoadSysroot(config.pacman.sysroot);
  OstreeDeployment *deployment = ostree_sysroot_get_booted_deployment(sysroot_smart.get());
  std::string header("x-ats-ostreehash: ");
  if (deployment != nullptr) {
    header += ostree_deployment_get_csum(deployment);
  } else {
    header += "?";
  }
  headers.push_back(header);
  add_apps_header(headers, config.pacman);

  Uptane::Target tgt = finalizeIfNeeded(*storage, config.pacman);
  headers.emplace_back("x-ats-target: " + tgt.filename());
  headers.emplace_back("x-ats-tags: " + boost::algorithm::join(config.pacman.tags, ","));

  auto http_client = std::make_shared<HttpClient>(&headers);
  auto bootloader = std::make_shared<Bootloader>(config.bootloader, *storage);
  auto report_queue = std::make_shared<ReportQueue>(config, http_client);

  KeyManager keys(storage, config.keymanagerConfig());
  keys.copyCertsToCurl(*http_client);

  auto client = std::make_shared<SotaUptaneClient>(config, storage, http_client, bootloader, report_queue);
  return client;
}

static void log_info_target(const std::string &prefix, const Config &config, const Uptane::Target &t) {
  auto name = t.filename();
  if (t.custom_version().length() > 0) {
    name = t.custom_version();
  }
  LOG_INFO << prefix + name << "\tsha256:" << t.sha256Hash();
  if (config.pacman.type == PackageManager::kOstreeDockerApp) {
    bool shown = false;
    auto apps = t.custom_data()["docker_apps"];
    for (Json::ValueIterator i = apps.begin(); i != apps.end(); ++i) {
      if (!shown) {
        shown = true;
        LOG_INFO << "\tDocker Apps:";
      }
      if ((*i).isObject() && (*i).isMember("filename")) {
        LOG_INFO << "\t\t" << i.key().asString() << " -> " << (*i)["filename"].asString();
      } else {
        LOG_ERROR << "\t\tInvalid custom data for docker-app: " << i.key().asString();
      }
    }
  }
}

static int status_main(Config &config, const bpo::variables_map &unused) {
  (void)unused;
  auto target = liteClient(config, nullptr)->getCurrent();

  if (target.MatchTarget(Uptane::Target::Unknown())) {
    LOG_INFO << "No active deployment found";
  } else {
    auto name = target.filename();
    if (target.custom_version().length() > 0) {
      name = target.custom_version();
    }
    log_info_target("Active image is: ", config, target);
  }
  return 0;
}

static int list_main(Config &config, const bpo::variables_map &unused) {
  (void)unused;
  auto client = liteClient(config, nullptr);
  Uptane::HardwareIdentifier hwid(config.provision.primary_ecu_hardware_id);

  LOG_INFO << "Refreshing target metadata";
  if (!client->updateImagesMeta()) {
    LOG_WARNING << "Unable to update latest metadata, using local copy";
    if (!client->checkImagesMetaOffline()) {
      LOG_ERROR << "Unable to use local copy of TUF data";
      return 1;
    }
  }

  LOG_INFO << "Updates available to " << hwid << ":";
  for (auto &t : client->allTargets()) {
    if (!target_has_tags(t, config.pacman.tags)) {
      continue;
    }
    for (auto const &it : t.hardwareIds()) {
      if (it == hwid) {
        log_info_target("", config, t);
        break;
      }
    }
  }
  return 0;
}

static std::unique_ptr<Uptane::Target> find_target(const std::shared_ptr<SotaUptaneClient> &client,
                                                   Uptane::HardwareIdentifier &hwid,
                                                   const std::vector<std::string> &tags, const std::string &version) {
  std::unique_ptr<Uptane::Target> rv;
  if (!client->updateImagesMeta()) {
    LOG_WARNING << "Unable to update latest metadata, using local copy";
    if (!client->checkImagesMetaOffline()) {
      LOG_ERROR << "Unable to use local copy of TUF data";
      throw std::runtime_error("Unable to find update");
    }
  }

  bool find_latest = (version == "latest");
  std::unique_ptr<Uptane::Target> latest = nullptr;
  for (auto &t : client->allTargets()) {
    if (!target_has_tags(t, tags)) {
      continue;
    }
    for (auto const &it : t.hardwareIds()) {
      if (it == hwid) {
        if (find_latest) {
          if (latest == nullptr || Version(latest->custom_version()) < Version(t.custom_version())) {
            latest = std_::make_unique<Uptane::Target>(t);
          }
        } else if (version == t.filename() || version == t.custom_version()) {
          return std_::make_unique<Uptane::Target>(t);
        }
      }
    }
  }
  if (find_latest) {
    return latest;
  }
  throw std::runtime_error("Unable to find update");
}

static int do_update(SotaUptaneClient &client, INvStorage &storage, Uptane::Target &target) {
  if (!client.downloadImage(target).first) {
    return 1;
  }

  if (client.VerifyTarget(target) != TargetStatus::kGood) {
    LOG_ERROR << "Downloaded target is invalid";
    return 1;
  }

  auto iresult = client.PackageInstall(target);
  if (iresult.result_code.num_code == data::ResultCode::Numeric::kNeedCompletion) {
    LOG_INFO << "Update complete. Please reboot the device to activate";
    storage.saveInstalledVersion("", target, InstalledVersionUpdateMode::kPending);
  } else if (iresult.result_code.num_code == data::ResultCode::Numeric::kOk) {
    storage.saveInstalledVersion("", target, InstalledVersionUpdateMode::kCurrent);
  } else {
    LOG_ERROR << "Unable to install update: " << iresult.description;
    return 1;
  }
  LOG_INFO << iresult.description;
  return 0;
}

static int update_main(Config &config, const bpo::variables_map &variables_map) {
  auto storage = INvStorage::newStorage(config.storage);
  auto client = liteClient(config, storage);
  Uptane::HardwareIdentifier hwid(config.provision.primary_ecu_hardware_id);

  std::string version("latest");
  if (variables_map.count("update-name") > 0) {
    version = variables_map["update-name"].as<std::string>();
  }
  LOG_INFO << "Finding " << version << " to update to...";
  auto target = find_target(client, hwid, config.pacman.tags, version);
  if (target == nullptr) {
    LOG_INFO << "Already up-to-date";
    return 0;
  }
  LOG_INFO << "Updating to: " << *target;
  return do_update(*client, *storage, *target);
}

static int daemon_main(Config &config, const bpo::variables_map &variables_map) {
  if (access(config.bootloader.reboot_command.c_str(), X_OK) != 0) {
    LOG_ERROR << "reboot command: " << config.bootloader.reboot_command << " is not executable";
    return 1;
  }

  auto storage = INvStorage::newStorage(config.storage);
  auto client = liteClient(config, storage);
  bool compareDockerApps = should_compare_docker_apps(config);
  Uptane::HardwareIdentifier hwid(config.provision.primary_ecu_hardware_id);

  auto current = client->getCurrent();
  LOG_INFO << "Active image is: " << current;

  unsigned int interval = 300;
  if (variables_map.count("interval") > 0) {
    interval = variables_map["interval"].as<unsigned int>();
  }

  while (true) {
    LOG_INFO << "Refreshing target metadata";
    if (!client->updateImagesMeta()) {
      LOG_WARNING << "Unable to update latest metadata, using local copy";
    }
    auto target = find_target(client, hwid, config.pacman.tags, "latest");
    if (target != nullptr && !targets_eq(*target, current, compareDockerApps)) {
      LOG_INFO << "Updating base image to: " << *target;
      if (do_update(*client, *storage, *target) == 0) {
        if (std::system(config.bootloader.reboot_command.c_str()) != 0) {
          LOG_ERROR << "Unable to reboot system";
          return 1;
        }
      }
    }
    sleep(interval);
  }
  return 0;
}

struct SubCommand {
  const char *name;
  int (*main)(Config &, const bpo::variables_map &);
};
static SubCommand commands[] = {
    {"status", status_main},
    {"list", list_main},
    {"update", update_main},
    {"daemon", daemon_main},
};

void check_info_options(const bpo::options_description &description, const bpo::variables_map &vm) {
  if (vm.count("help") != 0 || vm.count("command") == 0) {
    std::cout << description << '\n';
    exit(EXIT_SUCCESS);
  }
  if (vm.count("version") != 0) {
    std::cout << "Current aktualizr version is: " << aktualizr_version() << "\n";
    exit(EXIT_SUCCESS);
  }
}

bpo::variables_map parse_options(int argc, char *argv[]) {
  std::string subs("Command to execute: ");
  for (size_t i = 0; i < sizeof(commands) / sizeof(SubCommand); i++) {
    if (i != 0) {
      subs += ", ";
    }
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
      ("update-name", bpo::value<std::string>(), "optional name of the update when running \"update\". default=latest")
      ("interval", bpo::value<unsigned int>(), "optional interval in seconds to poll for update when in daemon mode. default=300")
      ("reboot-command", bpo::value<std::string>(), "reboot command to call after an update is applied from daemon mode")
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
  logger_init(isatty(1) == 1);
  logger_set_threshold(boost::log::trivial::info);

  bpo::variables_map commandline_map = parse_options(argc, argv);

  int r = EXIT_FAILURE;
  try {
    if (geteuid() != 0) {
      LOG_WARNING << "\033[31mRunning as non-root and may not work as expected!\033[0m\n";
    }

    Config config(commandline_map);
    config.storage.uptane_metadata_path = BasedPath(config.storage.path / "metadata");
    LOG_DEBUG << "Current directory: " << boost::filesystem::current_path().string();

    if (commandline_map.count("reboot-command") == 1) {
      config.bootloader.reboot_command = commandline_map["reboot-command"].as<std::string>();
    }

    std::string cmd = commandline_map["command"].as<std::string>();
    for (size_t i = 0; i < sizeof(commands) / sizeof(SubCommand); i++) {
      if (cmd == commands[i].name) {
        return commands[i].main(config, commandline_map);
      }
    }
    throw bpo::invalid_option_value(cmd);
    r = EXIT_SUCCESS;
  } catch (const std::exception &ex) {
    LOG_ERROR << ex.what();
  }
  return r;
}
