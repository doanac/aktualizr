#include "utilities/types.h"
#include "logging/logging.h"

#include <stdexcept>
#include <utility>

TimeStamp TimeStamp::Now() {
  time_t raw_time;
  struct tm time_struct {};
  time(&raw_time);
  gmtime_r(&raw_time, &time_struct);
  char formatted[22];
  strftime(formatted, 22, "%Y-%m-%dT%H:%M:%SZ", &time_struct);
  return TimeStamp(formatted);
}

TimeStamp::TimeStamp(std::string rfc3339) {
  if (rfc3339.length() != 20 || rfc3339[19] != 'Z') {
    LOG_ERROR << "ANDY its here: " << rfc3339 << "len=" << rfc3339.length();
    // throw TimeStamp::InvalidTimeStamp();
  }
  time_ = rfc3339;
}

bool TimeStamp::IsValid() const { return time_.length() != 0; }

bool TimeStamp::IsExpiredAt(const TimeStamp &now) const {
  if (!IsValid()) {
    return true;
  }
  if (!now.IsValid()) {
    return true;
  }
  return *this < now;
}

bool TimeStamp::operator<(const TimeStamp &other) const { return IsValid() && other.IsValid() && time_ < other.time_; }

bool TimeStamp::operator>(const TimeStamp &other) const { return (other < *this); }

std::ostream &operator<<(std::ostream &os, const TimeStamp &t) {
  os << t.time_;
  return os;
}

namespace data {
Json::Value Package::toJson() {
  Json::Value json;
  json["name"] = name;
  json["version"] = version;
  return json;
}

Package Package::fromJson(const std::string &json_str) {
  Json::Reader reader;
  Json::Value json;
  reader.parse(json_str, json);
  Package package;
  package.name = json["name"].asString();
  package.version = json["version"].asString();
  return package;
}

OperationResult::OperationResult(std::string id_in, UpdateResultCode result_code_in, std::string result_text_in)
    : id(std::move(id_in)), result_code(result_code_in), result_text(std::move(result_text_in)) {}

OperationResult::OperationResult(std::string id_in, InstallOutcome outcome_in)
    : id(std::move(id_in)), result_code(outcome_in.first), result_text(outcome_in.second) {}

InstallOutcome OperationResult::toOutcome() const { return InstallOutcome(result_code, result_text); }

Json::Value OperationResult::toJson() const {
  Json::Value json;
  json["id"] = id;
  json["result_code"] = static_cast<int>(result_code);
  json["result_text"] = result_text;
  return json;
}

OperationResult OperationResult::fromJson(const std::string &json_str) {
  Json::Reader reader;
  Json::Value json;
  reader.parse(json_str, json);
  OperationResult operation_result;
  operation_result.id = json["id"].asString();
  operation_result.result_code = static_cast<UpdateResultCode>(json["result_code"].asUInt());
  operation_result.result_text = json["result_text"].asString();
  return operation_result;
}

OperationResult OperationResult::fromOutcome(const std::string &id, const InstallOutcome &outcome) {
  OperationResult operation_result(id, outcome);
  return operation_result;
}

}  // namespace data

// vim: set tabstop=2 shiftwidth=2 expandtab:
