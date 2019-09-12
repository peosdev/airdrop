/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#pragma once

#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>
#include <eosiolib/crypto.hpp>
#include <eosiolib/transaction.hpp>
#include <string>

namespace eosiosystem
{
class system_contract;
}

namespace eosio
{

using std::string;

const eosio::symbol PEOS_SYMBOL = symbol(symbol_code("PEOS"), 4);

class[[eosio::contract("token")]] token : public contract
{
 public:
   using contract::contract;

   [[eosio::action]] void create(name issuer,
                                 asset maximum_supply);

   [[eosio::action]] void update(name issuer,
                   asset maximum_supply);

   [[eosio::action]] void issue(name to, asset quantity, string memo);

   [[eosio::action]] void retire(asset quantity, string memo);

   [[eosio::action]] void transfer(name from,
                                   name to,
                                   asset quantity,
                                   string memo);

   [[eosio::action]] void claim(name owner, symbol_code sym);
   [[eosio::action]] void recover(name owner, symbol_code sym);
   [[eosio::action]] void open(name owner, const symbol &symbol, name ram_payer);
   [[eosio::action]] void close(name owner, const symbol &symbol);

   [[eosio::action]] void stake(const name &owner, asset quantity);
   [[eosio::action]] void unstake(const name &owner, asset quantity);
   [[eosio::action]] void realizediv(const name &owner);
   [[eosio::action]] void refund(const name &owner);
   [[eosio::action]] void distribute(const name &owner, asset quantity);

   struct input {
      uint64_t id;
      signature sig;
   };

   struct output {
      public_key pk;
      name account;
      asset quantity;
   };

   [[eosio::action]] void transferutxo(const name &payer, const std::vector<input> &inputs, const std::vector<output> &outputs, const string &memo);
   [[eosio::action]] void loadutxo(const name &from, const public_key &pk, const asset &quantity);

   static asset get_supply(name token_contract_account, symbol_code sym_code)
   {
      stats statstable(token_contract_account, sym_code.raw());
      const auto &st = statstable.get(sym_code.raw());
      return st.supply;
   }

   static asset get_balance(name token_contract_account, name owner, symbol_code sym_code)
   {
      accounts accountstable(token_contract_account, owner.value);
      const auto &ac = accountstable.get(sym_code.raw());
      return ac.balance;
   }

 private:
   struct [[eosio::table]] account
   {
      asset balance;
      bool claimed = false;
      uint64_t primary_key() const { return balance.symbol.code().raw(); }
   };

   struct [[eosio::table]] currency_stats
   {
      asset supply;
      asset max_supply;
      name issuer;

      uint64_t primary_key() const { return supply.symbol.code().raw(); }
   };

   struct [[eosio::table]] utxo
   {
      uint64_t    id;
      public_key  pk;
      asset    amount;

      uint64_t primary_key() const { return id; }
      checksum256 by_pk() const { return getKeyHash(pk); }
   };

   struct [[eosio::table]] utxo_global
   {
      uint64_t    id;
      uint64_t    next_pk;

      uint64_t primary_key() const { return id; }
   };

   struct [[eosio::table]] team_vesting
   {
      name account;
      asset issued;

      uint64_t primary_key() const { return account.value; }
   };

   typedef eosio::multi_index<"accounts"_n, account> accounts;
   typedef eosio::multi_index<"stat"_n, currency_stats> stats;
   typedef eosio::multi_index<"teamvest"_n, team_vesting> vesting;
   typedef eosio::multi_index<"utxos"_n, 
                              utxo,
                              indexed_by<"ipk"_n, const_mem_fun<utxo, checksum256, &utxo::by_pk>>
                              > utxos;
   typedef eosio::multi_index<"utxoglobals"_n, utxo_global> utxo_globals;

   static inline checksum256 getKeyHash(const public_key &pk)
   {
      return sha256(pk.data.begin(), 33);
   }

   void sub_balance(name owner, asset value);
   void add_balance(name owner, asset value, name ram_payer, bool claimed);

   void do_claim(name owner, symbol_code sym, name payer);

   uint64_t getNextUTXOId();

   struct [[eosio::table]] user_staked 
   {
      asset quantity;
      double lastDividendsFrac;

      uint64_t primary_key() const { return quantity.symbol.code().raw(); }
   };

   typedef eosio::multi_index<"staked"_n, user_staked> staked;

   struct [[eosio::table]] dividend 
   {
      asset totalStaked;
      asset totalDividends;
      asset totalUnclaimedDividends;

      double totalDividendFrac;

      uint64_t primary_key() const { return totalStaked.symbol.code().raw(); }
   };

   struct [[eosio::table]] refund_request {
      name            owner;
      uint32_t  request_time;
      eosio::asset    amount;

      bool is_empty()const { return amount.amount == 0; }
      uint64_t  primary_key()const { return owner.value; }
   };

   typedef eosio::multi_index<"dividends"_n, dividend> dividends;
   typedef eosio::multi_index<"refunds"_n, refund_request> refunds_table;

   static constexpr name PEOS_CONTRACT_ACCOUNT    = "thepeostoken"_n;
   static constexpr name PEOS_MARKETING_ACCOUNT   = "peosmarketin"_n;
   static constexpr name PEOS_TEAMFUND_ACCOUNT    = "peosteamfund"_n;

   void validate_peos_team_vesting(name account, asset quantity);
};

} // namespace eosio