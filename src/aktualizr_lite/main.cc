#include <iostream>

#include <openssl/ssl.h>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include "config/config.h"
#include "package_manager/ostreemanager.h"
#include "primary/sotauptaneclient.h"

namespace bpo = boost::program_options;

static int status_main(Config &config, bpo::variables_map) {
  GObjectUniquePtr<OstreeSysroot> sysroot_smart = OstreeManager::LoadSysroot(config.pacman.sysroot);
  OstreeDeployment *deployment = ostree_sysroot_get_booted_deployment(sysroot_smart.get());
  if (deployment == nullptr) {
    LOG_INFO << "No active deployment found";
  } else {
    LOG_INFO << "Active image is: " << ostree_deployment_get_csum(deployment);
  }
  return 0;
}

struct SubCommand {
  const char *name;
  int (*main)(Config&, bpo::variables_map);
};
static SubCommand commands[] = {
  {"status", status_main},
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

