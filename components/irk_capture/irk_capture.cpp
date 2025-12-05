#include "irk_capture.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <esp_random.h>

namespace esphome {
namespace irk_capture {

static const char *const TAG = "irk_capture";
static const char hex_digits[] = "0123456789abcdef";

// Read bond info from NimBLE store
static int ble_sm_read_bond(uint16_t conn_handle, struct ble_store_value_sec *out_bond) {
  struct ble_store_key_sec key_sec;
  struct ble_gap_conn_desc desc;
  int rc = ble_gap_conn_find(conn_handle, &desc);
  if (rc != 0) return rc;
  memset(&key_sec, 0, sizeof key_sec);
  key_sec.peer_addr = desc.peer_id_addr;
  return ble_store_read_peer_sec(&key_sec, out_bond);
}

// Switch implementation
void IRKCaptureSwitch::write_state(bool state) {
  if (state) {
    parent_->start_advertising();
  } else {
    parent_->stop_advertising();
  }
  publish_state(state);
}

// Button implementation
void IRKCaptureButton::press_action() {
  ESP_LOGI(TAG, "Refreshing MAC address...");
  parent_->refresh_mac();
}

// Text input implementation for BLE name
void IRKCaptureText::control(const std::string &value) {
  ESP_LOGI(TAG, "BLE name changed to: %s", value.c_str());
  publish_state(value);
  parent_->update_ble_name(value);
}

// Server callbacks (NimBLE 2.x API)
void IRKServerCallbacks::onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) {
  ESP_LOGI(TAG, "Device connected: %s (handle: %d)", 
           connInfo.getAddress().toString().c_str(), connInfo.getConnHandle());
  parent_->on_connect(connInfo.getConnHandle());
  NimBLEDevice::startAdvertising();
}

void IRKServerCallbacks::onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) {
  ESP_LOGI(TAG, "Device disconnected: %s (reason: %d)", 
           connInfo.getAddress().toString().c_str(), reason);
  parent_->on_disconnect();
  NimBLEDevice::startAdvertising();
}

void IRKServerCallbacks::onAuthenticationComplete(NimBLEConnInfo &connInfo) {
  ESP_LOGD(TAG, "Auth complete: encrypted=%s, bonded=%s", 
           connInfo.isEncrypted() ? "yes" : "no",
           connInfo.isBonded() ? "yes" : "no");
  parent_->on_auth_complete(connInfo.isEncrypted());
}

// Main component
void IRKCaptureComponent::setup() {
  ESP_LOGI(TAG, "Setting up IRK Capture...");
  setup_ble();
}

void IRKCaptureComponent::setup_ble() {
  // esp32_ble may have already initialized NimBLE - deinit first to take full control
  if (NimBLEDevice::isInitialized()) {
    ESP_LOGD(TAG, "Deinitializing NimBLE to take control from esp32_ble...");
    NimBLEDevice::deinit(false);
    delay(100);
  }
  
  NimBLEDevice::init(ble_name_);

  // Random MAC address
  uint8_t mac[6];
  esp_fill_random(mac, 6);
  mac[0] |= 0xC0;  // Random static
  mac[0] &= 0xFE;  // Clear multicast
  
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  ble_hs_id_set_rnd(mac);
  ESP_LOGI(TAG, "BLE MAC: %02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // Security settings (matches ESPresense)
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityAuth(true, true, true);

  server_ = NimBLEDevice::createServer();
  server_->setCallbacks(new IRKServerCallbacks(this));

  // Heart Rate Service (0x180D) - triggers iOS pairing
  heart_rate_service_ = server_->createService("180D");
  hr_characteristic_ = heart_rate_service_->createCharacteristic(
      "2A37", NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC, 2);

  // Device Info Service (0x180A)
  device_info_service_ = server_->createService("180A");
  auto *manuf = device_info_service_->createCharacteristic("2A29", NIMBLE_PROPERTY::READ_ENC);
  manuf->setValue("ESPresense");
  auto *model = device_info_service_->createCharacteristic("2A24", NIMBLE_PROPERTY::READ_ENC);
  model->setValue(ble_name_);

  // Battery Service (0x180F)
  auto *battery = server_->createService("180F");
  auto *batt_level = battery->createCharacteristic("2A19", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  batt_level->setValue((uint8_t)100);

  heart_rate_service_->start();
  device_info_service_->start();
  battery->start();
  server_->start();

  start_advertising();
  
  if (ble_name_text_) {
    ble_name_text_->publish_state(ble_name_);
  }
  
  ESP_LOGI(TAG, "IRK Capture ready - advertising as '%s'", ble_name_.c_str());
}

void IRKCaptureComponent::start_advertising() {
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->reset();
  adv->setName(ble_name_);
  adv->addServiceUUID("180D");
  adv->addServiceUUID("180F");
  adv->setAppearance(0x0340);
  adv->setMinInterval(0x20);
  adv->setMaxInterval(0x40);
  adv->enableScanResponse(true);
  adv->setConnectableMode(BLE_GAP_CONN_MODE_UND);
  adv->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
  
  adv->start();
  advertising_ = true;
  
  if (advertising_switch_) {
    advertising_switch_->publish_state(true);
  }
  ESP_LOGD(TAG, "Advertising started");
}

void IRKCaptureComponent::stop_advertising() {
  NimBLEDevice::stopAdvertising();
  advertising_ = false;
  
  if (advertising_switch_) {
    advertising_switch_->publish_state(false);
  }
  ESP_LOGD(TAG, "Advertising stopped");
}

void IRKCaptureComponent::refresh_mac() {
  stop_advertising();
  App.feed_wdt();
  delay(100);
  App.feed_wdt();
  
  uint8_t mac[6];
  esp_fill_random(mac, 6);
  mac[0] |= 0xC0;
  mac[0] &= 0xFE;
  
  ble_hs_id_set_rnd(mac);
  ESP_LOGI(TAG, "New MAC: %02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  
  App.feed_wdt();
  delay(100);
  App.feed_wdt();
  
  start_advertising();
}

void IRKCaptureComponent::update_ble_name(const std::string &name) {
  ble_name_ = name;
  
  if (advertising_) {
    stop_advertising();
    App.feed_wdt();
    delay(100);
    App.feed_wdt();
    start_advertising();
  }
}

void IRKCaptureComponent::on_connect(uint16_t conn_handle) {
  connection_handle_ = conn_handle;
}

void IRKCaptureComponent::on_disconnect() {
  connection_handle_ = -1;
}

void IRKCaptureComponent::on_auth_complete(bool encrypted) {
  // IRK will be captured in loop() after auth completes
}

bool IRKCaptureComponent::try_get_irk(uint16_t conn_handle, std::string &irk, std::string &addr) {
  struct ble_store_value_sec bond;
  struct ble_gap_conn_desc desc;

  int rc = ble_gap_conn_find(conn_handle, &desc);
  if (rc != 0) return false;
  
  rc = ble_sm_read_bond(conn_handle, &bond);
  if (rc != 0) return false;
  
  if (!bond.irk_present) return false;

  // Format address
  char addr_buf[18];
  snprintf(addr_buf, sizeof(addr_buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           desc.peer_ota_addr.val[5], desc.peer_ota_addr.val[4],
           desc.peer_ota_addr.val[3], desc.peer_ota_addr.val[2],
           desc.peer_ota_addr.val[1], desc.peer_ota_addr.val[0]);
  addr = addr_buf;

  // Format IRK (reversed)
  std::string output;
  output.reserve(32);
  for (int i = 0; i < 16; i++) {
    auto c = bond.irk[15 - i];
    output.push_back(hex_digits[c >> 4]);
    output.push_back(hex_digits[c & 15]);
  }
  irk = output;
  return true;
}

void IRKCaptureComponent::loop() {
  uint32_t now = millis();
  if (now - last_loop_ < 500) return;
  last_loop_ = now;

  if (!server_->getConnectedCount()) return;

  // Send HR notifications to trigger/maintain pairing
  if (hr_characteristic_ && now - last_notify_ > 250) {
    last_notify_ = now;
    uint8_t buf[2] = {0x06, (uint8_t)(micros() & 0xFF)};
    hr_characteristic_->setValue(buf, 2);
    hr_characteristic_->notify();
  }

  // Try to get IRK
  if (connection_handle_ > -1) {
    std::string irk, addr;
    if (try_get_irk(connection_handle_, irk, addr)) {
      ESP_LOGI(TAG, "");
      ESP_LOGI(TAG, "*** IRK CAPTURED ***");
      ESP_LOGI(TAG, "Address: %s", addr.c_str());
      ESP_LOGI(TAG, "IRK: irk:%s", irk.c_str());
      ESP_LOGI(TAG, "");

      if (irk_sensor_) {
        irk_sensor_->publish_state("irk:" + irk);
      }
      if (address_sensor_) {
        address_sensor_->publish_state(addr);
      }

      server_->disconnect(connection_handle_);
      connection_handle_ = -1;
    }
  }
}

void IRKCaptureComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "IRK Capture:");
  ESP_LOGCONFIG(TAG, "  BLE Name: %s", ble_name_.c_str());
  ESP_LOGCONFIG(TAG, "  Advertising: %s", advertising_ ? "YES" : "NO");
}

}  // namespace irk_capture
}  // namespace esphome
