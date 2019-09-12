/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */

#include <token.hpp>

namespace eosio
{

static constexpr uint32_t seconds_per_day    = 24 * 3600;
static constexpr uint32_t refund_delay       = 3 * seconds_per_day;

void token::create(name issuer,
                   asset maximum_supply)
{
   require_auth(_self);

   auto sym = maximum_supply.symbol;
   check(sym.is_valid(), "invalid symbol name");
   check(maximum_supply.is_valid(), "invalid supply");
   check(maximum_supply.amount > 0, "max-supply must be positive");

   stats statstable(_self, sym.code().raw());
   auto existing = statstable.find(sym.code().raw());

   check(existing == statstable.end(), "token with symbol already exists");
   
   statstable.emplace(_self, [&](auto &s) {
      s.supply.symbol = maximum_supply.symbol;
      s.max_supply = maximum_supply;
      s.issuer = issuer;
   });
}

void token::update(name issuer,
                   asset maximum_supply)
{
   require_auth(_self);

   auto sym = maximum_supply.symbol;
   check(sym.is_valid(), "invalid symbol name");
   check(maximum_supply.is_valid(), "invalid supply");
   check(maximum_supply.amount > 0, "max-supply must be positive");

   stats statstable(_self, sym.code().raw());
   auto existing = statstable.find(sym.code().raw());
   
   check(existing != statstable.end(), "token with symbol doesn't exists");

   const auto& st = *existing;

   check(st.supply.amount <= maximum_supply.amount, "max_supply must be larger that available supply");
   check(maximum_supply.symbol == st.supply.symbol, "symbol precission mismatch");
   
   statstable.modify(st, same_payer, [&](auto &s) {
      s.max_supply = maximum_supply;
      s.issuer = issuer;
   });
}

void token::issue(name to, asset quantity, string memo)
{
   auto sym = quantity.symbol;
   check(sym.is_valid(), "invalid symbol name");
   check(memo.size() <= 256, "memo has more than 256 bytes");

   stats statstable(_self, sym.code().raw());
   auto existing = statstable.find(sym.code().raw());
   check(existing != statstable.end(), "token with symbol does not exist, create token before issue");
   const auto &st = *existing;

   require_auth(st.issuer);
   check(quantity.is_valid(), "invalid quantity");
   check(quantity.amount > 0, "must issue positive quantity");

   check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
   check(quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

   statstable.modify(st, same_payer, [&](auto &s) {
      s.supply += quantity;
   });

   add_balance(st.issuer, quantity, st.issuer, true);

   if (to != st.issuer)
   {
      SEND_INLINE_ACTION(*this, transfer, {{st.issuer, "active"_n}},
                         {st.issuer, to, quantity, memo});
   }

   validate_peos_team_vesting(to, quantity);
}

void token::retire(asset quantity, string memo)
{
   auto sym = quantity.symbol;
   check(sym.is_valid(), "invalid symbol name");
   check(memo.size() <= 256, "memo has more than 256 bytes");

   stats statstable(_self, sym.code().raw());
   auto existing = statstable.find(sym.code().raw());
   check(existing != statstable.end(), "token with symbol does not exist");
   const auto &st = *existing;

   require_auth(st.issuer);
   check(quantity.is_valid(), "invalid quantity");
   check(quantity.amount > 0, "must retire positive quantity");

   check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");

   statstable.modify(st, same_payer, [&](auto &s) {
      s.supply -= quantity;
   });

   sub_balance(st.issuer, quantity);
}

void token::transfer(name from,
                     name to,
                     asset quantity,
                     string memo)
{
   check(from != to, "cannot transfer to self");
   require_auth(from);
   check(is_account(to), "to account does not exist");
   auto sym = quantity.symbol.code();
   stats statstable(_self, sym.raw());
   const auto &st = statstable.get(sym.raw());

   require_recipient(from);
   require_recipient(to);

   check(quantity.is_valid(), "invalid quantity");
   check(quantity.amount > 0, "must transfer positive quantity");
   check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
   check(memo.size() <= 256, "memo has more than 256 bytes");

   auto payer = has_auth(to) ? to : from;

   do_claim(from, sym, from);
   sub_balance(from, quantity);
   add_balance(to, quantity, payer, payer != st.issuer);

   if (from != st.issuer)
   {
      do_claim(to, sym, from);
   }
}

void token::claim(name owner, symbol_code sym)
{
   do_claim(owner, sym, owner);
}

void token::do_claim(name owner, symbol_code sym, name payer)
{
   require_auth(payer);

   check(sym.is_valid(), "Invalid symbol name");

   accounts acnts(_self, owner.value);

   const auto &owner_acc = acnts.get(sym.raw(), "no balance object found");

   if (!owner_acc.claimed)
   {
      auto balance = owner_acc.balance;

      acnts.erase(owner_acc);

      auto replace = acnts.find(sym.raw());
      check(replace == acnts.end(), "There must be no balance object");

      acnts.emplace(payer, [&](auto &a) {
         a.balance = balance;
         a.claimed = true;
      });
   }
}

void token::recover(name owner, symbol_code sym)
{
   check(sym.is_valid(), "invalid symbol name");

   stats statstable(_self, sym.raw());
   auto existing = statstable.find(sym.raw());
   check(existing != statstable.end(), "token with symbol does not exist");
   const auto &st = *existing;

   require_auth(st.issuer);

   accounts acnts(_self, owner.value);

   const auto owner_acc = acnts.find(sym.raw());
   if(owner_acc != acnts.end() && !owner_acc->claimed) {      
      add_balance(st.issuer, owner_acc->balance, st.issuer, true);
      acnts.erase(owner_acc);
   }
}

void token::sub_balance(name owner, asset value)
{
   accounts from_acnts(_self, owner.value);

   const auto &from = from_acnts.get(value.symbol.code().raw(), "no balance object found");
   check(from.balance.amount >= value.amount, "overdrawn balance");

   from_acnts.modify(from, owner, [&](auto &a) {
      a.balance -= value;
      a.claimed = true;
   });
}

void token::add_balance(name owner, asset value, name ram_payer, bool claimed)
{
   accounts to_acnts(_self, owner.value);
   auto to = to_acnts.find(value.symbol.code().raw());
   if (to == to_acnts.end())
   {
      to_acnts.emplace(ram_payer, [&](auto &a) {
         a.balance = value;
         a.claimed = claimed;
      });
   }
   else
   {
      to_acnts.modify(to, same_payer, [&](auto &a) {
         a.balance += value;
      });
   }
}

void token::open(name owner, const symbol &symbol, name ram_payer)
{
   require_auth(ram_payer);

   auto sym_code_raw = symbol.code().raw();

   stats statstable(_self, sym_code_raw);
   const auto &st = statstable.get(sym_code_raw, "symbol does not exist");
   check(st.supply.symbol == symbol, "symbol precision mismatch");

   accounts acnts(_self, owner.value);
   auto it = acnts.find(sym_code_raw);
   if (it == acnts.end())
   {
      acnts.emplace(ram_payer, [&](auto &a) {
         a.balance = asset{0, symbol};
         a.claimed = true;
      });
   }
}

void token::close(name owner, const symbol &symbol)
{
   require_auth(owner);
   accounts acnts(_self, owner.value);
   auto it = acnts.find(symbol.code().raw());
   check(it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect.");
   check(it->balance.amount == 0, "Cannot close because the balance is not zero.");
   acnts.erase(it);
}

void token::validate_peos_team_vesting(name account, asset quantity)
{
   vesting vest_accounts(_self, _self.value);
   auto vest = vest_accounts.find(account.value);
   int64_t claimed = 0;

   if (  
      account == PEOS_MARKETING_ACCOUNT ||
      account == PEOS_TEAMFUND_ACCOUNT ||
      account == PEOS_CONTRACT_ACCOUNT
      )
   {
      if (vest != vest_accounts.end())
      {
         claimed = vest->issued.amount;

         vest_accounts.modify(vest, _self, [&](auto &v) {
            v.issued += quantity;
         });
      }
      else
      {
         vest_accounts.emplace(_self, [&](auto &v) {
            v.account = account;
            v.issued = quantity;
         });
      }
   } 

   if ( account == PEOS_MARKETING_ACCOUNT ) 
   {
      const int64_t claimable = 50'000'000'0000ll;
      check(claimable >= claimed + quantity.amount, "pEOS marketing/operations budget claimed");
   }
   else if ( account == PEOS_TEAMFUND_ACCOUNT )
   {
      const int64_t base_time = 1551096000; // 2019-02-25
      const int64_t max_claimable = 200'000'000'0000ll;
      const int64_t claimable = int64_t(max_claimable * double(now() - base_time) / (400 * seconds_per_day));
      check(claimable >= claimed + quantity.amount, "pEOS team can only issue their tokens over 400 days");
   } 
   else if ( account == PEOS_CONTRACT_ACCOUNT ) 
   { 
      const int64_t claimable = 596'224'1696ll;
      check(claimable >= claimed + quantity.amount, "pEOS token budget for <1.0000 PEOS airdrop accounts and contracts claimed");
   } 
   else
   {
      check(false, "token issuing era finished");
   }
}

#pragma pack(push,1)
struct sign_data {
   uint64_t id;
   checksum256 outputsDigest;
};
#pragma pack(pop)

void token::transferutxo(const name &payer, const std::vector<input> &inputs, const std::vector<output> &outputs, const string &memo) 
{
   utxos utxostable(_self, _self.value);
   require_auth(payer);

   check(memo.size() <= 256, "memo has more than 256 bytes");

   auto p = pack(outputs);
   checksum256 outputsDigest = sha256(&p[0], p.size());

   asset inputSum = asset(0, PEOS_SYMBOL);
   for(auto in = inputs.cbegin() ; in != inputs.cend() ; ++in) {
      sign_data sd = {in->id, outputsDigest};
      checksum256 digest = sha256((const char *)&sd, sizeof(sign_data));

      auto utxo = utxostable.find(in->id);
      check(utxo != utxostable.end(), "Unknown UTXO");
      assert_recover_key(digest, in->sig, utxo->pk);
      inputSum += utxo->amount;

      utxostable.erase(utxo);
   }

   asset outputSum = asset(0, PEOS_SYMBOL);
   for(auto oIter = outputs.cbegin() ; oIter != outputs.cend() ; ++oIter) {
      auto q = oIter->quantity;
      check(q.is_valid(), "Invalid asset");
      check(q.symbol == PEOS_SYMBOL, "Symbol precision mismatch");
      check(q.amount > 0, "Output amount must be positive");
      outputSum += q;

      if (oIter->account.value != 0) 
      {  
         SEND_INLINE_ACTION(*this, transfer, {{_self, "active"_n}}, {_self, oIter->account, q, memo});
      } 
      else 
      {
         utxostable.emplace(payer, [&](auto &u){
            u.id = getNextUTXOId();
            u.pk = oIter->pk;
            u.amount = q;
         });
      }
   }

   check(inputSum >= outputSum, "Inputs don't cover outputs");

   asset fees = inputSum - outputSum;
   if (fees.amount > 0) 
   {  
      SEND_INLINE_ACTION(*this, transfer, {{_self, "active"_n}}, {_self, payer, fees, ""});
   }
}

void token::loadutxo(const name &from, const public_key &pk, const asset &quantity) 
{
   require_auth(from);

   auto sym = quantity.symbol;
   check(sym.is_valid(), "invalid symbol name");
   check(quantity.amount > 0, "Must load utxo with positive quantity");

   stats statstable(_self, sym.code().raw());
   auto existing = statstable.find(sym.code().raw());
   check(existing != statstable.end(), "token with symbol does not exist");
   const auto &st = *existing;

   SEND_INLINE_ACTION(*this, transfer, {{from, "active"_n}}, {from, st.issuer, quantity, ""});

   utxos utxostable(_self, _self.value);

   utxostable.emplace(from, [&](auto &u){
      u.id = getNextUTXOId();
      u.pk = pk;
      u.amount = quantity;
   });
}

uint64_t token::getNextUTXOId() 
{
   utxo_globals globals(_self, _self.value);

   uint64_t ret = 0;

   auto const &it = globals.find(0);
   if (it == globals.end()) 
   {
      globals.emplace(_self, [&](auto &g){
         g.next_pk = 1;
      });
   }
   else 
   {
      globals.modify(it, same_payer, [&](auto &g){
         ret = g.next_pk;
         g.next_pk += 1;
      });
   }

   return ret;
}

void token::realizediv(const name &owner)
{
   require_auth(owner);

   staked owner_staked(_self, owner.value);

   const auto sym = PEOS_SYMBOL.code().raw();
   const auto &stake = owner_staked.find(sym);

   if(stake == owner_staked.end()) 
   {
      return;
   }

   if(stake->quantity.amount == 0)
   {
      return;
   }

   dividends dividend(_self, _self.value);

   double totalDividendFrac = 1.0;
   auto div = dividend.find(sym);
   if (div != dividend.end())
   {
      totalDividendFrac = div->totalDividendFrac;
   }

   double profit = (div->totalDividendFrac - stake->lastDividendsFrac);
   profit *= stake->quantity.amount;

   dividend.modify(div, _self, [&](auto &d) {
      d.totalUnclaimedDividends.amount -= profit;
   });

   owner_staked.modify(stake, owner, [&](auto &s) {
      s.lastDividendsFrac = div->totalDividendFrac;
   });

   if(profit >= 1.0) {
      SEND_INLINE_ACTION(
         *this, 
         transfer, 
         {get_self(), "active"_n}, 
         {get_self(), owner, asset{(int64_t)profit, PEOS_SYMBOL}, std::string("Your dividents from staked PEOS tokens")}
      );
   }
}

void token::stake(const name &owner, asset quantity)
{
   require_auth(owner);

   auto sym = quantity.symbol.code().raw();
   stats statstable(_self, sym);
   const auto &st = statstable.get(sym);

   check(quantity.is_valid(), "invalid quantity");
   check(quantity.amount > 0, "must transfer positive quantity");
   check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");

   realizediv(owner);
   
   staked owner_staked(_self, owner.value);
   const auto &stake = owner_staked.find(sym);
   dividends dividend(_self, _self.value);

   double totalDividendFrac = 1.0;
   auto div = dividend.find(sym);
   if(div != dividend.end()) 
   {
      totalDividendFrac = div->totalDividendFrac;

      dividend.modify(div, _self, [&](auto &d) {
         d.totalStaked += quantity;
      });
   }
   else 
   {
      dividend.emplace(_self, [&](auto &s) {
         s.totalStaked = quantity;
         s.totalDividends = asset{0, PEOS_SYMBOL};
         s.totalUnclaimedDividends = asset{0, PEOS_SYMBOL};

         s.totalDividendFrac = 1.0;
      });
   }

   if(stake != owner_staked.end()) {
      owner_staked.modify(stake, owner, [&](auto &s) {
         s.quantity += quantity;
         check(s.lastDividendsFrac == totalDividendFrac, "Divs not realized");
      });
   }
   else
   {
      owner_staked.emplace(owner, [&](auto &s) {
         s.quantity = quantity;
         s.lastDividendsFrac = totalDividendFrac;
      });
   }

   SEND_INLINE_ACTION(*this, transfer, {owner, "active"_n}, {owner, _self, quantity, "PEOS tokens staked"});
}

void token::unstake(const name &owner, asset quantity)
{
   require_auth(owner);

   realizediv(owner);
   
   staked owner_staked(_self, owner.value);
   const auto sym = quantity.symbol.code().raw();
   const auto &stake = owner_staked.find(sym);

   check(stake != owner_staked.end(), "nothing staked");
   if(stake->quantity <= quantity) 
   {
      quantity = stake->quantity;
      owner_staked.erase(stake);
   }
   else
   {
      owner_staked.modify(stake, owner, [&](auto &s){
         s.quantity -= quantity;
      });
   }
   
   stats statstable(_self, sym);
   const auto &st = statstable.get(sym);

   check(quantity.is_valid(), "invalid quantity");
   check(quantity.amount > 0, "must unstake positive quantity");
   check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");

   dividends dividend(_self, _self.value);
   auto div = dividend.find(sym);

   dividend.modify(div, _self, [&](auto &d) {
      d.totalStaked -= quantity;
   });

   refunds_table refunds_tbl( get_self(), owner.value );
   auto req = refunds_tbl.find( owner.value );

   if (req != refunds_tbl.end()) {
      refunds_tbl.modify( req, owner, [&]( refund_request& r ) {
         r.request_time = now();
         r.amount += quantity;
      }); 
   } else {
      refunds_tbl.emplace( owner, [&]( refund_request& r ) {
         r.owner = owner;
         r.request_time = now();
         r.amount = quantity;
      });
   }
}

void token::refund(const name &owner) {
   require_auth( owner );

   refunds_table refunds_tbl( get_self(), owner.value );
   auto req = refunds_tbl.find( owner.value );
   check( req != refunds_tbl.end(), "refund request not found" );
   check( req->request_time + refund_delay <= now(), "refund is not available yet" );

   SEND_INLINE_ACTION(
      *this, 
      transfer, 
      {get_self(), "active"_n}, 
      {get_self(), owner, req->amount, std::string("Your unstaked PEOS tokens")}
   );
   
   refunds_tbl.erase( req );
}

void token::distribute(const name &owner, asset quantity)
{
   require_auth(owner);
   
   const auto sym = PEOS_SYMBOL.code().raw();

   SEND_INLINE_ACTION(*this, transfer, {owner, "active"_n}, {owner, _self, quantity, ""});

   check(quantity.symbol == PEOS_SYMBOL, "Only distribute PEOS");
   check(quantity.amount > 0, "Can't distribute negative tokens");

   dividends dividend(_self, _self.value);

   auto div = dividend.find(sym);
   if (div == dividend.end())
   {
      dividend.emplace(_self, [&](auto &s) {
         s.totalStaked = asset{0, PEOS_SYMBOL};
         s.totalDividends = asset{0, PEOS_SYMBOL};
         s.totalUnclaimedDividends = quantity;

         s.totalDividendFrac = 1.0;
      });
   }
   else
   {
      dividend.modify(div, get_self(), [&](auto &s) {
         
         s.totalUnclaimedDividends += quantity;
         if (s.totalStaked.amount > 0) 
         {
               s.totalDividends += quantity;
               s.totalDividendFrac += (double)quantity.amount / (double)s.totalStaked.amount;
         }            
      });
   }
}

} // namespace eosio

EOSIO_DISPATCH(eosio::token, (create)(update)(issue)(transfer)(claim)(recover)(retire)(close)(transferutxo)(loadutxo)(stake)(unstake)(realizediv)(refund)(distribute))
