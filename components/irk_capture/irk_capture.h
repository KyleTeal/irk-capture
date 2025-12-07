#pragma once

#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/button/button.h"
#include "esphome/components/text/text.h"

#include <NimBLEDevice.h>

namespace esphome {
namespace irk_capture {

class IRKCaptureComponent;

// Text input for BLE name
class IRKCaptureText : public text::Text, public Component {
 public:
  void set_parent(IRKCaptureComponent *parent) { parent_ = parent; }
  void control(const std::string &value) override;
 protected:
  IRKCaptureComponent *parent_;
};

// Text sensor for IRK/Address output
class IRKCaptureTextSensor : public text_sensor::TextSensor, public Component {
 public:
  void setup() override {}
  void dump_config() override {}
};

// Switch for advertising control
class IRKCaptureSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(IRKCaptureComponent *parent) { parent_ = parent; }
  void write_state(bool state) override;
 protected:
  IRKCaptureComponent *parent_;
};

// Button for new MAC
class IRKCaptureButton : public button::Button, public Component {
 public:
  void set_parent(IRKCaptureComponent *parent) { parent_ = parent; }
  void press_action() override;
 protected:
  IRKCaptureComponent *parent_;
};

// Server callbacks (NimBLE 2.x API)
class IRKServerCallbacks : public NimBLEServerCallbacks {
 public:
  IRKServerCallbacks(IRKCaptureComponent *parent) : parent_(parent) {}
  void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override;
  void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override;
  void onAuthenticationComplete(NimBLEConnInfo &connInfo) override;
 protected:
  IRKCaptureComponent *parent_;
};

// Main component
class IRKCaptureComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  // Very late priority - run AFTER esp32_ble finishes so we can take over NimBLE
  float get_setup_priority() const override { return -200.0f; }

  void set_ble_name(const std::string &name) { ble_name_ = name; }
  void set_start_on_boot(bool start) { start_on_boot_ = start; }
  void set_irk_sensor(text_sensor::TextSensor *sensor) { irk_sensor_ = sensor; }
  void set_address_sensor(text_sensor::TextSensor *sensor) { address_sensor_ = sensor; }
  void set_advertising_switch(IRKCaptureSwitch *sw) { 
    advertising_switch_ = sw; 
    sw->set_parent(this);
  }
  void set_new_mac_button(IRKCaptureButton *btn) { 
    new_mac_button_ = btn;
    btn->set_parent(this);
  }
  void set_ble_name_text(IRKCaptureText *txt) {
    ble_name_text_ = txt;
    txt->set_parent(this);
  }

  void update_ble_name(const std::string &name);
  std::string get_ble_name() { return ble_name_; }
  void start_advertising();
  void stop_advertising();
  void refresh_mac();
  bool is_advertising() { return advertising_; }

  void on_connect(uint16_t conn_handle);
  void on_disconnect();
  void on_auth_complete(bool encrypted);

 protected:
  std::string ble_name_{"JBL Tune"};
  bool start_on_boot_{true};
  text_sensor::TextSensor *irk_sensor_{nullptr};
  text_sensor::TextSensor *address_sensor_{nullptr};
  IRKCaptureSwitch *advertising_switch_{nullptr};
  IRKCaptureButton *new_mac_button_{nullptr};
  IRKCaptureText *ble_name_text_{nullptr};

  NimBLEServer *server_{nullptr};
  NimBLEService *heart_rate_service_{nullptr};
  NimBLEService *device_info_service_{nullptr};
  NimBLECharacteristic *hr_characteristic_{nullptr};

  int connection_handle_{-1};
  bool advertising_{false};
  uint32_t last_loop_{0};
  uint32_t last_notify_{0};

  bool try_get_irk(uint16_t conn_handle, std::string &irk, std::string &addr);
  void setup_ble();
};

}  // namespace irk_capture
}  // namespace esphome

