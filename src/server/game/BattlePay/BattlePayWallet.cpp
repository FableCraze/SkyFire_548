#include "BattlePayWallet.h"
#include "DatabaseEnv.h"
#include <openssl/rand.h>

namespace
{
std::string N(uint64_t value) { return std::to_string(value); }
bool LockWallet(CheckedTransaction& tx, uint32_t account, uint64_t& balance, bool* hasBoost = nullptr)
{
    auto owner = tx.Query("SELECT hasBoost FROM account WHERE id=" + N(account) + " FOR UPDATE");
    if (!owner) return false;
    if (hasBoost) *hasBoost = (*owner)[0].GetBool();
    if (!tx.Execute("INSERT IGNORE INTO battlepay_wallet (accountId,balance) VALUES (" + N(account) + ",0)")) return false;
    auto wallet = tx.Query("SELECT balance FROM battlepay_wallet WHERE accountId=" + N(account) + " FOR UPDATE");
    if (!wallet) return false;
    balance = (*wallet)[0].GetUInt64(); return true;
}
bool Move(CheckedTransaction& tx, uint32_t account, uint64_t oldBalance, uint64_t amount,
    bool credit, std::string const& kind, std::string const& reason, uint32_t actor,
    std::string const& operation, uint64_t purchase = 0)
{
    if (credit ? !BattlePay::CanAdd(oldBalance, amount) : !BattlePay::CanSubtract(oldBalance, amount)) return false;
    uint64_t balance = credit ? oldBalance + amount : oldBalance - amount;
    return tx.Execute("UPDATE battlepay_wallet SET balance=" + N(balance) + " WHERE accountId=" + N(account)) &&
        tx.Execute("INSERT INTO battlepay_wallet_movement (operationId,accountId,kind,amount,balanceBefore,balanceAfter,reason,actorAccountId,purchaseId) VALUES (" +
        tx.Quote(operation) + "," + N(account) + "," + tx.Quote(kind) + "," + N(amount) + "," + N(oldBalance) + "," + N(balance) + "," +
        tx.Quote(reason) + "," + N(actor) + "," + (purchase ? N(purchase) : "NULL") + ")");
}
}

bool BattlePayWallet::Ready()
{
    auto result = LoginDatabase.Query("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name IN ('battlepay_wallet','battlepay_wallet_movement','battlepay_purchase','battlepay_purchase_item')");
    return result && (*result)[0].GetUInt32() == 4;
}
std::string BattlePayWallet::OperationId()
{
    unsigned char bytes[16];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) return "";
    char const* hex = "0123456789abcdef";
    std::string id;
    for (auto byte : bytes) { id += hex[byte >> 4]; id += hex[byte & 15]; }
    return id;
}
bool BattlePayWallet::Balance(uint32_t account, uint64_t& balance)
{
    auto rows = LoginDatabase.PQuery("SELECT a.id,COALESCE(w.balance,0) FROM account a LEFT JOIN battlepay_wallet w ON w.accountId=a.id WHERE a.id=%u", account);
    if (!rows) return false;
    balance = (*rows)[1].GetUInt64(); return true;
}
DatabaseCommitResult BattlePayWallet::Change(uint32_t account, uint64_t amount, bool credit,
    std::string const& reason, uint32_t actor, std::string const& operation)
{
    if (!amount || reason.empty() || reason.size() > 255 || operation.size() != 32) return DatabaseCommitResult::Failed;
    return LoginDatabase.WithTransaction([&](CheckedTransaction& tx)
    {
        uint64_t balance;
        if (!LockWallet(tx, account, balance)) return false;
        auto prior = tx.Query("SELECT accountId,kind,amount,reason,actorAccountId FROM battlepay_wallet_movement WHERE operationId=" + tx.Quote(operation));
        if (prior) return (*prior)[0].GetUInt32() == account && (*prior)[1].GetString() == (credit ? "credit" : "debit") &&
            (*prior)[2].GetUInt64() == amount && (*prior)[3].GetString() == reason && (*prior)[4].GetUInt32() == actor;
        return Move(tx, account, balance, amount, credit, credit ? "credit" : "debit", reason, actor, operation);
    });
}
DatabaseCommitResult BattlePayWallet::Quote(BattlePay::Purchase& p, std::string const& operation)
{
    if (operation.size() != 32) return DatabaseCommitResult::Failed;
    return LoginDatabase.WithTransaction([&](CheckedTransaction& tx)
    {
        auto existing = tx.Query("SELECT id,accountId,realmId,targetGuid,productId,productType,price FROM battlepay_purchase WHERE operationId=" + tx.Quote(operation));
        if (existing)
        {
            if ((*existing)[1].GetUInt32()!=p.Account || (*existing)[2].GetUInt32()!=p.Realm ||
                (*existing)[3].GetUInt32()!=p.Target || (*existing)[4].GetUInt32()!=p.Product ||
                (*existing)[5].GetUInt8()!=p.Type || (*existing)[6].GetUInt64()!=p.Price) return false;
            p.Id = (*existing)[0].GetUInt64(); return true;
        }
        if (!tx.Execute("INSERT INTO battlepay_purchase (operationId,accountId,realmId,targetGuid,productId,productType,title,description,price,state) VALUES (" +
            tx.Quote(operation) + "," + N(p.Account) + "," + N(p.Realm) + "," + N(p.Target) + "," + N(p.Product) + "," + N(p.Type) + "," +
            tx.Quote(p.Title) + "," + tx.Quote(p.Description) + "," + N(p.Price) + ",0)")) return false;
        auto id = tx.Query("SELECT LAST_INSERT_ID()");
        if (!id) return false;
        p.Id = (*id)[0].GetUInt64();
        for (auto const& item : p.Items)
            if (!tx.Execute("INSERT INTO battlepay_purchase_item (purchaseId,lineId,itemId,quantity) VALUES (" + N(p.Id) + "," + N(item.Id) + "," + N(item.ItemId) + "," + N(item.Count) + ")")) return false;
        return true;
    });
}
bool BattlePayWallet::Load(uint64_t id, BattlePay::Purchase& p)
{
    auto rows = LoginDatabase.PQuery("SELECT id,accountId,realmId,targetGuid,productId,productType,title,description,price,state FROM battlepay_purchase WHERE id=" UI64FMTD, id);
    if (!rows) return false;
    auto f = rows->Fetch();
    p.Id=f[0].GetUInt64(); p.Account=f[1].GetUInt32(); p.Realm=f[2].GetUInt32(); p.Target=f[3].GetUInt32();
    p.Product=f[4].GetUInt32(); p.Type=f[5].GetUInt8(); p.Title=f[6].GetString(); p.Description=f[7].GetString();
    p.Price=f[8].GetUInt64(); p.State=static_cast<BattlePay::PurchaseState>(f[9].GetUInt8()); p.Items.clear();
    auto items = LoginDatabase.PQuery("SELECT lineId,itemId,quantity FROM battlepay_purchase_item WHERE purchaseId=" UI64FMTD " ORDER BY lineId", id);
    if (items) do { auto fields=items->Fetch(); p.Items.push_back({fields[0].GetUInt32(),fields[1].GetUInt32(),fields[2].GetUInt32()}); } while (items->NextRow());
    return true;
}
DatabaseCommitResult BattlePayWallet::Pay(BattlePay::Purchase const& p)
{
    return LoginDatabase.WithTransaction([&](CheckedTransaction& tx)
    {
        uint64_t balance; bool boost;
        if (!LockWallet(tx, p.Account, balance, &boost)) return false;
        auto row=tx.Query("SELECT state,price,accountId FROM battlepay_purchase WHERE id=" + N(p.Id) + " FOR UPDATE");
        if (!row || (*row)[1].GetUInt64()!=p.Price || (*row)[2].GetUInt32()!=p.Account) return false;
        auto state=static_cast<BattlePay::PurchaseState>((*row)[0].GetUInt8());
        if (state==BattlePay::PurchaseState::Paid || state==BattlePay::PurchaseState::Delivered) return true;
        if (state!=BattlePay::PurchaseState::Quoted || (p.Type==2 && boost)) return false;
        if (!Move(tx,p.Account,balance,p.Price,false,"purchase",p.Title,0,"purchase:"+N(p.Id),p.Id)) return false;
        if (p.Type==2 && !tx.Execute("UPDATE account SET hasBoost=1 WHERE id="+N(p.Account))) return false;
        return tx.Execute("UPDATE battlepay_purchase SET state="+std::string(p.Type==2?"2":"1")+",paidAt=CURRENT_TIMESTAMP WHERE id="+N(p.Id));
    });
}
DatabaseCommitResult BattlePayWallet::Refund(BattlePay::Purchase const& p)
{
    return LoginDatabase.WithTransaction([&](CheckedTransaction& tx)
    {
        uint64_t balance;
        if (!LockWallet(tx,p.Account,balance)) return false;
        auto row=tx.Query("SELECT state FROM battlepay_purchase WHERE id="+N(p.Id)+" FOR UPDATE");
        if (!row) return false;
        if ((*row)[0].GetUInt8()==4) return true;
        if ((*row)[0].GetUInt8()!=1 || !Move(tx,p.Account,balance,p.Price,true,"refund","Undeliverable recipient",0,"refund:"+N(p.Id),p.Id)) return false;
        return tx.Execute("UPDATE battlepay_purchase SET state=4 WHERE id="+N(p.Id));
    });
}
DatabaseCommitResult BattlePayWallet::Complete(uint64_t id)
{
    return LoginDatabase.WithTransaction([&](CheckedTransaction& tx) { return tx.Execute("UPDATE battlepay_purchase SET state=2 WHERE id="+N(id)+" AND state=1"); });
}
void BattlePayWallet::Cancel(uint64_t id,uint32_t account)
{
    LoginDatabase.PExecute("UPDATE battlepay_purchase SET state=3 WHERE id=" UI64FMTD " AND accountId=%u AND state=0",id,account);
}
