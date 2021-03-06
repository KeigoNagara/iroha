#
# Copyright Soramitsu Co., Ltd. 2017 All Rights Reserved.
# http://soramitsu.co.jp
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

add_subdirectory(backend_proto)
add_subdirectory(bindings)
add_subdirectory(converters)
add_subdirectory(builders)
add_subdirectory(validators)

AddTest(lazy_test
    lazy_test.cpp
    )
target_link_libraries(lazy_test
    boost
    )

AddTest(crypto_usage_test
    crypto_usage_test.cpp
    )
target_link_libraries(crypto_usage_test
    shared_model_ed25519_sha3
    logger
    )

AddTest(blob_test
    cryptography/blob_test.cpp
    )
target_link_libraries(blob_test
    boost
    optional
    )

AddTest(reference_holder_test
    reference_holder_test.cpp
    )
target_link_libraries(reference_holder_test
    boost
    )
