#include <ctime>
#include <iomanip>
#include <iostream>

#include <curl/curl.h>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include "json/json.h"

#include "accumulator.h"
#include "authenticate.h"
#include "logging/logging.h"
#include "treehub_server.h"
#include "utilities/types.h"
#include "utilities/utils.h"

namespace po = boost::program_options;
using std::string;

// helper function to download data to a string
static size_t writeString(void *contents, size_t size, size_t nmemb, void *userp) {
  assert(userp);
  // append the writeback data to the provided string
  (static_cast<std::string *>(userp))->append(static_cast<char *>(contents), size * nmemb);

  // return size of written data
  return size * nmemb;
}

int main(int argc, char **argv) {
  logger_init();

  string ref = "uninitialized";

  boost::filesystem::path credentials_path;
  string home_path = string(getenv("HOME"));
  string cacerts;

  int verbosity;
  po::options_description desc("garage-check command line options");
  // clang-format off
  desc.add_options()
    ("help", "print usage")
    ("verbose,v",accumulator<int>(&verbosity), "verbose logging (use twice for more information)")
    ("quiet,q", "quiet mode")
    ("ref,r", po::value<string>(&ref)->required(), "refhash to check")
    ("credentials,j", po::value<boost::filesystem::path>(&credentials_path)->required(), "credentials (json or zip containing json)")
    ("cacert", po::value<string>(&cacerts), "override path to CA root certificates, in the same format as curl --cacert");
  // clang-format on

  po::variables_map vm;

  try {
    po::store(po::parse_command_line(argc, reinterpret_cast<const char *const *>(argv), desc), vm);

    if (vm.count("help") != 0u) {
      LOG_INFO << desc;
      return EXIT_SUCCESS;
    }

    po::notify(vm);
  } catch (const po::error &o) {
    LOG_INFO << o.what();
    LOG_INFO << desc;
    return EXIT_FAILURE;
  }

  // Configure logging
  if (verbosity == 0) {
    // 'verbose' trumps 'quiet'
    if (static_cast<int>(vm.count("quiet")) != 0) {
      logger_set_threshold(boost::log::trivial::warning);
    } else {
      logger_set_threshold(boost::log::trivial::info);
    }
  } else if (verbosity == 1) {
    logger_set_threshold(boost::log::trivial::debug);
    LOG_DEBUG << "Debug level debugging enabled";
  } else if (verbosity > 1) {
    logger_set_threshold(boost::log::trivial::trace);
    LOG_TRACE << "Trace level debugging enabled";
  } else {
    assert(0);
  }

  TreehubServer treehub;
  if (cacerts != "") {
    if (boost::filesystem::exists(cacerts)) {
      treehub.ca_certs(cacerts);
    } else {
      LOG_FATAL << "--cacert path " << cacerts << " does not exist";
      return EXIT_FAILURE;
    }
  }

  if (authenticate(cacerts, ServerCredentials(credentials_path), treehub) != EXIT_SUCCESS) {
    LOG_FATAL << "Authentication failed";
    return EXIT_FAILURE;
  }

  // check if the ref is present on treehub
  CurlEasyWrapper curl;
  if (curl.get() == nullptr) {
    LOG_FATAL << "Error initializing curl";
    return EXIT_FAILURE;
  }
  curlEasySetoptWrapper(curl.get(), CURLOPT_VERBOSE, get_curlopt_verbose());
  curlEasySetoptWrapper(curl.get(), CURLOPT_NOBODY, 1L);  // HEAD

  treehub.InjectIntoCurl("objects/" + ref.substr(0, 2) + "/" + ref.substr(2) + ".commit", curl.get());

  CURLcode result = curl_easy_perform(curl.get());
  if (result != CURLE_OK) {
    LOG_FATAL << "Error connecting to treehub: " << result << ": " << curl_easy_strerror(result);
    return EXIT_FAILURE;
  }

  long http_code;  // NOLINT
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code == 404) {
    LOG_FATAL << "OSTree commit " << ref << " is missing in treehub";
    return EXIT_FAILURE;
  }
  if (http_code != 200) {
    LOG_FATAL << "Error " << http_code << " getting commit " << ref << " from treehub";
    return EXIT_FAILURE;
  }
  LOG_INFO << "OSTree commit " << ref << " is found on treehub";

  // check if the ref is present in targets.json
  curlEasySetoptWrapper(curl.get(), CURLOPT_VERBOSE, get_curlopt_verbose());
  curlEasySetoptWrapper(curl.get(), CURLOPT_HTTPGET, 1L);
  curlEasySetoptWrapper(curl.get(), CURLOPT_NOBODY, 0L);
  treehub.InjectIntoCurl("/api/v1/user_repo/targets.json", curl.get(), true);

  std::string targets_str;
  curlEasySetoptWrapper(curl.get(), CURLOPT_WRITEFUNCTION, writeString);
  curlEasySetoptWrapper(curl.get(), CURLOPT_WRITEDATA, static_cast<void *>(&targets_str));
  result = curl_easy_perform(curl.get());

  if (result != CURLE_OK) {
    LOG_FATAL << "Error connecting to TUF repo: " << result << ": " << curl_easy_strerror(result);
    return EXIT_FAILURE;
  }

  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code != 200) {
    LOG_FATAL << "Error " << http_code << " getting targets.json from TUF repo: " << targets_str;
    return EXIT_FAILURE;
  }

  Json::Value targets_json = Utils::parseJSON(targets_str);
  std::string expiry_time_str = targets_json["signed"]["expires"].asString();
  TimeStamp timestamp(expiry_time_str);

  if (timestamp.IsExpiredAt(TimeStamp::Now())) {
    LOG_FATAL << "targets.json has been expired.";
    return EXIT_FAILURE;
  }

  Json::Value target_list = targets_json["signed"]["targets"];
  for (Json::ValueIterator t_it = target_list.begin(); t_it != target_list.end(); t_it++) {
    if ((*t_it)["hashes"]["sha256"].asString() == ref) {
      LOG_INFO << "OSTree package " << ref << " is found in targets.json";
      return EXIT_SUCCESS;
    }
  }
  LOG_INFO << "OSTree package " << ref << " was not found in targets.json";
  return EXIT_FAILURE;
}
// vim: set tabstop=2 shiftwidth=2 expandtab:
