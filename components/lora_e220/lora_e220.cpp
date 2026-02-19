#include "lora_e220.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esphome/core/log.h"

namespace esphome {
namespace lora_e220 {

static const char *const TAG = "lora_e220";
static constexpr size_t CFG_FRAME_LEN = 11;  // C1 00 08 + 8 bytes
static constexpr uint32_t VERIFY_DELAY_MS = 1500;
static constexpr uint32_t MSG_ACK_TIMEOUT_MS = 3000;

void LoRaE220::setup() {
  buf_.clear();
  this->publish_online_state_();

  if (read_on_boot_) {
    this->set_timeout(500, [this]() { this->request_read_config_(); });
  }
  if (send_on_boot_ && !tx_message_.empty()) {
    this->set_timeout(1000, [this]() {
      if (fixed_tx_) {
        ESP_LOGI(TAG, "TX(on_boot, fixed): to=0x%02X%02X ch=0x%02X msg=%s", target_addh_, target_addl_, target_ch_,
                 tx_message_.c_str());
      } else {
        ESP_LOGI(TAG, "TX(on_boot): %s", tx_message_.c_str());
      }
      this->send_text_(tx_message_);
    });
  }
}

void LoRaE220::add_ping_target(uint16_t addr, uint8_t ch) {
  PingTarget t;
  t.addh = (addr >> 8) & 0xFF;
  t.addl = addr & 0xFF;
  t.ch = ch;
  ping_targets_.push_back(t);
}

void LoRaE220::add_online_target_text_sensor(uint16_t addr, uint8_t ch, text_sensor::TextSensor *s) {
  OnlineTargetTextSensor t;
  t.addh = (addr >> 8) & 0xFF;
  t.addl = addr & 0xFF;
  t.ch = ch;
  t.sensor = s;
  online_target_text_sensors_.push_back(t);
}

void LoRaE220::read_config() { this->request_read_config_(); }

void LoRaE220::send_msg() {
  this->send("MSG");
}

void LoRaE220::send(const std::string &msg) {
  if (!fixed_tx_) {
    ESP_LOGW(TAG, "send(msg) requires fixed_tx=true and target_addr/target_ch set.");
    return;
  }
  char header[18];
  snprintf(header, sizeof(header), "MSG|%04X|%02X|", this->self_addr_(), this->self_ch_());
  const size_t header_len = strlen(header);
  if (header_len + msg.size() > max_line_) {
    ESP_LOGW(TAG, "MSG too long (%u bytes), max supported payload is %u bytes; dropping", static_cast<unsigned>(msg.size()),
             static_cast<unsigned>(max_line_ - header_len));
    return;
  }
  std::string frame(header);
  frame += msg;
  this->send_text_(frame);

  awaiting_msg_ack_ = true;
  msg_ack_deadline_ms_ = millis() + MSG_ACK_TIMEOUT_MS;
  msg_ack_expected_from_ = (static_cast<uint16_t>(target_addh_) << 8) | target_addl_;
  ESP_LOGD(TAG, "MSG sent, waiting for MSG_ACK from %s", this->format_addr_(msg_ack_expected_from_).c_str());
}

void LoRaE220::loop() {
  this->handle_rx_();
  this->maybe_timeout_warn_();
  this->maybe_send_();
  this->maybe_ping_();
  this->maybe_check_ack_timeout_();
  this->maybe_check_ping_seen_timeout_();
  this->maybe_check_msg_ack_timeout_();
}

bool LoRaE220::config_equals_(const E220Config &a, const E220Config &b) {
  return a.addh == b.addh && a.addl == b.addl && a.sped == b.sped && a.opt == b.opt && a.ch == b.ch &&
         a.reg3 == b.reg3 && a.crypth == b.crypth && a.cryptl == b.cryptl;
}

bool LoRaE220::config_equals_ignoring_crypt_(const E220Config &a, const E220Config &b) {
  return a.addh == b.addh && a.addl == b.addl && a.sped == b.sped && a.opt == b.opt && a.ch == b.ch &&
         a.reg3 == b.reg3;
}

void LoRaE220::request_read_config_() {
  uint8_t cmd[3] = {0xC1, 0x00, 0x08};
  ESP_LOGD(TAG, "Sending READ CONFIG: C1 00 08");
  this->write_array(cmd, sizeof(cmd));
  this->flush();

  last_request_ms_ = millis();
  awaiting_response_ = true;
  stage_ = Stage::WAIT_READ;
}

void LoRaE220::request_write_config_(const E220Config &cfg) {
  // C0 00 08 + params
  uint8_t cmd[11] = {0xC0, 0x00, 0x08, cfg.addh,  cfg.addl, cfg.sped, cfg.opt,  cfg.ch,  cfg.reg3,
                     cfg.crypth, cfg.cryptl};

  ESP_LOGW(TAG, "Writing config (C0 00 08 ...) because it differs from desired.");
  ESP_LOGD(TAG, "Sending WRITE CONFIG: C0 00 08 ...");

  this->write_array(cmd, sizeof(cmd));
  this->flush();

  last_request_ms_ = millis();
  awaiting_response_ = true;
  stage_ = Stage::WAIT_WRITE_ECHO;
}

void LoRaE220::handle_rx_() {
  while (this->available()) {
    uint8_t c;
    if (!this->read_byte(&c))
      break;

    // 1) Keep a frame buffer for config responses (C1 00 08)
    buf_.push_back(c);
    if (buf_.size() > 256)
      buf_.erase(buf_.begin(), buf_.begin() + (buf_.size() - 256));

    // 2) Process the same bytes as line-based payload
    this->handle_payload_byte_(c);
  }
  this->parse_buffer_();
}

void LoRaE220::handle_payload_byte_(uint8_t c) {
  if (expect_packet_rssi_) {
    expect_packet_rssi_ = false;
    if (publish_next_rssi_) {
      publish_next_rssi_ = false;
      const int dbm = static_cast<int>(c) - 256;  // E220: RSSI(dBm) = -(256 - rssi_byte)
      if (rssi_text_sensor_ != nullptr) {
        char txt[32];
        snprintf(txt, sizeof(txt), "%d dBm", dbm);
        rssi_text_sensor_->publish_state(txt);
      }
      ESP_LOGD(TAG, "RX RSSI: raw=%u dbm=%d", c, dbm);
    }
    return;
  }

  if (c == '\n') {
    if (rx_discard_until_newline_) {
      rx_discard_until_newline_ = false;
      rx_line_.clear();
      return;
    }
    if (!rx_line_.empty()) {
      const std::string line = rx_line_;
      if (rx_log_) {
        ESP_LOGD(TAG, "RX: %s", line.c_str());
      }
      for (auto *trig : on_receive_triggers_) {
        trig->trigger(line);
      }
      this->process_protocol_line_(line);
      rx_line_.clear();
    }
    return;
  }
  if (c == '\r')
    return;

  if (c >= 32 && c <= 126) {
    if (rx_discard_until_newline_)
      return;
    rx_line_.push_back(static_cast<char>(c));
    if (rx_line_.size() > max_line_) {
      ESP_LOGW(TAG, "RX line exceeded %u bytes, discarding until newline", static_cast<unsigned>(max_line_));
      rx_line_.clear();
      rx_discard_until_newline_ = true;
    }
  }
}

void LoRaE220::process_protocol_line_(const std::string &line) {
  const size_t p1 = line.find('|');
  const size_t p2 = (p1 == std::string::npos) ? std::string::npos : line.find('|', p1 + 1);
  const size_t p3 = (p2 == std::string::npos) ? std::string::npos : line.find('|', p2 + 1);
  const bool rssi_enabled = this->rssi_enabled_();
  const bool is_receiver_role = !ping_targets_.empty();  // receiver sends PING, transmitter responds with ACK

  if (line.rfind("PING|", 0) == 0 && p1 != std::string::npos && p2 != std::string::npos && p3 != std::string::npos) {
    expect_packet_rssi_ = rssi_enabled;
    publish_next_rssi_ = rssi_enabled && !is_receiver_role;  // transmitter uses RSSI from incoming PING

    const std::string seq_s = line.substr(p1 + 1, p2 - p1 - 1);
    const std::string src_s = line.substr(p2 + 1, p3 - p2 - 1);
    const std::string ch_s = line.substr(p3 + 1);

    uint16_t src_addr = 0;
    if (!this->parse_hex_u16_(src_s, &src_addr))
      return;

    char *end = nullptr;
    const unsigned long seq_ul = strtoul(seq_s.c_str(), &end, 10);
    if (*end != '\0' || seq_ul > 0xFFFF)
      return;

    const unsigned long ch_ul = strtoul(ch_s.c_str(), &end, 16);
    if (*end != '\0' || ch_ul > 0xFF)
      return;

    const uint16_t seq = static_cast<uint16_t>(seq_ul);
    const uint8_t src_ch = static_cast<uint8_t>(ch_ul);
    this->last_ping_seen_ms_ = millis();
    this->ping_seen_online_ = true;
    this->publish_online_state_();
    this->trigger_ping_(src_addr);

    char ack[48];
    snprintf(ack, sizeof(ack), "ACK|%04X|%u", this->self_addr_(), seq);
    ESP_LOGD(TAG, "PING from %s seq=%u -> ACK", this->format_addr_(src_addr).c_str(), seq);
    this->send_fixed_text_to_(src_addr, src_ch, ack);
    return;
  }

  if (line.rfind("ACK|", 0) == 0 && p1 != std::string::npos && p2 != std::string::npos) {
    expect_packet_rssi_ = rssi_enabled;
    publish_next_rssi_ = rssi_enabled && is_receiver_role;  // receiver uses RSSI from incoming ACK

    const std::string src_s = line.substr(p1 + 1, p2 - p1 - 1);
    const std::string seq_s = line.substr(p2 + 1);

    uint16_t src_addr = 0;
    if (!this->parse_hex_u16_(src_s, &src_addr))
      return;

    char *end = nullptr;
    const unsigned long seq_ul = strtoul(seq_s.c_str(), &end, 10);
    if (*end != '\0' || seq_ul > 0xFFFF)
      return;
    const uint16_t seq = static_cast<uint16_t>(seq_ul);

    for (auto &target : ping_targets_) {
      const uint16_t target_addr = (static_cast<uint16_t>(target.addh) << 8) | target.addl;
      if (target_addr != src_addr)
        continue;
      if (!target.awaiting_ack || target.pending_seq != seq)
        continue;

      target.awaiting_ack = false;
      target.online = true;
      ESP_LOGD(TAG, "ACK OK from %s seq=%u", this->format_addr_(src_addr).c_str(), seq);
      this->trigger_ping_ack_(src_addr);
      this->publish_online_state_();
      return;
    }
    return;
  }

  if (line.rfind("MSG|", 0) == 0 && p1 != std::string::npos && p2 != std::string::npos) {
    expect_packet_rssi_ = rssi_enabled;
    publish_next_rssi_ = false;

    const std::string src_s = line.substr(p1 + 1, p2 - p1 - 1);
    bool has_src_ch = false;
    uint8_t src_ch = this->self_ch_();
    std::string msg;
    if (p3 != std::string::npos) {
      const std::string ch_s = line.substr(p2 + 1, p3 - p2 - 1);
      if (!this->parse_hex_u8_(ch_s, &src_ch))
        return;
      msg = line.substr(p3 + 1);
      has_src_ch = true;
    } else {
      msg = line.substr(p2 + 1);
    }

    uint16_t src_addr = 0;
    if (!this->parse_hex_u16_(src_s, &src_addr))
      return;

    const std::string src = this->format_addr_(src_addr);
    for (auto *trig : on_message_triggers_) {
      trig->trigger(src, msg);
    }
    char ack[32];
    snprintf(ack, sizeof(ack), "MSG_ACK|%04X", this->self_addr_());
    this->send_fixed_text_to_(src_addr, src_ch, ack);
    if (!has_src_ch) {
      ESP_LOGW(TAG, "MSG from %s without source channel in payload; MSG_ACK sent on local channel 0x%02X", src.c_str(),
               src_ch);
    }
    ESP_LOGD(TAG, "MSG received from %s, sent MSG_ACK", src.c_str());
    return;
  }

  if (line.rfind("MSG_ACK|", 0) == 0 && p1 != std::string::npos) {
    expect_packet_rssi_ = rssi_enabled;
    publish_next_rssi_ = false;
    const std::string src_s = line.substr(p1 + 1);
    uint16_t src_addr = 0;
    if (!this->parse_hex_u16_(src_s, &src_addr))
      return;
    if (awaiting_msg_ack_ && src_addr == msg_ack_expected_from_) {
      awaiting_msg_ack_ = false;
      ESP_LOGI(TAG, "MSG delivered, MSG_ACK from %s", this->format_addr_(src_addr).c_str());
      this->trigger_msg_ack_(src_addr);
    }
  }
}

void LoRaE220::maybe_ping_() {
  if (awaiting_response_ || stage_ != Stage::IDLE)
    return;

  if (ping_targets_.empty())
    return;

  const uint32_t now = millis();
  if (last_ping_ms_ != 0 && now - last_ping_ms_ < ping_interval_ms_)
    return;
  last_ping_ms_ = now;

  const uint16_t my_addr = this->self_addr_();
  const uint8_t my_ch = this->self_ch_();

  for (auto &target : ping_targets_) {
    ping_seq_++;
    if (ping_seq_ == 0)
      ping_seq_++;

    char ping[64];
    snprintf(ping, sizeof(ping), "PING|%u|%04X|%02X", ping_seq_, my_addr, my_ch);

    const uint16_t target_addr = (static_cast<uint16_t>(target.addh) << 8) | target.addl;
    ESP_LOGD(TAG, "PING -> %s seq=%u", this->format_addr_(target_addr).c_str(), ping_seq_);
    this->send_fixed_text_to_(target_addr, target.ch, ping);

    target.awaiting_ack = true;
    target.pending_seq = ping_seq_;
    target.deadline_ms = now + ack_timeout_ms_;
  }
}

void LoRaE220::maybe_check_ack_timeout_() {
  if (awaiting_response_ || stage_ != Stage::IDLE)
    return;

  if (ping_targets_.empty())
    return;

  const uint32_t now = millis();
  for (auto &target : ping_targets_) {
    if (!target.awaiting_ack)
      continue;
    if ((int32_t) (now - target.deadline_ms) < 0)
      continue;

    target.awaiting_ack = false;
    target.online = false;

    const uint16_t addr = (static_cast<uint16_t>(target.addh) << 8) | target.addl;
    ESP_LOGD(TAG, "ACK TIMEOUT from %s (seq=%u)", this->format_addr_(addr).c_str(), target.pending_seq);
    this->trigger_ping_timeout_(addr);
    this->publish_online_state_();
  }
}

void LoRaE220::trigger_ping_ack_(uint16_t addr) {
  const std::string src = this->format_addr_(addr);
  for (auto *trig : on_ping_ack_triggers_) {
    trig->trigger(src);
  }
}

void LoRaE220::trigger_ping_timeout_(uint16_t addr) {
  const std::string src = this->format_addr_(addr);
  for (auto *trig : on_ping_timeout_triggers_) {
    trig->trigger(src);
  }
}

void LoRaE220::trigger_ping_(uint16_t addr) {
  const std::string src = this->format_addr_(addr);
  for (auto *trig : on_ping_triggers_) {
    trig->trigger(src);
  }
}

void LoRaE220::trigger_msg_ack_(uint16_t addr) {
  const std::string src = this->format_addr_(addr);
  for (auto *trig : on_msg_ack_triggers_) {
    trig->trigger(src);
  }
}

void LoRaE220::maybe_check_ping_seen_timeout_() {
  if (!ping_targets_.empty())
    return;  // receiver mode: online is based on ACKs
  if (!ping_seen_online_)
    return;

  const uint32_t now = millis();
  const uint32_t timeout_ms = ping_interval_ms_ * 3;
  if (timeout_ms > 0 && now - last_ping_seen_ms_ >= timeout_ms) {
    ping_seen_online_ = false;
    publish_online_state_();
  }
}

void LoRaE220::maybe_check_msg_ack_timeout_() {
  if (!awaiting_msg_ack_)
    return;
  const uint32_t now = millis();
  if ((int32_t) (now - msg_ack_deadline_ms_) >= 0) {
    awaiting_msg_ack_ = false;
    ESP_LOGW(TAG, "MSG_ACK timeout from %s", this->format_addr_(msg_ack_expected_from_).c_str());
  }
}

uint16_t LoRaE220::self_addr_() const {
  if (has_runtime_config_) {
    return (static_cast<uint16_t>(runtime_.addh) << 8) | runtime_.addl;
  }
  if (has_desired_) {
    return (static_cast<uint16_t>(desired_.addh) << 8) | desired_.addl;
  }
  return 0;
}

uint8_t LoRaE220::self_ch_() const {
  if (has_runtime_config_) {
    return runtime_.ch;
  }
  if (has_desired_) {
    return desired_.ch;
  }
  return target_ch_;
}

bool LoRaE220::rssi_enabled_() const {
  if (has_runtime_config_) {
    return (runtime_.reg3 & 0x80) != 0;
  }
  if (has_desired_) {
    return (desired_.reg3 & 0x80) != 0;
  }
  return false;
}

std::string LoRaE220::format_addr_(uint16_t addr) {
  char b[8];
  snprintf(b, sizeof(b), "0x%04X", addr);
  return std::string(b);
}

bool LoRaE220::parse_hex_u16_(const std::string &s, uint16_t *out) {
  char *end = nullptr;
  const unsigned long v = strtoul(s.c_str(), &end, 16);
  if (*end != '\0' || v > 0xFFFF) {
    return false;
  }
  *out = static_cast<uint16_t>(v);
  return true;
}

bool LoRaE220::parse_hex_u8_(const std::string &s, uint8_t *out) {
  char *end = nullptr;
  const unsigned long v = strtoul(s.c_str(), &end, 16);
  if (*end != '\0' || v > 0xFF) {
    return false;
  }
  *out = static_cast<uint8_t>(v);
  return true;
}

void LoRaE220::publish_online_state_() {
  if (!ping_targets_.empty()) {
    for (auto &ots : online_target_text_sensors_) {
      bool found = false;
      bool state = false;
      for (const auto &pt : ping_targets_) {
        if (pt.addh == ots.addh && pt.addl == ots.addl && pt.ch == ots.ch) {
          found = true;
          state = pt.online;
          break;
        }
      }
      if (found && ots.sensor != nullptr) {
        ots.sensor->publish_state(state ? "true" : "false");
      }
    }
  }

  if (online_text_sensor_ == nullptr) {
    return;
  }
  if (ping_targets_.empty()) {
    online_text_sensor_->publish_state(ping_seen_online_ ? "true" : "false");
    return;
  }

  bool all_online = true;
  for (const auto &target : ping_targets_) {
    if (!target.online) {
      all_online = false;
    }
  }
  online_text_sensor_->publish_state(all_online ? "true" : "false");
}

void LoRaE220::parse_buffer_() {
  if (buf_.size() < 3)
    return;

  // Look for C1 00 08 (read or write-echo)
  for (size_t i = 0; i + 2 < buf_.size(); i++) {
    if (buf_[i] == 0xC1 && buf_[i + 1] == 0x00 && buf_[i + 2] == 0x08) {
      if (i + CFG_FRAME_LEN <= buf_.size()) {
        this->on_config_frame_(&buf_[i], CFG_FRAME_LEN);
        buf_.erase(buf_.begin(), buf_.begin() + i + CFG_FRAME_LEN);
        awaiting_response_ = false;
      }
      return;
    }
  }
}

void LoRaE220::on_config_frame_(const uint8_t *p, size_t n) {
  (void) n;
  E220Config current;
  current.addh = p[3];
  current.addl = p[4];
  current.sped = p[5];
  current.opt = p[6];
  current.ch = p[7];
  current.reg3 = p[8];
  current.crypth = p[9];
  current.cryptl = p[10];
  runtime_ = current;
  has_runtime_config_ = true;

  const char *stage_name = (stage_ == Stage::WAIT_READ)       ? "READ"
                           : (stage_ == Stage::WAIT_WRITE_ECHO) ? "WRITE_ECHO"
                           : (stage_ == Stage::WAIT_READ_VERIFY) ? "VERIFY"
                                                                 : "FRAME";

  this->log_and_publish_(current, stage_name);

  if (stage_ == Stage::WAIT_READ) {
    if (has_desired_ && auto_write_) {
      if (!this->config_equals_(current, desired_)) {
        this->request_write_config_(desired_);
        this->verify_retries_left_ = 1;
        this->set_timeout(VERIFY_DELAY_MS, [this]() {
          this->request_read_config_();
          this->stage_ = Stage::WAIT_READ_VERIFY;
        });
        return;
      }
      ESP_LOGI(TAG, "Config matches desired. No write needed.");
    }
    stage_ = Stage::IDLE;
    return;
  }

  if (stage_ == Stage::WAIT_WRITE_ECHO) {
    stage_ = Stage::IDLE;
    return;
  }

  if (stage_ == Stage::WAIT_READ_VERIFY) {
    if (has_desired_) {
      if (this->config_equals_(current, desired_)) {
        ESP_LOGI(TAG, "Verify OK: module config now matches desired.");
        verify_retries_left_ = 0;
      } else if ((desired_.crypth != 0 || desired_.cryptl != 0) &&
                 this->config_equals_ignoring_crypt_(current, desired_) && current.crypth == 0 &&
                 current.cryptl == 0) {
        // E220 docs: CRYPT_H/CRYPT_L are write-only and read-back returns 0x0000.
        ESP_LOGI(TAG, "Verify OK (except CRYPT): read-back CRYPT=0x0000 is expected for E220.");
        verify_retries_left_ = 0;
      } else {
        if (verify_retries_left_ > 0) {
          verify_retries_left_--;
          ESP_LOGW(TAG, "Verify mismatch, retrying read in %u ms...", VERIFY_DELAY_MS);
          this->set_timeout(VERIFY_DELAY_MS, [this]() {
            this->request_read_config_();
            this->stage_ = Stage::WAIT_READ_VERIFY;
          });
          return;
        }
        ESP_LOGE(TAG, "Verify FAILED: module config still differs from desired.");
      }
    }
    stage_ = Stage::IDLE;
    return;
  }
}

void LoRaE220::log_and_publish_(const E220Config &cfg, const char *prefix) {
  ESP_LOGI(TAG, "%s: ADDR=0x%02X%02X SPED=0x%02X OPT=0x%02X CH=0x%02X REG3=0x%02X CRYPT=0x%02X%02X", prefix,
           cfg.addh, cfg.addl, cfg.sped, cfg.opt, cfg.ch, cfg.reg3, cfg.crypth, cfg.cryptl);

  if (config_text_sensor_ != nullptr) {
    char line[128];
    snprintf(line, sizeof(line), "ADDR=0x%02X%02X SPED=0x%02X OPT=0x%02X CH=0x%02X REG3=0x%02X CRYPT=0x%02X%02X",
             cfg.addh, cfg.addl, cfg.sped, cfg.opt, cfg.ch, cfg.reg3, cfg.crypth, cfg.cryptl);
    config_text_sensor_->publish_state(line);
  }
}

void LoRaE220::maybe_timeout_warn_() {
  if (!awaiting_response_)
    return;
  const uint32_t now = millis();
  if (now - last_request_ms_ >= response_timeout_ms_) {
    ESP_LOGW(TAG, "No config response within %u ms. (Mode 3? UART=9600?)", response_timeout_ms_);
    awaiting_response_ = false;
    stage_ = Stage::IDLE;
  }
}

void LoRaE220::send_fixed_text_to_(uint16_t addr, uint8_t ch, const std::string &s) {
  std::string out = s;
  if (out.empty() || out.back() != '\n')
    out.push_back('\n');

  std::vector<uint8_t> frame;
  frame.reserve(out.size() + 3);
  frame.push_back((addr >> 8) & 0xFF);
  frame.push_back(addr & 0xFF);
  frame.push_back(ch);
  frame.insert(frame.end(), out.begin(), out.end());
  this->write_array(frame.data(), frame.size());
  this->flush();
}

void LoRaE220::send_text_(const std::string &s) {
  std::string out = s;
  if (out.empty() || out.back() != '\n')
    out.push_back('\n');

  if (!fixed_tx_) {
    this->write_array(reinterpret_cast<const uint8_t *>(out.data()), out.size());
    this->flush();
    return;
  }

  this->send_fixed_text_to_((static_cast<uint16_t>(target_addh_) << 8) | target_addl_, target_ch_, out);
}

void LoRaE220::maybe_send_() {
  if (send_interval_ms_ == 0)
    return;
  if (tx_message_.empty())
    return;

  const uint32_t now = millis();
  if (last_send_ms_ == 0 || (now - last_send_ms_ >= send_interval_ms_)) {
    last_send_ms_ = now;
    if (fixed_tx_) {
      ESP_LOGI(TAG, "TX(interval, fixed): to=0x%02X%02X ch=0x%02X msg=%s", target_addh_, target_addl_, target_ch_,
               tx_message_.c_str());
    } else {
      ESP_LOGI(TAG, "TX(interval): %s", tx_message_.c_str());
    }
    this->send_text_(tx_message_);
  }
}

}  // namespace lora_e220
}  // namespace esphome
