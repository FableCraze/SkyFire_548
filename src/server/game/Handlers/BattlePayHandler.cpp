#include "BattlePayMgr.h"
#include "WorldSession.h"

void WorldSession::HandleBattlePayGetProductList(WorldPacket& packet)
{
    if (packet.size()) { packet.rfinish(); return; }
    sBattlePayMgr->SendBattlePayProductList(this);
    sBattlePayMgr->SendPointsBalance(this);
}
void WorldSession::HandleBattlePayGetPurchaseList(WorldPacket& packet)
{
    if (packet.size()) { packet.rfinish(); return; }
    // The store waits for all three lists. Refresh distributions together with
    // purchases, including when opened after a logout or a UI reload.
    sBattlePayMgr->SendBattlePayDistributionList(this);
    sBattlePayMgr->SendBattlePayPurchaseList(this);
}
void WorldSession::HandleBattlePayStartPurchase(WorldPacket& packet)
{
    if (packet.size()<9) { packet.rfinish(); return; }
    uint32 product=packet.read<uint32>(),token=packet.read<uint32>();
    ObjectGuid guid;
    packet.ReadGuidMask(guid,2,3,5,1,4,7,0,6);
    packet.ReadGuidBytes(guid,5,3,7,1,4,0,6,2);
    if (packet.rpos()!=packet.size()) { packet.rfinish(); return; }
    sBattlePayMgr->StartPurchase(this,product,token,uint64(guid));
}
void WorldSession::HandleBattlePayConfirmPurchaseResponse(WorldPacket& packet)
{
    if (packet.size()!=13) { packet.rfinish(); return; }
    uint64 price=packet.read<uint64>();
    uint32 token=packet.read<uint32>();
    // Build 18414 encodes acceptance in the high bit (0x80); the remaining
    // seven bits are padding.
    bool confirm=packet.ReadBit();
    if (packet.ReadBits(7)!=0) return;
    sBattlePayMgr->ConfirmPurchase(this,price,token,confirm);
}
