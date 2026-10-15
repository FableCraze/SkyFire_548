#ifndef SKYFIRE_BATTLEPAY_WALLET_H
#define SKYFIRE_BATTLEPAY_WALLET_H
#include "BattlePayTypes.h"
#include "CheckedTransaction.h"

class BattlePayWallet
{
public:
    static bool Ready();
    static std::string OperationId();
    static bool Balance(uint32_t account, uint64_t& balance);
    static DatabaseCommitResult Change(uint32_t account, uint64_t amount, bool credit,
        std::string const& reason, uint32_t actor, std::string const& operation);
    static DatabaseCommitResult Quote(BattlePay::Purchase& purchase, std::string const& operation);
    static bool Load(uint64_t id, BattlePay::Purchase& purchase);
    static DatabaseCommitResult Pay(BattlePay::Purchase const& purchase);
    static DatabaseCommitResult Refund(BattlePay::Purchase const& purchase);
    static DatabaseCommitResult Complete(uint64_t id);
    static void Cancel(uint64_t id, uint32_t account);
};
#endif
