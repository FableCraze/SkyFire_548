#include "BattlePayMgr.h"
#include "BattlePayWallet.h"
#include "Chat.h"
#include "ScriptMgr.h"
#include "WorldSession.h"
#include <sstream>

namespace
{
bool Id(std::string const& text,uint64& value)
{
    if (text.empty()) return false;
    value=0;
    for (char c:text) {if (c<'0' || c>'9' || value>(UINT64_MAX-(c-'0'))/10) return false; value=value*10+c-'0';}
    return value!=0;
}
bool Available(ChatHandler* handler)
{
    if (sBattlePayMgr->IsReady()) return true;
    handler->SendSysMessage("BattlePay unavailable: apply the database migrations."); return false;
}
}
class battlepay_commandscript : public CommandScript
{
public:
    battlepay_commandscript():CommandScript("battlepay_commandscript") { }
    std::vector<ChatCommand> GetCommands() const OVERRIDE
    {
        static std::vector<ChatCommand> wallet={
            {"show",rbac::RBAC_PERM_COMMAND_BATTLEPAY_WALLET,true,&Show,""},
            {"history",rbac::RBAC_PERM_COMMAND_BATTLEPAY_WALLET,true,&History,""},
            {"credit",rbac::RBAC_PERM_COMMAND_BATTLEPAY_WALLET,true,&Credit,""},
            {"debit",rbac::RBAC_PERM_COMMAND_BATTLEPAY_WALLET,true,&Debit,""}};
        static std::vector<ChatCommand> purchases={
            {"show",rbac::RBAC_PERM_COMMAND_BATTLEPAY_PURCHASE,true,&PurchaseShow,""},
            {"retry",rbac::RBAC_PERM_COMMAND_BATTLEPAY_PURCHASE,true,&Retry,""}};
        static std::vector<ChatCommand> children={
            {"balance",rbac::RBAC_PERM_COMMAND_BATTLEPAY_BALANCE,false,&Balance,""},
            {"wallet",rbac::RBAC_PERM_COMMAND_BATTLEPAY_WALLET,true,nullptr,"",wallet},
            {"purchase",rbac::RBAC_PERM_COMMAND_BATTLEPAY_PURCHASE,true,nullptr,"",purchases}};
        return {{"battlepay",rbac::RBAC_PERM_COMMAND_BATTLEPAY,true,nullptr,"",children}};
    }
    static bool Balance(ChatHandler* handler,char const*)
    {
        if (!handler->GetSession() || !Available(handler)) return false;
        sBattlePayMgr->SendPointsBalance(handler->GetSession()); return true;
    }
    static bool Show(ChatHandler* handler,char const* args)
    {
        uint64 account,balance;
        if (!Available(handler) || !Id(args,account) || account>UINT32_MAX || !BattlePayWallet::Balance(uint32(account),balance)) return false;
        handler->PSendSysMessage("Account %u: %s credits.",uint32(account),BattlePay::FormatCredits(balance).c_str()); return true;
    }
    static bool History(ChatHandler* handler,char const* args)
    {
        std::istringstream in(args); std::string accountText,pageText,extra;
        uint64 account,page=1;
        if (!Available(handler) || !(in>>accountText) || !Id(accountText,account) || account>UINT32_MAX) return false;
        if (in>>pageText) if (!Id(pageText,page) || page>1000000) return false;
        if (in>>extra) return false;
        auto rows=LoginDatabase.PQuery("SELECT id,operationId,kind,amount,balanceBefore,balanceAfter,reason,actorAccountId,purchaseId,createdAt FROM battlepay_wallet_movement WHERE accountId=%u ORDER BY id DESC LIMIT 20 OFFSET %u",uint32(account),uint32((page-1)*20));
        if (!rows) {handler->SendSysMessage("No movements on this page."); return true;}
        do
        {
            auto f=rows->Fetch();
            handler->PSendSysMessage("%s | %s | %s credits | %s -> %s | actor %u | purchase " UI64FMTD " | %s | operation %s",
                f[9].GetString().c_str(),f[2].GetString().c_str(),BattlePay::FormatCredits(f[3].GetUInt64()).c_str(),
                BattlePay::FormatCredits(f[4].GetUInt64()).c_str(),BattlePay::FormatCredits(f[5].GetUInt64()).c_str(),
                f[7].GetUInt32(),f[8].GetUInt64(),f[6].GetString().c_str(),f[1].GetString().c_str());
        } while (rows->NextRow()); return true;
    }
    static bool Adjust(ChatHandler* handler,char const* args,bool credit)
    {
        std::istringstream in(args); std::string accountText,amountText,reason; uint64 account,amount;
        if (!Available(handler) || !(in>>accountText>>amountText) || !Id(accountText,account) || account>UINT32_MAX ||
            !BattlePay::ParseCredits(amountText,amount) || !amount) return false;
        std::getline(in>>std::ws,reason);
        if (reason.empty() || reason.size()>255) return false;
        uint32 actor=handler->GetSession()?handler->GetSession()->GetAccountId():0;
        std::string operation=BattlePayWallet::OperationId();
        auto result=BattlePayWallet::Change(uint32(account),amount,credit,reason,actor,operation);
        if (result==DatabaseCommitResult::Indeterminate)
        {
            // A failed reconciliation cannot prove the original COMMIT rolled back.
            auto retry=BattlePayWallet::Change(uint32(account),amount,credit,reason,actor,operation);
            result=retry==DatabaseCommitResult::Committed?retry:DatabaseCommitResult::Indeterminate;
        }
        handler->PSendSysMessage("Operation %s: %s.",operation.c_str(),result==DatabaseCommitResult::Committed?"committed":
            result==DatabaseCommitResult::Indeterminate?"indeterminate; consult history before issuing another adjustment":"failed; balance unchanged");
        return result==DatabaseCommitResult::Committed;
    }
    static bool Credit(ChatHandler* h,char const* a) {return Adjust(h,a,true);}
    static bool Debit(ChatHandler* h,char const* a) {return Adjust(h,a,false);}
    static bool PurchaseShow(ChatHandler* handler,char const* args)
    {
        uint64 id; BattlePay::Purchase p;
        if (!Available(handler) || !Id(args,id) || !BattlePayWallet::Load(id,p)) return false;
        handler->PSendSysMessage("Purchase " UI64FMTD ": account %u, realm %u, character %u, product %u, %s credits, state %u (0=quoted,1=paid,2=delivered,3=cancelled,4=refunded).",
            id,p.Account,p.Realm,p.Target,p.Product,BattlePay::FormatCredits(p.Price).c_str(),uint32(p.State)); return true;
    }
    static bool Retry(ChatHandler* handler,char const* args)
    {
        uint64 id;
        if (!Available(handler) || !Id(args,id)) return false;
        bool done=sBattlePayMgr->Deliver(id);
        handler->SendSysMessage(done?"Purchase reconciled.":"Purchase pending or belongs to another realm; check logs."); return done;
    }
};
void AddSC_battlepay_commandscript() {new battlepay_commandscript();}
