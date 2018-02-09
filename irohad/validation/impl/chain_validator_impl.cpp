/**
 * Copyright Soramitsu Co., Ltd. 2017 All Rights Reserved.
 * http://soramitsu.co.jp
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "validation/impl/chain_validator_impl.hpp"

#include "ametsuchi/impl/postgres_wsv_query.hpp"
#include "consensus/consensus_common.hpp"

#include "backend/protobuf/from_old_model.hpp"

namespace iroha {
  namespace validation {
    ChainValidatorImpl::ChainValidatorImpl() {
      log_ = logger::log("ChainValidator");
    }

    bool ChainValidatorImpl::validateBlock(const model::Block &old_block,
                                           ametsuchi::MutableStorage &storage) {
      auto block = shared_model::proto::from_old(old_block);
      log_->info("validate block: height {}, hash {}",
                 block.height(),
                 block.hash().hex());
      auto apply_block = [](
          const auto &old_block, auto &queries, const auto &top_hash) {
        auto block = shared_model::proto::from_old(old_block);
        auto peers = queries.getPeers();
        if (not peers.has_value()) {
          return false;
        }
        return old_block.prev_hash == top_hash
            and consensus::hasSupermajority(block.signatures().size(),
                                            peers.value().size())
            and consensus::peersSubset(old_block.sigs, peers.value());
      };

      // Apply to temporary storage
      return storage.apply(old_block, apply_block);
    }

    bool ChainValidatorImpl::validateChain(Commit blocks,
                                           ametsuchi::MutableStorage &storage) {
      log_->info("validate chain...");
      return blocks
          .all([this, &storage](auto old_block) {
            auto block = shared_model::proto::from_old(old_block);
            log_->info("Validating block: height {}, hash {}",
                       block.height(),
                       block.hash().hex());
            return this->validateBlock(old_block, storage);
          })
          .as_blocking()
          .first();
    }
  }  // namespace validation
}  // namespace iroha
