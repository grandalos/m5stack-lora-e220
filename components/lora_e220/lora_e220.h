#pragma once

#include <vector>
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace lora_e220 {

struct E220Config {
  uint8_t addh{0};
  uint8_t addl{0};
  uint8_t sped{0};
  uint8_t opt{0};
  uint8_t ch{0};
  uint8_t reg3{0};
  uint8_t crypth{0};
  uint8_t cryptl{0};
};

struct PingTarget {
  uint8_t addh{0};
  uint8_t addl{0};
  uint8_t ch{0x02};
  bool online{false};
  bool awaiting_ack{false};
  uint16_t pending_seq{0};
  uint32_t deadline_ms{0};
};

struct OnlineTargetTextSensor {
  uint8_t addh{0};
  uint8_t addl{0};
  uint8_t ch{0x02};
  text_sensor::TextSensor *sensor{nullptr};
};

class LoRaE220 : public Component, public uart::UARTDevice {
 public:
  void set_read_on_boot(bool v) { read_on_boot_ = v; }
  void set_response_timeout(uint32_t ms) { response_timeout_ms_ = ms; }
  void set_config_text_sensor(text_sensor::TextSensor *s) { config_text_sensor_ = s; }

  // config setters
  void set_has_config(bool v) { has_config_ = v; }
  void set_auto_write(bool v) { auto_write_ = v; }
  void set_config_addr(uint16_t addr) { config_.addh = (addr >> 8) & 0xFF; config_.addl = addr & 0xFF; }
  void set_config_sped(uint8_t v) { config_.sped = v; }
  void set_config_option(uint8_t v) { config_.opt = v; }
  void set_config_ch(uint8_t v) { config_.ch = v; }
  void set_config_reg3(uint8_t v) { config_.reg3 = v; }
  void set_config_crypt(uint16_t c) { config_.crypth = (c >> 8) & 0xFF; config_.cryptl = c & 0xFF; }
  void set_tx_message(const std::string &s) { tx_message_ = s; }
  void set_send_on_boot(bool v) { send_on_boot_ = v; }
  void set_send_interval_ms(uint32_t ms) { send_interval_ms_ = ms; }
  void set_rx_log(bool v) { rx_log_ = v; }
  void set_fixed_tx(bool v) { fixed_tx_ = v; }
  void set_target_addr(uint16_t addr) { target_addh_ = (addr >> 8) & 0xFF; target_addl_ = addr & 0xFF; }
  void set_target_ch(uint8_t ch) { target_ch_ = ch; }
  void set_ping_interval_ms(uint32_t ms) { ping_interval_ms_ = ms; }
  void set_ack_timeout_ms(uint32_t ms) { ack_timeout_ms_ = ms; }
  void add_ping_target(uint16_t addr, uint8_t ch);
  void set_online_text_sensor(text_sensor::TextSensor *s) { online_text_sensor_ = s; }
  void set_rssi_text_sensor(text_sensor::TextSensor *s) { rssi_text_sensor_ = s; }
  void add_online_target_text_sensor(uint16_t addr, uint8_t ch, text_sensor::TextSensor *s);
  void add_on_receive_trigger(Trigger<std::string> *trig) { on_receive_triggers_.push_back(trig); }
  void add_on_message_trigger(Trigger<std::string, std::string> *trig) { on_message_triggers_.push_back(trig); }
  void add_on_ping_ack_trigger(Trigger<std::string> *trig) { on_ping_ack_triggers_.push_back(trig); }
  void add_on_ping_timeout_trigger(Trigger<std::string> *trig) { on_ping_timeout_triggers_.push_back(trig); }
  void add_on_ping_trigger(Trigger<std::string> *trig) { on_ping_triggers_.push_back(trig); }
  void add_on_msg_ack_trigger(Trigger<std::string> *trig) { on_msg_ack_triggers_.push_back(trig); }
  void setup() override;
  void loop() override;

  void read_config();  // ręcznie w przyszłości
  void send(const std::string &msg);
  void send_msg();

 protected:
  void request_read_config_();
  void request_write_config_(const E220Config &cfg);
  void handle_rx_();
  void parse_buffer_();
  void on_config_frame_(const uint8_t *p, size_t n);
  void maybe_send_();
  void maybe_ping_();
  void maybe_check_ack_timeout_();
  void maybe_check_ping_seen_timeout_();
  void maybe_check_msg_ack_timeout_();
  void handle_payload_byte_(uint8_t c);
  void send_text_(const std::string &s);
  void send_fixed_text_to_(uint16_t addr, uint8_t ch, const std::string &s);
  void process_protocol_line_(const std::string &line);
  bool parse_hex_u16_(const std::string &s, uint16_t *out);
  bool parse_hex_u8_(const std::string &s, uint8_t *out);
  std::string format_addr_(uint16_t addr);
  uint16_t self_addr_() const;
  uint8_t self_ch_() const;
  bool rssi_enabled_() const;
  void publish_online_state_();
  void trigger_ping_ack_(uint16_t addr);
  void trigger_ping_timeout_(uint16_t addr);
  void trigger_ping_(uint16_t addr);
  void trigger_msg_ack_(uint16_t addr);
  void log_and_publish_(const E220Config &cfg, const char *prefix);
  bool config_equals_(const E220Config &a, const E220Config &b);
  bool config_equals_ignoring_crypt_(const E220Config &a, const E220Config &b);

  void maybe_timeout_warn_();

  bool read_on_boot_{true};
  uint32_t response_timeout_ms_{600};

  std::vector<uint8_t> buf_;
  uint32_t last_request_ms_{0};
  bool awaiting_response_{false};

  text_sensor::TextSensor *config_text_sensor_{nullptr};
  text_sensor::TextSensor *online_text_sensor_{nullptr};
  text_sensor::TextSensor *rssi_text_sensor_{nullptr};
// TX
  std::string tx_message_{};
  bool send_on_boot_{false};
  uint32_t send_interval_ms_{0};
  uint32_t last_send_ms_{0};
  bool fixed_tx_{false};
  uint8_t target_addh_{0x00};
  uint8_t target_addl_{0x00};
  uint8_t target_ch_{0x02};
  std::vector<Trigger<std::string> *> on_receive_triggers_{};
  std::vector<Trigger<std::string, std::string> *> on_message_triggers_{};
  std::vector<Trigger<std::string> *> on_ping_ack_triggers_{};
  std::vector<Trigger<std::string> *> on_ping_timeout_triggers_{};
  std::vector<Trigger<std::string> *> on_ping_triggers_{};
  std::vector<Trigger<std::string> *> on_msg_ack_triggers_{};
  uint32_t ping_interval_ms_{5000};
  uint32_t ack_timeout_ms_{1500};
  uint32_t last_ping_ms_{0};
  uint16_t ping_seq_{0};
  uint8_t verify_retries_left_{0};
  bool ping_seen_online_{false};
  uint32_t last_ping_seen_ms_{0};
  bool awaiting_msg_ack_{false};
  uint32_t msg_ack_deadline_ms_{0};
  uint16_t msg_ack_expected_from_{0};
  std::vector<PingTarget> ping_targets_{};
  std::vector<OnlineTargetTextSensor> online_target_text_sensors_{};

// RX payload (linie)
  bool rx_log_{true};
  std::string rx_line_{};
  const size_t max_line_{240};
  bool rx_discard_until_newline_{false};
  bool expect_packet_rssi_{false};
  bool publish_next_rssi_{false};
  // configured module config
  bool has_config_{false};
  bool auto_write_{true};
  E220Config config_{};
  bool has_runtime_config_{false};
  E220Config runtime_{};

  // state machine
  enum class Stage : uint8_t { IDLE, WAIT_READ, WAIT_WRITE_ECHO, WAIT_READ_VERIFY };
  Stage stage_{Stage::IDLE};
};

class LoRaE220OnReceiveTrigger : public Trigger<std::string> {
 public:
  explicit LoRaE220OnReceiveTrigger(LoRaE220 *parent) { parent->add_on_receive_trigger(this); }
};

class LoRaE220OnMessageTrigger : public Trigger<std::string, std::string> {
 public:
  explicit LoRaE220OnMessageTrigger(LoRaE220 *parent) { parent->add_on_message_trigger(this); }
};

class LoRaE220OnPingAckTrigger : public Trigger<std::string> {
 public:
  explicit LoRaE220OnPingAckTrigger(LoRaE220 *parent) { parent->add_on_ping_ack_trigger(this); }
};

class LoRaE220OnPingTimeoutTrigger : public Trigger<std::string> {
 public:
  explicit LoRaE220OnPingTimeoutTrigger(LoRaE220 *parent) { parent->add_on_ping_timeout_trigger(this); }
};

class LoRaE220OnPingTrigger : public Trigger<std::string> {
 public:
  explicit LoRaE220OnPingTrigger(LoRaE220 *parent) { parent->add_on_ping_trigger(this); }
};

class LoRaE220OnMsgAckTrigger : public Trigger<std::string> {
 public:
  explicit LoRaE220OnMsgAckTrigger(LoRaE220 *parent) { parent->add_on_msg_ack_trigger(this); }
};

}  // namespace lora_e220
}  // namespace esphome
