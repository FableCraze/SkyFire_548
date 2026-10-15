#include "BattlePayMgr.h"
#include "BattlePayWallet.h"
#include "ServiceBoost.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "BattlePetMgr.h"
#include "Item.h"
#include "Mail.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "WorldSession.h"
#include <algorithm>
#include <openssl/rand.h>

namespace
{
WorldPacket PurchaseUpdate(BattlePay::Purchase const& purchase,uint32 status,uint32 result)
{
    std::string wallet=status==2?"":"Account";
    WorldPacket data(SMSG_BATTLE_PAY_PURCHASE_UPDATE);
    data.WriteBits(1,19);
    data.WriteBits(wallet.size(),8);
    data.FlushBits();
    data<<uint32(result)<<uint64(purchase.Id)<<uint32(purchase.Product)<<uint32(status);
    data.WriteString(wallet);
    return data;
}
std::string N(uint64 value) { return std::to_string(value); }
std::string Local(std::string const& base,std::array<std::string,12> const& localized,int locale)
{ return locale>0 && locale<12 && !localized[locale].empty()?localized[locale]:base; }
bool TablesPresent(DatabaseWorkerPool<WorldDatabaseConnection>& db)
{
    auto rows=db.Query("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name IN ('battle_pay_product','battle_pay_product_items','battle_pay_group','battle_pay_entry','locales_battle_pay_product','locales_battle_pay_group','locales_battle_pay_entry')");
    return rows && (*rows)[0].GetUInt32()==7;
}
bool OwnsItem(WorldSession* session,uint32 itemId)
{
    auto species=sBattlePetItemXSpeciesStore.find(itemId);
    if (species!=sBattlePetItemXSpeciesStore.end())
    {
        if (Player* player=session->GetPlayer()) return player->GetBattlePetMgr()->GetBattlePetCount(species->second)!=0;
        return bool(CharacterDatabase.PQuery("SELECT id FROM account_battle_pet WHERE accountId=%u AND species=%u LIMIT 1",session->GetAccountId(),uint32(species->second)));
    }
    auto item=sObjectMgr->GetItemTemplate(itemId);
    if (!item) return false;
    for (auto const& itemSpell:item->Spells)
        if (auto spell=sSpellMgr->GetSpellInfo(itemSpell.SpellId))
            if (spell->IsAbilityOfSkillType(SKILL_MOUNTS))
            {
                if (Player* player=session->GetPlayer()) return player->HasSpell(itemSpell.SpellId);
                return bool(CharacterDatabase.PQuery("SELECT cs.spell FROM character_spell cs JOIN characters c ON c.guid=cs.guid WHERE c.account=%u AND cs.spell=%u LIMIT 1",session->GetAccountId(),itemSpell.SpellId));
            }
    return false;
}
}
BattlePayMgr* BattlePayMgr::Instance() { static BattlePayMgr manager; return &manager; }
void BattlePayMgr::Configure()
{
    _enabled=sConfigMgr->GetBoolDefault("BattlePay.StoreEnabled",false);
    _purchases=sConfigMgr->GetBoolDefault("BattlePay.PurchasesEnabled",false);
    _currency=sConfigMgr->GetIntDefault("BattlePay.Currency",16);
    if (_currency!=1 && _currency!=2 && _currency!=3 && _currency!=4 && _currency!=5 &&
        _currency!=8 && _currency!=9 && _currency!=10 && _currency!=11 && _currency!=12 &&
        _currency!=14 && _currency!=15 && _currency!=16) _currency=16;
}
bool BattlePayMgr::InitializePersistence()
{
    auto delivery=CharacterDatabase.Query("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name IN ('battlepay_delivery','battlepay_delivery_mail')");
    _ready=BattlePayWallet::Ready() && delivery && (*delivery)[0].GetUInt32()==2;
    return _ready;
}
bool BattlePayMgr::LoadFromDb()
{
    Configure();
    if (!InitializePersistence() || !TablesPresent(WorldDatabase))
    {
        _ready=false;
        SF_LOG_ERROR("server.loading","BattlePay unavailable: apply the auth, characters and world migrations.");
        return false;
    }
    auto catalog=std::make_shared<BattlePayCatalog>();
    auto rows=WorldDatabase.Query("SELECT id,title,description,icon,price,discount,displayId,type,choiceType,flags,flagsInfo FROM battle_pay_product ORDER BY id");
    if (rows) do
    {
        auto f=rows->Fetch(); BattlePayProduct p;
        p.Id=f[0].GetUInt32(); p.Title=f[1].GetString(); p.Description=f[2].GetString(); p.Icon=f[3].GetUInt32();
        p.Discount=f[5].GetUInt32(); p.DisplayId=f[6].GetUInt32(); p.Type=f[7].GetUInt8();
        p.ChoiceType=f[8].GetUInt8(); p.Flags=f[9].GetUInt32(); p.FlagsInfo=f[10].GetUInt32();
        if (!p.Id || !BattlePay::ParseCredits(f[4].GetString(),p.Price) || p.Title.empty() ||
            p.Title.size()>50 || p.Description.size()>500 || p.Discount>99 || p.ChoiceType>3 ||
            (p.Type!=0 && !(p.Type==2 && p.Id==BattlePay::BoostProduct)))
        { SF_LOG_ERROR("sql.sql","BattlePay: invalid product %u skipped.",p.Id); continue; }
        catalog->Products.emplace(p.Id,std::move(p));
    } while (rows->NextRow());
    rows=WorldDatabase.Query("SELECT id,itemId,count,productId FROM battle_pay_product_items ORDER BY id");
    if (rows) do
    {
        auto f=rows->Fetch(); auto p=catalog->Products.find(f[3].GetUInt32());
        if (p==catalog->Products.end()) continue;
        uint32 item=f[1].GetUInt32(),count=f[2].GetUInt32();
        if (!sObjectMgr->GetItemTemplate(item) || !count)
        { SF_LOG_ERROR("sql.sql","BattlePay: product %u contains an invalid item; product removed.",p->first); catalog->Products.erase(p); continue; }
        p->second.Items.push_back({f[0].GetUInt32(),item,count});
    } while (rows->NextRow());
    for (auto i=catalog->Products.begin();i!=catalog->Products.end();)
    {
        uint64 stacks=0;
        for (auto const& item:i->second.Items) stacks+=(uint64(item.Count)+sObjectMgr->GetItemTemplate(item.ItemId)->GetMaxStackSize()-1)/sObjectMgr->GetItemTemplate(item.ItemId)->GetMaxStackSize();
        if ((i->second.Type==0 && i->second.Items.empty()) || stacks>4096) i=catalog->Products.erase(i); else ++i;
    }
    rows=WorldDatabase.Query("SELECT id,idx,name,icon,type FROM battle_pay_group ORDER BY idx,id");
    if (rows) do
    {
        auto f=rows->Fetch(); BattlePayGroup g;
        g.Id=f[0].GetUInt32(); g.Index=f[1].GetInt32(); g.Name=f[2].GetString(); g.Icon=f[3].GetUInt32(); g.Type=f[4].GetUInt8();
        if (g.Id && !g.Name.empty() && g.Name.size()<=16 && g.Type<=1) catalog->Groups.push_back(std::move(g));
    } while (rows->NextRow());
    rows=WorldDatabase.Query("SELECT id,productId,groupId,idx,title,description,icon,displayId,banner,flags FROM battle_pay_entry ORDER BY idx,id");
    if (rows) do
    {
        auto f=rows->Fetch(); BattlePayEntry e;
        e.Id=f[0].GetUInt32(); e.ProductId=f[1].GetUInt32(); e.GroupId=f[2].GetUInt32(); e.Index=f[3].GetInt32();
        e.Title=f[4].GetString(); e.Description=f[5].GetString(); e.Icon=f[6].GetUInt32(); e.DisplayId=f[7].GetUInt32(); e.Banner=f[8].GetUInt8(); e.Flags=f[9].GetUInt32();
        if (e.Title.size()<=50 && e.Description.size()<=500 && e.Banner<=3 && catalog->Products.count(e.ProductId) &&
            std::any_of(catalog->Groups.begin(),catalog->Groups.end(),[&](BattlePayGroup const& g){return g.Id==e.GroupId;})) catalog->Entries.push_back(std::move(e));
    } while (rows->NextRow());
    for (std::string const& kind:{std::string("product"),std::string("entry"),std::string("group")})
    {
        std::string query="SELECT id";
        for (int i=1;i<12;++i) query+=kind=="group"?",name_loc"+N(i):",title_loc"+N(i)+",description_loc"+N(i);
        query+=" FROM locales_battle_pay_"+kind;
        rows=WorldDatabase.Query(query.c_str());
        if (rows) do
        {
            auto f=rows->Fetch(); uint32 id=f[0].GetUInt32();
            for (int loc=1;loc<12;++loc)
            {
                std::string title=f[kind=="group"?loc:loc*2-1].GetString();
                std::string description=kind=="group"?"":f[loc*2].GetString();
                if (title.size()>(kind=="group"?16u:50u) || description.size()>500) continue;
                if (kind=="product" && catalog->Products.count(id)) { catalog->Products[id].Titles[loc]=title; catalog->Products[id].Descriptions[loc]=description; }
                if (kind=="entry") for (auto& e:catalog->Entries) if (e.Id==id) {e.Titles[loc]=title; e.Descriptions[loc]=description;}
                if (kind=="group") for (auto& g:catalog->Groups) if (g.Id==id) g.Names[loc]=title;
            }
        } while (rows->NextRow());
    }
    // Remove products which aren't actually offered, preventing hidden-product purchases.
    for (auto i=catalog->Products.begin();i!=catalog->Products.end();)
        if (!std::any_of(catalog->Entries.begin(),catalog->Entries.end(),[&](BattlePayEntry const& e){return e.ProductId==i->first;})) i=catalog->Products.erase(i); else ++i;
    _catalog=std::move(catalog); _ready=true;
    SF_LOG_INFO("server.loading","BattlePay loaded: %u products, %u groups, %u entries.",uint32(_catalog->Products.size()),uint32(_catalog->Groups.size()),uint32(_catalog->Entries.size()));
    return true;
}
void BattlePayMgr::SendBattlePayProductList(WorldSession* session)
{
    WorldPacket data(SMSG_BATTLE_PAY_GET_PRODUCT_LIST_RESPONSE);
    if (!IsStoreEnabled())
    {
        data.WriteBits(0,19); data.WriteBits(0,19); data.WriteBits(0,20); data.FlushBits();
        data<<uint32(0)<<uint32(1); session->SendPacket(&data); return;
    }
    // Serialization order follows the Torghast 18414 packet, including optional bits.
    BattlePayCatalog c=*_catalog;
    int loc=session->GetSessionDbLocaleIndex();
    for (auto& pair:c.Products) {auto& p=pair.second; p.Title=Local(p.Title,p.Titles,loc); p.Description=Local(p.Description,p.Descriptions,loc);}
    for (auto& e:c.Entries) {e.Title=Local(e.Title,e.Titles,loc); e.Description=Local(e.Description,e.Descriptions,loc);}
    for (auto& g:c.Groups) g.Name=Local(g.Name,g.Names,loc);
    data.WriteBits(c.Entries.size(),19); data.WriteBits(c.Products.size(),19);
    for (auto const& pair:c.Products)
    {
        auto const& p=pair.second;
        data.WriteBits(p.ChoiceType,2); data.WriteBits(p.Items.size(),20);
        for (auto const& item:p.Items)
        {
            data.WriteBit(false); data.WriteBit(p.Type==2?session->HasBoost():OwnsItem(session,item.ItemId)); data.WriteBit(false); data.WriteBit(false);
        }
        data.WriteBit(!p.Title.empty());
        if (!p.Title.empty())
        {
            data.WriteBit(p.DisplayId!=0); data.WriteBits(p.Title.size(),10); data.WriteBits(p.Description.size(),13);
            data.WriteBits(0,10); data.WriteBit(p.FlagsInfo!=0); data.WriteBit(false); data.WriteBit(p.Icon!=0);
        }
    }
    data.WriteBits(c.Groups.size(),20);
    for (auto const& e:c.Entries)
    {
        data.WriteBit(!e.Title.empty());
        if (!e.Title.empty()) {data.WriteBits(e.Title.size(),10); data.WriteBit(false); data.WriteBits(0,10); data.WriteBits(e.Description.size(),13); data.WriteBit(e.DisplayId!=0); data.WriteBit(e.Icon!=0); data.WriteBit(false);}
    }
    for (auto const& g:c.Groups) data.WriteBits(g.Name.size(),8);
    data.FlushBits();
    for (auto const& g:c.Groups) {data<<uint32(g.Index); data.WriteString(g.Name); data<<uint8(g.Type)<<uint32(g.Icon)<<uint32(g.Id);}
    for (auto const& pair:c.Products)
    {
        auto const& p=pair.second; data<<uint8(p.Type);
        for (auto const& item:p.Items) data<<uint32(item.ItemId)<<uint32(item.Count)<<uint32(item.Id);
        if (!p.Title.empty())
        {
            if (p.DisplayId) data<<uint32(p.DisplayId);
            if (p.Icon) data<<uint32(p.Icon);
            data.WriteString(""); data.WriteString(p.Title);
            if (p.FlagsInfo) data<<uint32(p.FlagsInfo);
            data.WriteString(p.Description);
        }
        uint32 flags=(!CanPurchase() || (p.Type==2 && session->HasBoost()))?5:p.Flags;
        data<<uint32(p.Id)<<uint32(flags)<<uint64(p.Price)<<uint64(BattlePay::DiscountPrice(p.Price,p.Discount));
    }
    for (auto const& e:c.Entries)
    {
        if (!e.Title.empty())
        {
            data.WriteString(""); if (e.Icon) data<<uint32(e.Icon); if (e.DisplayId) data<<uint32(e.DisplayId);
            data.WriteString(e.Description); data.WriteString(e.Title);
        }
        data<<uint32(e.Flags)<<uint8(e.Banner)<<uint32(e.Id)<<int32(e.Index)<<uint32(e.GroupId)<<uint32(e.ProductId);
    }
    data<<uint32(_currency)<<uint32(0); session->SendPacket(&data);
}
void BattlePayMgr::SendBattlePayDistributionList(WorldSession* session)
{
    WorldPacket data(SMSG_BATTLE_PAY_GET_DISTRIBUTION_LIST_RESPONSE);
    data<<uint32(0); data.WriteBits(0,19); data.FlushBits(); session->SendPacket(&data);
    // Refreshing the list must not remove an already purchased boost entitlement.
    if (session->HasBoost())
        session->SendBattlePayDistributionUpdate(0,CHARACTER_BOOST,CHARACTER_BOOST_ALLOW,
            CHARACTER_BOOST_TEXT_ID,CHARACTER_BOOST_BONUS_TEXT,CHARACTER_BOOST_BONUS_TEXT2);
}
void BattlePayMgr::SendBattlePayPurchaseList(WorldSession* session)
{
    // C_PurchaseAPI.HasPurchaseList() must be initialized with the actual list
    // response. The non-empty layout is NOT the PURCHASE_UPDATE layout.
    WorldPacket ready(SMSG_BATTLE_PAY_GET_PURCHASE_LIST_RESPONSE,7);
    ready.WriteBits(0,19);
    ready.FlushBits();
    ready<<uint32(0);
    session->SendPacket(&ready);
    if (_ready)
    {
        auto rows=LoginDatabase.PQuery("SELECT id FROM battlepay_purchase WHERE accountId=%u AND realmId=%u AND state=2 ORDER BY id DESC LIMIT 100",session->GetAccountId(),uint32(realmID));
        if (rows) do
        {
            BattlePay::Purchase purchase;
            if (!BattlePayWallet::Load((*rows)[0].GetUInt64(),purchase) || purchase.State!=BattlePay::PurchaseState::Delivered) continue;
            WorldPacket update=PurchaseUpdate(purchase,6,0);
            session->SendPacket(&update);
        } while (rows->NextRow());
    }
}
void BattlePayMgr::SendPointsBalance(WorldSession* session)
{
    uint64 balance;
    if (_ready && BattlePayWallet::Balance(session->GetAccountId(),balance))
        ChatHandler(session).PSendSysMessage("BattlePay balance: %s credits.",BattlePay::FormatCredits(balance).c_str());
}
bool BattlePayMgr::ValidateTarget(BattlePay::Purchase const& p)
{
    if (p.Type==2) return p.Product==BattlePay::BoostProduct;
    if (!p.Target) return false;
    auto row=CharacterDatabase.PQuery("SELECT account FROM characters WHERE guid=%u AND realm=%u",p.Target,p.Realm);
    return row && (*row)[0].GetUInt32()==p.Account;
}
void BattlePayMgr::StartPurchase(WorldSession* session,uint32 product,uint32 token,uint64 target)
{
    auto& ctx=session->GetBattlePayContext();
    if (ctx.Phase!=BattlePayContext::Idle) return;
    ctx=BattlePayContext(); ctx.ClientToken=token;
    auto c=_catalog;
    if (!CanPurchase() || !c || !c->Products.count(product)) {Reject(session,25); return;}
    auto const& p=c->Products.at(product);
    if (p.Flags==5 || (p.Type==2 && session->HasBoost())) {Reject(session,3); return;}
    if (p.Type==0 && target && !IS_PLAYER_GUID(target)) {Reject(session,3); return;}
    auto& quote=ctx.Purchase;
    quote.Account=session->GetAccountId(); quote.Realm=uint32(realmID); quote.Target=GUID_LOPART(target);
    if (!quote.Target && session->GetPlayer()) quote.Target=session->GetPlayer()->GetGUIDLow();
    quote.Product=p.Id; quote.Type=p.Type; quote.Price=BattlePay::DiscountPrice(p.Price,p.Discount);
    int loc=session->GetSessionDbLocaleIndex(); quote.Title=Local(p.Title,p.Titles,loc); quote.Description=Local(p.Description,p.Descriptions,loc); quote.Items=p.Items;
    if (!ValidateTarget(quote)) {Reject(session,3); return;}
    std::string operation=BattlePayWallet::OperationId();
    auto result=BattlePayWallet::Quote(quote,operation);
    if (result!=DatabaseCommitResult::Committed)
    {
        // Reusing this operation ID safely reconciles a lost quote COMMIT response.
        if (result!=DatabaseCommitResult::Indeterminate || BattlePayWallet::Quote(quote,operation)!=DatabaseCommitResult::Committed) {Reject(session,3); return;}
    }
    if (RAND_bytes(reinterpret_cast<unsigned char*>(&ctx.ServerToken),sizeof(ctx.ServerToken))!=1 || !ctx.ServerToken) {Reject(session,3); return;}
    ctx.Phase=BattlePayContext::Start; ctx.Timer=500;
}
void BattlePayMgr::SendUpdate(WorldSession* session,uint32 status,uint32 result)
{
    auto const& ctx=session->GetBattlePayContext();
    WorldPacket data=PurchaseUpdate(ctx.Purchase,status,result);
    session->SendPacket(&data);
}
void BattlePayMgr::Reject(WorldSession* session,uint32 result)
{
    auto& ctx=session->GetBattlePayContext();
    SendUpdate(session,3,result);
    if (_ready && ctx.Purchase.Id) BattlePayWallet::Cancel(ctx.Purchase.Id,session->GetAccountId());
    ctx=BattlePayContext();
}
void BattlePayMgr::ConfirmPurchase(WorldSession* session,uint64 price,uint32 token,bool confirm)
{
    auto& ctx=session->GetBattlePayContext();
    if (ctx.Phase!=BattlePayContext::Confirmation) return;
    if (!confirm || token!=ctx.ServerToken || price!=ctx.Purchase.Price || ctx.Purchase.Account!=session->GetAccountId() || ctx.Age>=60000)
    {Reject(session,4); return;}
    if (!CanPurchase() || !ValidateTarget(ctx.Purchase)) {Reject(session,25); return;}
    ctx.Phase=BattlePayContext::Charge; ctx.Timer=8000;
}
void BattlePayMgr::UpdateSession(WorldSession* session,uint32 diff)
{
    auto& ctx=session->GetBattlePayContext();
    if (ctx.Phase==BattlePayContext::Idle) return;
    ctx.Age=std::min<uint32>(60000,ctx.Age+std::min<uint32>(diff,60000));
    if (ctx.Phase<=BattlePayContext::Confirmation && ctx.Age>=60000) {Reject(session,4); return;}
    if (ctx.Phase==BattlePayContext::Confirmation) return;
    if (ctx.Timer>diff) {ctx.Timer-=diff; return;}
    if (ctx.Phase==BattlePayContext::Start)
    {
        WorldPacket data(SMSG_BATTLE_PAY_START_PURCHASE_RESPONSE); data<<uint64(ctx.Purchase.Id)<<uint32(1)<<uint32(ctx.ClientToken); session->SendPacket(&data);
        SendUpdate(session,2,0); ctx.Phase=BattlePayContext::Allow; ctx.Timer=500;
    }
    else if (ctx.Phase==BattlePayContext::Allow)
    {
        uint64 balance;
        if (!CanPurchase() || !BattlePayWallet::Balance(session->GetAccountId(),balance) || balance<ctx.Purchase.Price) {Reject(session,3); return;}
        SendUpdate(session,3,0);
        WorldPacket data(SMSG_BATTLE_PAY_CONFIRM_PURCHASE); data<<uint32(ctx.ServerToken)<<uint64(ctx.Purchase.Price)<<uint64(ctx.Purchase.Id); session->SendPacket(&data);
        ctx.Phase=BattlePayContext::Confirmation;
    }
    else if (ctx.Phase==BattlePayContext::Charge)
    {
        if (!CanPurchase() || !ValidateTarget(ctx.Purchase)) {Reject(session,25); return;}
        auto result=BattlePayWallet::Pay(ctx.Purchase);
        if (result==DatabaseCommitResult::Failed) {Reject(session,3); return;}
        ctx.Phase=BattlePayContext::Reconcile; ctx.Timer=1000;
        if (result==DatabaseCommitResult::Committed) SendUpdate(session,5,0);
    }
    else
    {
        BattlePay::Purchase persistent;
        if (!BattlePayWallet::Load(ctx.Purchase.Id,persistent)) {ctx.Timer=5000; return;}
        if (persistent.State==BattlePay::PurchaseState::Quoted) {Reject(session,3); return;}
        if (persistent.State==BattlePay::PurchaseState::Paid) {Deliver(persistent.Id); ctx.Timer=1000; return;}
        if (persistent.State!=BattlePay::PurchaseState::Delivered) {Reject(session,4); return;}
        if (persistent.Type==2) BattlePay::NotifyBoostPurchased(session);
        WorldPacket data(SMSG_BATTLE_PAY_DELIVERY_ENDED);
        data.WriteBits(persistent.Items.size(),22); data.FlushBits();
        for (auto const& item:persistent.Items) data<<uint32(item.ItemId);
        data<<uint64(persistent.Id); session->SendPacket(&data);
        SendUpdate(session,6,0); SendPointsBalance(session); ctx=BattlePayContext();
    }
}
bool BattlePayMgr::Deliver(uint64 id)
{
    BattlePay::Purchase p;
    if (!_ready || !BattlePayWallet::Load(id,p) || p.Realm!=uint32(realmID)) return false;
    if (p.State==BattlePay::PurchaseState::Delivered || p.State==BattlePay::PurchaseState::Refunded) return true;
    if (p.State!=BattlePay::PurchaseState::Paid || p.Type!=0) return false;
    std::vector<std::unique_ptr<Item>> items;
    std::vector<std::unique_ptr<Mail>> mails;
    bool rejected=false;
    auto result=CharacterDatabase.WithTransaction([&](CheckedTransaction& tx)
    {
        // The receipt also serializes competing recovery attempts across processes.
        auto receipt=tx.Query("SELECT result FROM battlepay_delivery WHERE purchaseId="+N(id)+" FOR UPDATE");
        if (receipt) {rejected=(*receipt)[0].GetUInt8()==2; return true;}
        auto receiver=tx.Query("SELECT account,realm FROM characters WHERE guid="+N(p.Target)+" FOR UPDATE");
        rejected=!receiver || (*receiver)[0].GetUInt32()!=p.Account || (*receiver)[1].GetUInt32()!=p.Realm;
        if (!tx.Execute("INSERT INTO battlepay_delivery (purchaseId,accountId,targetGuid,result) VALUES ("+N(id)+","+N(p.Account)+","+N(p.Target)+","+(rejected?"2":"1")+")")) return false;
        if (rejected) return true;
        for (auto const& line:p.Items)
        {
            auto itemTemplate=sObjectMgr->GetItemTemplate(line.ItemId);
            if (!itemTemplate || !line.Count) return false;
            uint32 remaining=line.Count;
            while (remaining)
            {
                uint32 count=std::min(remaining,itemTemplate->GetMaxStackSize());
                std::unique_ptr<Item> item(Item::CreateItem(line.ItemId,count,nullptr));
                if (!item || items.size()>=4096) return false;
                if (items.size()%MAX_MAIL_ITEMS==0)
                {
                    std::unique_ptr<Mail> mail(new Mail());
                    mail->messageID=sObjectMgr->GenerateMailID(); mail->messageType=MAIL_NORMAL; mail->stationery=MAIL_STATIONERY_GM;
                    mail->receiver=p.Target; mail->subject=p.Title; mail->body=p.Description;
                    mail->deliver_time=time(nullptr); mail->expire_time=mail->deliver_time+180*DAY;
                    mail->checked=MAIL_CHECK_MASK_HAS_BODY|MAIL_CHECK_MASK_RETURNED; mail->state=MailState::MAIL_STATE_UNCHANGED;
                    mails.push_back(std::move(mail));
                }
                SQLTransaction statements=CharacterDatabase.BeginTransaction(); item->SaveToDB(statements);
                if (!tx.Execute(statements)) return false;
                Mail& mail=*mails.back(); mail.AddItem(item->GetGUIDLow(),item->GetEntry());
                auto stmt=CharacterDatabase.GetPreparedStatement(CHAR_INS_MAIL_ITEM);
                stmt->setUInt32(0,mail.messageID); stmt->setUInt32(1,item->GetGUIDLow()); stmt->setUInt32(2,p.Target);
                statements=CharacterDatabase.BeginTransaction(); statements->Append(stmt);
                if (!tx.Execute(statements)) return false;
                items.push_back(std::move(item)); remaining-=count;
            }
        }
        if (items.empty()) return false;
        for (auto const& mail:mails)
        {
            if (!tx.Execute("INSERT INTO battlepay_delivery_mail (purchaseId,mailId) VALUES ("+N(id)+","+N(mail->messageID)+")")) return false;
            auto stmt=CharacterDatabase.GetPreparedStatement(CHAR_INS_MAIL);
            stmt->setUInt32(0,mail->messageID); stmt->setUInt8(1,mail->messageType); stmt->setInt8(2,mail->stationery); stmt->setUInt16(3,0);
            stmt->setUInt32(4,0); stmt->setUInt32(5,p.Target); stmt->setString(6,mail->subject); stmt->setString(7,mail->body); stmt->setBool(8,true);
            stmt->setUInt64(9,mail->expire_time); stmt->setUInt64(10,mail->deliver_time); stmt->setUInt32(11,0); stmt->setUInt32(12,0); stmt->setUInt8(13,mail->checked);
            SQLTransaction statements=CharacterDatabase.BeginTransaction(); statements->Append(stmt);
            if (!tx.Execute(statements)) return false;
        }
        return true;
    });
    if (result!=DatabaseCommitResult::Committed)
    {
        SF_LOG_ERROR("battlepay","Purchase " UI64FMTD " delivery pending reconciliation (result %u).",id,unsigned(result)); return false;
    }
    if (!rejected)
    {
        Player* player=sObjectMgr->GetPlayerByLowGUID(p.Target);
        if (player) player->RefreshBattlePayMail(p.Id);
    }
    auto completion=rejected?BattlePayWallet::Refund(p):BattlePayWallet::Complete(p.Id);
    if (completion!=DatabaseCommitResult::Committed) SF_LOG_ERROR("battlepay","Purchase " UI64FMTD " awaits auth reconciliation.",id);
    return completion==DatabaseCommitResult::Committed;
}
void BattlePayMgr::Update(uint32 diff)
{
    if (!_ready) return;
    if (_recoveryTimer>diff) {_recoveryTimer-=diff; return;}
    _recoveryTimer=5000;
    static uint64 cursor=0;
    auto rows=LoginDatabase.PQuery("SELECT id FROM battlepay_purchase WHERE realmId=%u AND state=1 AND id>" UI64FMTD " ORDER BY id LIMIT 20",uint32(realmID),cursor);
    if (rows) do {cursor=(*rows)[0].GetUInt64(); Deliver(cursor);} while (rows->NextRow());
    else cursor=0; // Rotate instead of starving later purchases behind a broken product.
    LoginDatabase.PExecute("UPDATE battlepay_purchase SET state=3 WHERE realmId=%u AND state=0 AND createdAt<CURRENT_TIMESTAMP-INTERVAL 2 MINUTE",uint32(realmID));
}
