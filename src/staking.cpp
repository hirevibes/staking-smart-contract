#include "staking.hpp"
#include <eosio/transaction.hpp>
#include <eosio/print.hpp>

namespace hirevibes
{

bool const inline staking::is_frozen()
{
  auto st = setting_table.exists() ? setting_table.get() : get_default_params();
  return st.freeze;
}

bool const inline staking::is_allowed_to_stake(name const owner)
{
  return std::find(whitelist.begin(), whitelist.end(), owner) == whitelist.end();
}
bool const inline staking::is_allowed_claim(name const owner)
{
  profile_index _profile(get_self(), get_self().value);
  auto itr = _profile.find(owner.value);

  if (itr != _profile.end()) {
    return itr->active;
  }
  return false;
}

uint64_t inline staking::get_active_day()
{

  auto st = setting_table.exists() ? setting_table.get() : get_default_params();

  return st.active_day;
}

uint32_t inline staking::get_reward_ratio(uint64_t const day)
{

  daily_reward_index dr_table(get_self(), get_self().value);

  auto itr = dr_table.find(day);

  if (itr == dr_table.end())
    return 0;

  return itr->ratio;
}

asset inline staking::calc_user_unclaimed_reward(name const owner)
{
  resource_index res_table(get_self(), owner.value);

  auto itr = res_table.find(HVT_SYMBOL.code().raw());

  if (itr == res_table.end())
    return asset(0, HVT_SYMBOL);

  auto const active_day = get_active_day();

  auto last_calc_day = itr->last_calc_day;

  auto diff = active_day - last_calc_day;

  int64_t reward = 0;

  // preventing the calc timeout
  if (diff > 100)
  {
    diff = 100;
  }

  while (diff > 0)
  {
    auto ratio = get_reward_ratio(last_calc_day);
    reward += int64_t((itr->quantity.amount * ratio) / HVT_DECIMAL_PLACES);
    --diff;
    last_calc_day++;
  }

  // updating the last reward calc day
  res_table.modify(itr, same_payer, [&](auto &r) {
    r.last_calc_day = last_calc_day;
  });

  return asset(reward, HVT_SYMBOL);
}

void staking::send_token(name const to, asset const quantity, std::string const memo)
{
  action(
      permission_level(get_self(), "active"_n),
      HVT_CODE,
      "transfer"_n,
      std::make_tuple(get_self(), to, quantity, memo))
      .send();
}

void staking::add_resources(name const owner, asset const val, name const ram_payer)
{

  resource_index res_table(get_self(), owner.value);

  auto itr = res_table.find(val.symbol.code().raw());

  if (itr != res_table.end())
  {
    asset unclaimed_reward = calc_user_unclaimed_reward(owner);
    res_table.modify(itr, same_payer, [&](auto &r) {
      r.quantity += val;
      r.unclaimed_tokens += unclaimed_reward;
      if (itr->last_calc_day == 1767) {
        r.last_calc_day = 177;
      } else {
        r.last_calc_day = get_active_day();
      }
    });
  }
  else
  {
    res_table.emplace(ram_payer, [&](auto &r) {
      r.quantity = val;
      r.unclaimed_tokens = asset(0, HVT_SYMBOL);
      r.last_calc_day = get_active_day();
    });
  }
  auto st = setting_table.exists() ? setting_table.get() : get_default_params();
  st.total_staked_hvt += val;
  setting_table.set(st, get_self());
}

void staking::sub_resources(name const owner, asset const val)
{
  resource_index res_table(get_self(), owner.value);
  auto itr = res_table.find(val.symbol.code().raw());

  eosio::check(itr != res_table.end(), "no resource object found");
  eosio::check(itr->quantity >= val, "overdrawn resource");

  if (itr->quantity == val && itr->unclaimed_tokens.amount == 0)
  {
    res_table.erase(itr);
  }
  else
  {
    asset unclaimed_reward = calc_user_unclaimed_reward(owner);

    res_table.modify(itr, owner, [&](auto &r) {
      r.quantity -= val;
      r.unclaimed_tokens += unclaimed_reward;
    });
  }

  auto st = setting_table.get();
  st.total_staked_hvt -= val;
  setting_table.set(st, get_self());
}

/* Admin actions */
void staking::setday(uint64_t day)
{
  require_auth(get_self());
  auto setting = setting_table.get();
  setting.active_day = day;
  setting_table.set(setting, get_self());
}

void staking::freeze()
{
  require_auth(get_self());
  auto setting = setting_table.get();
  setting.freeze = true;
  setting_table.set(setting, get_self());
}

void staking::unfreeze()
{
  require_auth(get_self());
  auto setting = setting_table.get();
  setting.freeze = false;
  setting_table.set(setting, get_self());
}

void staking::calcratio(uint64_t const day)
{
  eosio::check(!is_frozen(), FREEZE_MESSAGE.c_str());
  auto const payer = get_self();
  require_auth(payer);
  auto const setting = setting_table.get();
  auto const ratio = int32_t(float(DAILY_REWARD_LIMIT.amount) / float(setting.total_staked_hvt.amount) * HVT_DECIMAL_PLACES);
  daily_reward_index daily_reward_table(get_self(), get_self().value);
  auto itr = daily_reward_table.find(day);
  if (itr != daily_reward_table.end())
  {
    daily_reward_table.modify(itr, payer, [&](auto &r) {
      r.ratio = ratio;
    });
  }
  else
  {
    daily_reward_table.emplace(payer, [&](auto &r) {
      r.day = day;
      r.ratio = ratio;
    });
  }
}

/* Users actions */

void staking::checkreward(name const owner)
{

  asset unclaimed_reward = calc_user_unclaimed_reward(owner);

  resource_index resource_table(get_self(), get_self().value);

  auto userres = resource_table.find(owner.value);

  if (userres != resource_table.end())
  {
    unclaimed_reward += userres->unclaimed_tokens;
  }

  std::string err = unclaimed_reward.to_string();

  eosio::check(false, err.c_str());
}

// [[eosio::on_notify("hirevibeshvt::transfer")]]
// void staking::powerup(transfer_args const &t)
// {
//   const name owner = name(t.from);

//   if (owner != get_self() && t.to == get_self().value && is_allowed_to_stake(owner))
//   {
//     eosio::check(!is_frozen(), FREEZE_MESSAGE.c_str());
//     eosio::check(is_account(owner), "from account does not exist");
//     eosio::check(t.quantity.is_valid(), "invalid quantity");
//     eosio::check(t.quantity.amount > 0, "must stake positive quantity");
//     eosio::check(t.quantity.symbol == HVT_SYMBOL, "symbol precision mismatch");
//     add_resources(owner, t.quantity, get_self());
//   }
// }

[[eosio::on_notify("hirevibeshvt::transfer")]]
void staking::powerup(name const from, name const to, asset const quantity, std::string const memo)
{
  check(!is_frozen(), FREEZE_MESSAGE.c_str());
  check(is_account(from), "from account does not exist");
  check(quantity.is_valid(), "invalid quantity");
  check(quantity.amount > 0, "must stake positive quantity");
  check(quantity.symbol == HVT_SYMBOL, "symbol precision mismatch");
  check(get_first_receiver() == name("hirevibeshvt"), "invalid token");
  
  if (from != get_self() && to == get_self() && is_allowed_to_stake(from))
  {
    add_resources(from, quantity, get_self());
  }
}

void staking::powerdown(name const owner, asset const quantity)
{
  eosio::check(!is_frozen(), FREEZE_MESSAGE.c_str());
  eosio::check(is_account(owner), "owner account doesn't exists");
  eosio::check(quantity.is_valid(), "invalid quantity");
  eosio::check(quantity.amount > 0, "must stake positive quantity");
  eosio::check(quantity.symbol == HVT_SYMBOL, "symbol precision mismatch");

  require_auth(owner);

  sub_resources(owner, quantity);

  refund_index refund_table(get_self(), owner.value);

  auto itr = refund_table.find(quantity.symbol.code().raw());

  auto const ct = time_point_sec(eosio::current_time_point());

  if (itr != refund_table.end())
  {

    refund_table.modify(itr, same_payer, [&](auto &rt) {
      rt.quantity += quantity;
      rt.request_at = ct;
    });
  }
  else
  {

    refund_table.emplace(owner, [&](auto &rt) {
      rt.quantity = quantity;
      rt.request_at = ct;
    });
  }

  eosio::transaction out{};
  out.actions.emplace_back(
      permission_level{get_self(), "active"_n},
      get_self(),
      "refund"_n,
      std::make_tuple(owner));
  out.delay_sec = REFUND_DELAY;
  cancel_deferred(owner.value);
  out.send(owner.value, get_self(), true);
}

void staking::refund(name const owner)
{
  eosio::check(is_account(owner), "owner account does not exists");
  refund_index refund_table(get_self(), owner.value);
  auto const itr = refund_table.find(HVT_SYMBOL.code().raw());
  eosio::check(itr != refund_table.end(), "refund object doesn't exists.");
  eosio::check((itr->request_at + REFUND_DELAY).utc_seconds <= eosio::current_time_point().sec_since_epoch(), "refund is not available yet");
  send_token(owner, itr->quantity, "refund hvt");
  refund_table.erase(itr);
}

void staking::claim(name const owner)
{

  eosio::check(!is_frozen(), FREEZE_MESSAGE.c_str());

  eosio::check(is_account(owner), "owner account doesn't exists.");

  eosio::check(is_allowed_claim(owner), "you need to setup hirevibes profile to claim your staking reward, please contact hirevibes support.");

      require_auth(owner);

  resource_index resource_table(get_self(), owner.value);

  auto userres = resource_table.find(HVT_SYMBOL.code().raw());

  eosio::check(userres != resource_table.end(), "staking object doesn't exist");

  asset reward = calc_user_unclaimed_reward(owner);

  asset total_reward = userres->unclaimed_tokens + reward;

  eosio::check(total_reward.amount > 0, "you have already claimed your staking reward");

  send_token(owner, total_reward, "staking reward");

  resource_table.modify(userres, owner, [&](auto &r) {
    r.last_claim_time = time_point_sec(eosio::current_time_point());
    r.unclaimed_tokens = asset(0, HVT_SYMBOL);
    r.last_calc_day = get_active_day();
  });
}

void staking::setprofile(name const owner, bool const active, std::string const memo)
{
  eosio::check(is_account(owner), "account is invalid");
  eosio::check(memo.size() <= 256, "memo has more than 256 bytes");

  auto const payer = get_self();
  require_auth(payer);

  profile_index _profile(payer, payer.value);
  auto itr = _profile.find(owner.value);
  if (itr != _profile.end()) {
    _profile.modify(itr, payer, [&](auto &row){
      row.active = active;
    });
  } else {
    _profile.emplace(payer, [&](auto &row) {
      row.owner = owner;
      row.active = active;
    });
  }
}

// extern "C"
// {
//   [[noreturn]] void apply(uint64_t receiver, uint64_t code, uint64_t action) {
//     if (code != receiver && code == "hirevibeshvt"_n.value && action == "transfer"_n.value)
//     {
//       execute_action(eosio::name(receiver), eosio::name(code), &staking::powerup);
//     }

//     if (code == receiver)
//     {
//       switch (action)
//       {
//         EOSIO_DISPATCH_HELPER(staking, (powerdown)(claim)(refund)(setday)(freeze)(unfreeze)(calcratio)(checkreward)(setprofile))
//       }
//     }

//     eosio_exit(0);
//   }
// }
} // namespace hirevibes