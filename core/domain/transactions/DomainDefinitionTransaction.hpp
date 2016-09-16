#ifndef CORE_DOMAIN_TRANSACTIONS_DOMAINDEFINITIONTRANSACTION_HPP_
#define CORE_DOMAIN_TRANSACTIONS_DOMAINDEFINITIONTRANSACTION_HPP_

#include <msgpack.hpp>

namespace DomainDefinitionTransaction {
  MSGPACK_DEFINE(hash, type, domainToDefine, accountPublicKey);
};

#endif  // CORE_DOMAIN_TRANSACTIONS_DOMAINDEFINITIONTRANSACTION_HPP_