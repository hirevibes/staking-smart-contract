#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/time.hpp>
#include <eosio/system.hpp>
#include <eosio/singleton.hpp>
#include <cmath>

using namespace eosio;

namespace hirevibes
{

// transfer action param struct
struct transfer_args
{
  uint64_t from;
  uint64_t to;
  asset quantity;
  std::string memo;
};

// 3 * 24 * 3600
static const uint64_t REFUND_DELAY = 3 * 24 * 60 * 60;

static constexpr int16_t HVT_DECIMAL_PLACES = 10000;
static const symbol HVT_SYMBOL = symbol("HVT", 4);
static constexpr name HVT_CODE = name("hirevibeshvt");

static const std::string FREEZE_MESSAGE = "staking is not availble.";

// 50k daily reward limit
static const eosio::asset DAILY_REWARD_LIMIT_50K = asset(50000 * std::pow(10, 4), HVT_SYMBOL);
static const eosio::asset DAILY_REWARD_LIMIT = asset(30136 * std::pow(10, 4), HVT_SYMBOL);

static const std::vector<name> whitelist = {
    "hirevibeshvt"_n,
    "hirevibesdev"_n,
    "hvfoundation"_n,
    "smillingbolt"_n,
    "g44tinrxgyge"_n,
    "heydcmrrg4ge"_n,
    "ha2dgobyhege"_n,
    "thekedlucius"_n,
    "elliottminns"_n,
    "liamdgmurphy"_n,
    "absoluteduck"_n,
    "cleverrabbit"_n
};

CONTRACT staking : public contract
{
public:
  using contract::contract;
  staking(name const receiver, name const code, datastream<const char *> const ds)
      : contract(receiver, code, ds), setting_table(get_self(), get_self().value) {}

  // void inline powerup(transfer_args const &t);
  void inline powerup(name const from, name const to, asset const quantity, std::string const memo);
  ACTION powerdown(name const owner, asset const quantity);
  ACTION refund(name const owner);
  ACTION claim(name const owner);
  ACTION checkreward(name const owner);
  ACTION setprofile(name const owner, bool const active, std::string const memo);

  ACTION setday(uint64_t const day);
  ACTION calcratio(uint64_t const day);
  ACTION freeze();
  ACTION unfreeze();


private:
  void inline add_resources(name const owner, asset const quantity, name const payer);
  void inline sub_resources(name const owner, asset const quantity);
  void inline calc_reward(name const owner, time_point_sec const last_calim_time);
  void inline send_token(name const to, asset const quantity, std::string const memo);

  asset inline calc_user_unclaimed_reward(name const owner);

  uint32_t inline get_reward_ratio(uint64_t const day);
  uint64_t inline get_active_day();

  bool const inline is_allowed_to_stake(name const owner);
  bool const inline is_allowed_claim(name const owner);
  bool const inline is_frozen();

  /** Resource table */
  TABLE resource_row
  {
    asset quantity;
    asset unclaimed_tokens;
    time_point_sec last_claim_time;
    uint64_t last_calc_day = 1;

    uint64_t primary_key() const { return quantity.symbol.code().raw(); }

    EOSLIB_SERIALIZE(resource_row, (quantity)(unclaimed_tokens)(last_claim_time)(last_calc_day))
  };
  typedef multi_index<"resources"_n, resource_row> resource_index;
  /* end resource table */

  /** Reward ratio table */
  TABLE rewardratio_row
  {
    uint64_t day;
    int32_t ratio;

    uint64_t primary_key() const { return day; }

    EOSLIB_SERIALIZE(rewardratio_row, (day)(ratio))
  };
  typedef multi_index<"rewardratio"_n, rewardratio_row> daily_reward_index;

  /* end reward ratio table*/

  /** Refund table */
  TABLE refund_row
  {

    asset quantity;
    time_point_sec request_at;

    uint64_t primary_key() const { return quantity.symbol.code().raw(); }

    EOSLIB_SERIALIZE(refund_row, (quantity)(request_at))
  };

  typedef multi_index<"refundreqs"_n, refund_row> refund_index;
  /* end refund table */

  TABLE setting
  {
    asset total_staked_hvt;
    uint64_t active_day;
    bool freeze;

    EOSLIB_SERIALIZE(setting, (total_staked_hvt)(active_day)(freeze))
  };

  typedef singleton<"setting"_n, setting> setting_index;
  setting_index setting_table;

  TABLE profile {
    name owner;
    bool active = false;

    uint64_t primary_key() const { return owner.value; }

    EOSLIB_SERIALIZE(profile, (owner)(active))
  };

  typedef multi_index<"hvprofiles"_n, profile> profile_index;

  setting get_default_params()
  {
    setting st = {
        .total_staked_hvt = asset(0, HVT_SYMBOL),
        .active_day = 1,
        .freeze = false};
    return st;
  }
};
} // namespace hirevibes