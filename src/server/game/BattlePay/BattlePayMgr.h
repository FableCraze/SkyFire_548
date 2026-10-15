#ifndef SKYFIRE_BATTLEPAY_MGR_H
#define SKYFIRE_BATTLEPAY_MGR_H
#include "Common.h"
#include "BattlePayTypes.h"
#include <array>
#include <map>
#include <memory>

class WorldSession;
struct BattlePayProduct
{
    uint32 Id=0,Icon=0,Discount=0,DisplayId=0,Flags=0,FlagsInfo=0;
    uint64 Price=0;
    uint8 Type=0,ChoiceType=0;
    std::string Title,Description;
    std::vector<BattlePay::ProductItem> Items;
    std::array<std::string,12> Titles,Descriptions;
};
struct BattlePayGroup
{
    uint32 Id=0,Icon=0; int32 Index=0; uint8 Type=0;
    std::string Name; std::array<std::string,12> Names;
};
struct BattlePayEntry
{
    uint32 Id=0,ProductId=0,GroupId=0,Icon=0,DisplayId=0,Flags=0;
    int32 Index=0; uint8 Banner=0;
    std::string Title,Description; std::array<std::string,12> Titles,Descriptions;
};
struct BattlePayCatalog
{
    std::map<uint32,BattlePayProduct> Products;
    std::vector<BattlePayGroup> Groups;
    std::vector<BattlePayEntry> Entries;
};
struct BattlePayContext
{
    enum Stage { Idle, Start, Allow, Confirmation, Charge, Reconcile };
    Stage Phase=Idle;
    uint32 Timer=0,Age=0,ClientToken=0,ServerToken=0;
    BattlePay::Purchase Purchase;
};
class BattlePayMgr
{
public:
    static BattlePayMgr* Instance();
    bool LoadFromDb();
    bool InitializePersistence();
    void Configure();
    bool IsStoreEnabled() const { return _ready && _catalog && _enabled; }
    bool CanPurchase() const { return IsStoreEnabled() && _purchases; }
    uint32 GetStoreCurrency() const { return _currency; }
    bool IsReady() const { return _ready; }
    void SendBattlePayProductList(WorldSession* session);
    void SendBattlePayPurchaseList(WorldSession* session);
    void SendBattlePayDistributionList(WorldSession* session);
    void StartPurchase(WorldSession* session,uint32 product,uint32 token,uint64 target);
    void ConfirmPurchase(WorldSession* session,uint64 price,uint32 token,bool confirm);
    void UpdateSession(WorldSession* session,uint32 diff);
    void Update(uint32 diff);
    bool Deliver(uint64 purchaseId);
    void SendPointsBalance(WorldSession* session);
private:
    void SendUpdate(WorldSession* session,uint32 status,uint32 result);
    void Reject(WorldSession* session,uint32 result);
    bool ValidateTarget(BattlePay::Purchase const& purchase);
    std::shared_ptr<BattlePayCatalog const> _catalog;
    bool _ready=false,_enabled=false,_purchases=false;
    uint32 _currency=16,_recoveryTimer=0;
};
#define sBattlePayMgr BattlePayMgr::Instance()
#endif
