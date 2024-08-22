// Copyright (c) 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/moqt/tools/chat_client.h"
#include "quiche/quic/moqt/tools/chat_server.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_ip_address.h"

namespace moqt {

namespace test {

using ::testing::_;

constexpr std::string kChatHostname = "127.0.0.1";

class MockChatUserInterface : public ChatUserInterface {
 public:
  void Initialize(quiche::MultiUseCallback<void(absl::string_view)> callback,
                  quic::QuicEventLoop* event_loop) override {
    callback_ = std::move(callback);
    event_loop_ = event_loop;
  }

  void IoLoop() override {
    event_loop_->RunEventLoopOnce(moqt::kChatEventLoopDuration);
  }

  MOCK_METHOD(void, WriteToOutput,
              (absl::string_view user, absl::string_view message), (override));

  void SendMessage(absl::string_view message) { callback_(message); }

 private:
  quiche::MultiUseCallback<void(absl::string_view)> callback_;
  quic::QuicEventLoop* event_loop_;
  std::string message_;
};

class MoqChatEndToEndTest : public quiche::test::QuicheTest {
 public:
  MoqChatEndToEndTest()
      : server_(quic::test::crypto_test_utils::ProofSourceForTesting(),
                "test_chat", "") {
    quiche::QuicheIpAddress bind_address;
    bind_address.FromString(kChatHostname);
    EXPECT_TRUE(server_.moqt_server().quic_server().CreateUDPSocketAndListen(
        quic::QuicSocketAddress(bind_address, 0)));
    auto if1ptr = std::make_unique<MockChatUserInterface>();
    auto if2ptr = std::make_unique<MockChatUserInterface>();
    interface1_ = if1ptr.get();
    interface2_ = if2ptr.get();
    uint16_t port = server_.moqt_server().quic_server().port();
    client1_ = std::make_unique<ChatClient>(
        quic::QuicServerId(kChatHostname, port), true, std::move(if1ptr),
        server_.moqt_server().quic_server().event_loop());
    client2_ = std::make_unique<ChatClient>(
        quic::QuicServerId(kChatHostname, port), true, std::move(if2ptr),
        server_.moqt_server().quic_server().event_loop());
  }

  ChatServer server_;
  MockChatUserInterface *interface1_, *interface2_;
  std::unique_ptr<ChatClient> client1_, client2_;
};

TEST_F(MoqChatEndToEndTest, EndToEndTest) {
  EXPECT_TRUE(client1_->Connect("/moq-chat", "client1", "test_chat"));
  EXPECT_TRUE(client2_->Connect("/moq-chat", "client2", "test_chat"));
  EXPECT_TRUE(client1_->AnnounceAndSubscribe());
  EXPECT_TRUE(client2_->AnnounceAndSubscribe());
  EXPECT_CALL(*interface2_, WriteToOutput("client1", "Hello")).Times(1);
  interface1_->SendMessage("Hello");
  server_.moqt_server().quic_server().WaitForEvents();
  EXPECT_CALL(*interface1_, WriteToOutput("client2", "Hi")).Times(1);
  interface2_->SendMessage("Hi");
  server_.moqt_server().quic_server().WaitForEvents();
  EXPECT_CALL(*interface2_, WriteToOutput("client1", "How are you?")).Times(1);
  interface1_->SendMessage("How are you?");
  server_.moqt_server().quic_server().WaitForEvents();
  EXPECT_CALL(*interface1_, WriteToOutput("client2", "Good, and you?"))
      .Times(1);
  interface2_->SendMessage("Good, and you?");
  server_.moqt_server().quic_server().WaitForEvents();
  EXPECT_CALL(*interface2_, WriteToOutput("client1", "I'm fine")).Times(1);
  interface1_->SendMessage("I'm fine");
  server_.moqt_server().quic_server().WaitForEvents();
  EXPECT_CALL(*interface1_, WriteToOutput("client2", "Goodbye")).Times(1);
  interface2_->SendMessage("Goodbye");
  server_.moqt_server().quic_server().WaitForEvents();
  interface1_->SendMessage("/exit");
  EXPECT_CALL(*interface2_, WriteToOutput(_, _)).Times(0);
  server_.moqt_server().quic_server().WaitForEvents();
}

// TODO(martinduke): Add tests for users leaving the chat

}  // namespace test

}  // namespace moqt