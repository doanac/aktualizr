#ifndef UPTANE_SECONDARYINTERFACE_H
#define UPTANE_SECONDARYINTERFACE_H

#include <string>

#include "json/json.h"

#include "uptane/secondaryconfig.h"
#include "uptane/tuf.h"
#include "utilities/events.h"

/* Json snippet returned by sendMetaXXX():
 * {
 *   valid = true/false,
 *   wait_for_target = true/false
 * }
 */

namespace Uptane {

class SecondaryInterface {
 public:
  explicit SecondaryInterface(SecondaryConfig sconfig_in) : sconfig(std::move(sconfig_in)) {}
  virtual ~SecondaryInterface() = default;
  virtual EcuSerial getSerial() { return Uptane::EcuSerial(sconfig.ecu_serial); }
  virtual Uptane::HardwareIdentifier getHwId() { return Uptane::HardwareIdentifier(sconfig.ecu_hardware_id); }
  virtual PublicKey getPublicKey() = 0;

  virtual Json::Value getManifest() = 0;
  virtual bool putMetadata(const RawMetaPack& meta_pack) = 0;
  virtual int32_t getRootVersion(bool director) = 0;
  virtual bool putRoot(const std::string& root, bool director) = 0;

  virtual bool sendFirmwareAsync(const std::shared_ptr<std::string>& data) = 0;
  const SecondaryConfig sconfig;
  void addEventsChannel(std::shared_ptr<event::Channel> channel) { events_channel = std::move(channel); }

 protected:
  void sendEvent(std::shared_ptr<event::BaseEvent> event) {
    if (events_channel) {
      (*events_channel)(std::move(event));
    }
  }

 private:
  std::shared_ptr<event::Channel> events_channel;
};
}  // namespace Uptane

#endif  // UPTANE_SECONDARYINTERFACE_H