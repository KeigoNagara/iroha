/*
Copyright Soramitsu Co., Ltd. 2017 All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "model/converters/pb_transaction_factory.hpp"
#include "model/sha3_hash.hpp"
#include "module/irohad/ametsuchi/ametsuchi_mocks.hpp"
#include "module/irohad/network/network_mocks.hpp"
#include "module/irohad/validation/validation_mocks.hpp"

#include <endpoint.pb.h>
#include "main/server_runner.hpp"

#include "torii/command_client.hpp"
#include "torii/processor/query_processor_impl.hpp"

#include "torii/command_client.hpp"
#include "torii/command_service.hpp"
#include "torii/processor/query_processor_impl.hpp"
#include "torii/processor/transaction_processor_impl.hpp"
#include "torii/query_client.hpp"
#include "torii/query_service.hpp"

constexpr const char *Ip = "0.0.0.0";
constexpr int Port = 50051;

constexpr size_t TimesToriiBlocking = 5;

using ::testing::_;
using ::testing::A;
using ::testing::AtLeast;
using ::testing::Return;

using namespace iroha::network;
using namespace iroha::validation;
using namespace iroha::ametsuchi;

using Commit = rxcpp::observable<iroha::model::Block>;

class CustomPeerCommunicationServiceMock : public PeerCommunicationService {
 public:
  CustomPeerCommunicationServiceMock(
      rxcpp::subjects::subject<iroha::model::Proposal> prop_notifier,
      rxcpp::subjects::subject<Commit> commit_notifier)
      : prop_notifier_(prop_notifier), commit_notifier_(commit_notifier){};

  void propagate_transaction(
      std::shared_ptr<const iroha::model::Transaction> transaction) override {}

  rxcpp::observable<iroha::model::Proposal> on_proposal() override {
    return prop_notifier_.get_observable();
  }
  rxcpp::observable<Commit> on_commit() override {
    return commit_notifier_.get_observable();
  }

 private:
  rxcpp::subjects::subject<iroha::model::Proposal> prop_notifier_;
  rxcpp::subjects::subject<Commit> commit_notifier_;
};

// TODO: allow dynamic port binding in ServerRunner IR-741
class ToriiServiceTest : public testing::Test {
 public:
  virtual void SetUp() {
    runner = new ServerRunner(std::string(Ip) + ":" + std::to_string(Port));
    th = std::thread([this] {
      // ----------- Command Service --------------
      pcsMock = std::make_shared<CustomPeerCommunicationServiceMock>(
          prop_notifier_, commit_notifier_);
      statelessValidatorMock = std::make_shared<MockStatelessValidator>();
      wsv_query = std::make_shared<MockWsvQuery>();
      storageMock = std::make_shared<MockStorage>();
      block_query = std::make_shared<MockBlockQuery>();

      auto tx_processor =
          std::make_shared<iroha::torii::TransactionProcessorImpl>(
              pcsMock, statelessValidatorMock);
      auto pb_tx_factory =
          std::make_shared<iroha::model::converters::PbTransactionFactory>();

      //----------- Query Service ----------
      auto qpf = std::make_unique<iroha::model::QueryProcessingFactory>(
          wsv_query, block_query);

      auto qpi = std::make_shared<iroha::torii::QueryProcessorImpl>(
          std::move(qpf), statelessValidatorMock);

      auto pb_query_factory =
          std::make_shared<iroha::model::converters::PbQueryFactory>();
      auto pb_query_resp_factory =
          std::make_shared<iroha::model::converters::PbQueryResponseFactory>();

      EXPECT_CALL(*storageMock, getBlockQuery())
          .WillRepeatedly(Return(block_query));
      EXPECT_CALL(*block_query, getTxByHashSync(_))
          .WillRepeatedly(Return(boost::none));

      //----------- Server run ----------------
      runner
          ->append(std::make_unique<torii::CommandService>(
              pb_tx_factory, tx_processor, storageMock))
          .append(std::make_unique<torii::QueryService>(
              pb_query_factory, pb_query_resp_factory, qpi))
          .run();
    });

    runner->waitForServersReady();
  }

  virtual void TearDown() {
    runner->shutdown();
    delete runner;
    th.join();
  }

  ServerRunner *runner;
  std::thread th;

  std::shared_ptr<MockWsvQuery> wsv_query;
  std::shared_ptr<MockBlockQuery> block_query;
  std::shared_ptr<MockStorage> storageMock;

  rxcpp::subjects::subject<iroha::model::Proposal> prop_notifier_;
  rxcpp::subjects::subject<Commit> commit_notifier_;

  std::shared_ptr<CustomPeerCommunicationServiceMock> pcsMock;
  std::shared_ptr<MockStatelessValidator> statelessValidatorMock;
};

/**
 * @given chain of CommandClient copies and moves
 * @when status is asked
 * @then grpc returns ok
 */
TEST_F(ToriiServiceTest, CommandClient) {
  iroha::protocol::TxStatusRequest tx_request;
  iroha::protocol::ToriiResponse toriiResponse;

  auto client1 = torii::CommandSyncClient(Ip, Port);
  // Copy ctor
  torii::CommandSyncClient client2(client1);
  // copy assignment
  auto client3 = client2;
  // move ctor
  torii::CommandSyncClient client4(std::move(client3));
  // move assignment
  auto client5 = std::move(client4);
  auto stat = client5.Status(tx_request, toriiResponse);

  ASSERT_TRUE(stat.ok());
}
/**
 * @given torii service and number of transactions
 * @when retrieving their status
 * @then ensure those are not received
 */
TEST_F(ToriiServiceTest, StatusWhenTxWasNotReceivedBlocking) {
  std::vector<iroha::model::Transaction> txs;
  std::vector<std::string> tx_hashes;

  iroha::model::converters::PbTransactionFactory tx_factory;

  // create transactions, but do not send them
  for (size_t i = 0; i < TimesToriiBlocking; ++i) {
    auto new_tx = iroha::protocol::Transaction();
    auto payload = new_tx.mutable_payload();
    payload->set_tx_counter(i);
    payload->set_creator_account_id("accountA");

    auto iroha_tx = tx_factory.deserialize(new_tx);
    txs.push_back(*iroha_tx);
    auto tx_hash = iroha::hash(*iroha_tx);
    tx_hashes.push_back(tx_hash.to_string());
  }

  // get statuses of unsent transactions
  auto client = torii::CommandSyncClient(Ip, Port);

  for (size_t i = 0; i < TimesToriiBlocking; ++i) {
    iroha::protocol::TxStatusRequest tx_request;
    tx_request.set_tx_hash(tx_hashes.at(i));
    iroha::protocol::ToriiResponse toriiResponse;
    client.Status(tx_request, toriiResponse);

    ASSERT_EQ(toriiResponse.tx_status(),
              iroha::protocol::TxStatus::NOT_RECEIVED);
  }
}

/**
 * That test simulates the real behavior of the blocking Torii.
 *
 * @given torii service with mocked CommunicationService
 *        that is subscribed on custom rxcpp::subjects
 * @when sending some number of transactions to the Torii
 * @then ensure it perform as real one:
 *       - Torii returns ok status
 *       - Proper txs responses in Status are STATELESS_VALIDATION_SUCCESS,
 *         then STATEFUL_VALIDATION_SUCCESS and COMMITTED (order matters)
 *       - Tx that not in a block returns STATELESS_VALIDATION_SUCCESS and
           then STATEFUL_VALIDATION_FAILED
 */
TEST_F(ToriiServiceTest, StatusWhenBlocking) {
  EXPECT_CALL(*statelessValidatorMock,
              validate(A<const iroha::model::Transaction &>()))
      .Times(TimesToriiBlocking)
      .WillRepeatedly(Return(true));

  std::vector<iroha::model::Transaction> txs;
  std::vector<std::string> tx_hashes;

  iroha::model::converters::PbTransactionFactory tx_factory;

  auto client1 = torii::CommandSyncClient(Ip, Port);

  // create transactions and send them to Torii
  for (size_t i = 0; i < TimesToriiBlocking; ++i) {
    auto new_tx = iroha::protocol::Transaction();
    auto payload = new_tx.mutable_payload();
    payload->set_tx_counter(i);
    payload->set_creator_account_id("accountA");

    auto stat = client1.Torii(new_tx);

    auto iroha_tx = tx_factory.deserialize(new_tx);
    txs.push_back(*iroha_tx);
    auto tx_hash = iroha::hash(*iroha_tx);
    tx_hashes.push_back(tx_hash.to_string());

    ASSERT_TRUE(stat.ok());
  }

  // create proposal from these transactions
  iroha::model::Proposal proposal(txs);
  prop_notifier_.get_subscriber().on_next(proposal);

  torii::CommandSyncClient client2(client1);

  // check if stateless validation passed
  for (size_t i = 0; i < TimesToriiBlocking; ++i) {
    iroha::protocol::TxStatusRequest tx_request;
    tx_request.set_tx_hash(tx_hashes.at(i));
    iroha::protocol::ToriiResponse toriiResponse;
    client2.Status(tx_request, toriiResponse);

    ASSERT_EQ(toriiResponse.tx_status(),
              iroha::protocol::TxStatus::STATELESS_VALIDATION_SUCCESS);
  }

  // create block from the all transactions but the last one
  iroha::model::Block block;
  block.transactions.insert(
      block.transactions.begin(), txs.begin(), txs.end() - 1);

  // create commit from block notifier's observable
  rxcpp::subjects::subject<iroha::model::Block> block_notifier_;
  Commit commit = block_notifier_.get_observable();

  // invoke on next of commit_notifier by sending new block to commit
  commit_notifier_.get_subscriber().on_next(commit);
  block_notifier_.get_subscriber().on_next(block);

  auto client3 = client2;
  // check if all transactions but the last one passed stateful validation
  for (size_t i = 0; i < TimesToriiBlocking - 1; ++i) {
    iroha::protocol::TxStatusRequest tx_request;
    tx_request.set_tx_hash(tx_hashes.at(i));
    iroha::protocol::ToriiResponse toriiResponse;
    client3.Status(tx_request, toriiResponse);

    ASSERT_EQ(toriiResponse.tx_status(),
              iroha::protocol::TxStatus::STATEFUL_VALIDATION_SUCCESS);
  }

  // end current commit
  block_notifier_.get_subscriber().on_completed();

  auto client4 = client3;
  // check if all transactions but the last have committed state
  for (size_t i = 0; i < TimesToriiBlocking - 1; ++i) {
    iroha::protocol::TxStatusRequest tx_request;
    tx_request.set_tx_hash(tx_hashes.at(i));
    iroha::protocol::ToriiResponse toriiResponse;
    client4.Status(tx_request, toriiResponse);

    ASSERT_EQ(toriiResponse.tx_status(), iroha::protocol::TxStatus::COMMITTED);
  }

  torii::CommandSyncClient client5(client4);
  // check if the last transaction from txs has failed stateful validation
  iroha::protocol::TxStatusRequest last_tx_request;
  last_tx_request.set_tx_hash(tx_hashes.at(TimesToriiBlocking - 1));
  iroha::protocol::ToriiResponse stful_invalid_response;
  client5.Status(last_tx_request, stful_invalid_response);
  ASSERT_EQ(stful_invalid_response.tx_status(),
            iroha::protocol::TxStatus::STATEFUL_VALIDATION_FAILED);
}

/**
 * @given torii service and some number of transactions with hashes
 * @when sending request on this txs
 * @then ensure that response has the same hash as sent tx
 */
TEST_F(ToriiServiceTest, CheckHash) {
  // given
  std::vector<std::string> tx_hashes;
  const int tx_num = 10;

  iroha::model::converters::PbTransactionFactory tx_factory;

  // create transactions, but do not send them
  for (size_t i = 0; i < tx_num; ++i) {
    auto new_tx = iroha::protocol::Transaction();
    auto payload = new_tx.mutable_payload();
    payload->set_tx_counter(i);
    auto tx = tx_factory.deserialize(new_tx);

    tx_hashes.push_back(iroha::hash(*tx).to_string());
  }

  auto client = torii::CommandSyncClient(Ip, Port);

  // get statuses of transactions
  for (auto &hash : tx_hashes) {
    iroha::protocol::TxStatusRequest tx_request;
    tx_request.set_tx_hash(hash);
    iroha::protocol::ToriiResponse toriiResponse;
    // when
    client.Status(tx_request, toriiResponse);
    // then
    ASSERT_EQ(toriiResponse.tx_hash(), hash);
  }
}
