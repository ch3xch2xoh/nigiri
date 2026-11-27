#include "gtest/gtest.h"

#include "nigiri/loader/hrd/load_timetable.h"
#include "nigiri/loader/init_finish.h"

#include "nigiri/routing/raptor_search.h"

#include "../loader/hrd/hrd_timetable.h"

#include "../raptor_search_modified.h"
#include "results_to_string.h"

#include "nigiri/routing/raptor/raptor.h"
#include "nigiri/routing/raptor_search_modified.h"

#include <iostream>
#include <random>

#include <sstream>
#include <string>
#include <regex>

using namespace date;
using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::test_data::hrd_timetable;
using nigiri::test::raptor_search_modified;



inline loader::mem_dir files_mod_table() {
  constexpr auto const services_abc = R"(
*Z 01337 80____       048 030                             %
*A VE 0000001 0000002 000005                              %
*G RE  0000001 0000002                                    %
0000001 A                            00230                %
0000002 B                     00330                       %
*Z 07331 80____       092 015                             %
*A VE 0000002 0000003 000005                              %
*G RE  0000002 0000003                                    %
0000002 B                            00230                %
0000003 C                     00330                       %
)";
  return test_data::hrd_timetable::base().add(
      {loader::hrd::hrd_5_20_26.fplan_ / "services.101", services_abc});
}

inline loader::mem_dir files_mod_table2() {
  constexpr auto const services_abc = R"(
*Z 01337 80____       024 030                             %
*A VE 0000001 0000004 000005                              %
*G RE  0000001 0000004                                    %
0000001 A                            00230                %
0000002 B                     00330  00331                %
0000004 D                     00430                       %
*Z 01234 80____       024 030                             %
*A VE 0000002 0000003 000005                              %
*G RE  0000002 0000003                                    %
0000002 B                            00230                %
0000003 C                     00330                       %
*Z 07331 80____       048 015                             %
*A VE 0000004 0000003 000005                              %
*G RE  0000004 0000003                                    %
0000004 D                            00230                %
0000003 C                     00330                       %
)";
  return test_data::hrd_timetable::base().add(
      {loader::hrd::hrd_5_20_26.fplan_ / "services.101", services_abc});
}



constexpr auto const fwd_journeys = R"(
[2020-03-30 05:00, 2020-03-30 07:15]
TRANSFERS: 1
     FROM: (A, 0000001) [2020-03-30 05:00]
       TO: (C, 0000003) [2020-03-30 07:15]
leg 0: (A, 0000001) [2020-03-30 05:00] -> (B, 0000002) [2020-03-30 06:00]
   0: 0000001 A...............................................                               d: 30.03 05:00 [30.03 07:00]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/300/0000002/360/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:00 [30.03 08:00]
leg 1: (B, 0000002) [2020-03-30 06:00] -> (B, 0000002) [2020-03-30 06:02]
  FOOTPATH (duration=2)
leg 2: (B, 0000002) [2020-03-30 06:15] -> (C, 0000003) [2020-03-30 07:15]
   0: 0000002 B...............................................                               d: 30.03 06:15 [30.03 08:15]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/375/0000003/435/, src=0}]
   1: 0000003 C............................................... a: 30.03 07:15 [30.03 09:15]

[2020-03-30 05:30, 2020-03-30 07:45]
TRANSFERS: 1
     FROM: (A, 0000001) [2020-03-30 05:30]
       TO: (C, 0000003) [2020-03-30 07:45]
leg 0: (A, 0000001) [2020-03-30 05:30] -> (B, 0000002) [2020-03-30 06:30]
   0: 0000001 A...............................................                               d: 30.03 05:30 [30.03 07:30]  [{name=RE 1337, day=2020-03-30, id=1337/0000001/330/0000002/390/, src=0}]
   1: 0000002 B............................................... a: 30.03 06:30 [30.03 08:30]
leg 1: (B, 0000002) [2020-03-30 06:30] -> (B, 0000002) [2020-03-30 06:32]
  FOOTPATH (duration=2)
leg 2: (B, 0000002) [2020-03-30 06:45] -> (C, 0000003) [2020-03-30 07:45]
   0: 0000002 B...............................................                               d: 30.03 06:45 [30.03 08:45]  [{name=RE 7331, day=2020-03-30, id=7331/0000002/405/0000003/465/, src=0}]
   1: 0000003 C............................................... a: 30.03 07:45 [30.03 09:45]

)";

TEST(routing, raptor_forward_modified) {
  constexpr auto const src = source_idx_t{0U};

  timetable tt;
  tt.date_range_ = full_period();
  load_timetable(src, loader::hrd::hrd_5_20_26, files_mod_table2(), tt);
  finalize(tt);

  auto const results = raptor_search_modified(
      tt,
      nullptr,
      "0000001",
      "0000003",
      interval{unixtime_t{sys_days{2020_y / March / 30}} + 5_hours,
               unixtime_t{sys_days{2020_y / March / 30}} + 6_hours}
      );

  //EXPECT_EQ(std::string_view{fwd_journeys}, to_string(tt, results));
  std::cout << to_string(tt, results) << '\n';
}

TEST(routing, raptor_forward_modified_bench) {

  auto tt_path = std::filesystem::path{std::filesystem::current_path() / "cmake-build-debug/tt.bin"};
  timetable tt = *nigiri::timetable::read(tt_path);
  tt.resolve();

  //constexpr auto const src = source_idx_t{0U};

  routing::start_time_t time = interval{unixtime_t{sys_days{2026_y / December / 6}} + 5_hours,
               unixtime_t{sys_days{2026_y / December / 6}} + 6_hours};

  rt_timetable const* rtt = nullptr;
  constexpr profile_idx_t profile = 0U;

  assert(tt.n_locations() > 0);
  std::uniform_int_distribution<location_idx_t::value_t> location_d{0, tt.n_locations()-1};
  std::mt19937 rng{42};


  vector<unsigned> froms = {
  108196,
  40286,
  600774,
  416929,
  491114,
  14277,
  672723,
  577377,
  147277,
  126112,
  127208,
  211020,
  363967,
  299594,
  201994,
  424377,
  96752,
  202629,
  254106,
  316327,
  544593,
  138492,
  356669,
  410895,
  32217,
  421389,
  118274,
  45119,
  658141,
  669756,
  560699,
  211278,
  67744,
  474579,
  305287,
  84644,
  343451,
  23851,
  630699,
  179488,
  459521,
  216200,
  360716,
  379194,
  128213,
  672498,
  537627,
  651630,
  620646,
  414699,
  639406,
  61377,
  135932,
  31369,
  225647,
  269584,
  188206,
  574807,
  247441,
  194854,
  376410,
  97744,
  556399,
  51707,
  684498,
  535624,
  137828,
  3830,
  565599,
  490272,
  505634,
  534948,
  51356,
  248629,
  80366,
  598643,
  432315,
  229508,
  44083,
  215695,
  225545,
  506050,
  442206,
  615365,
  327525,
  82949,
  494702,
  527675,
  389298,
  534738,
  342493,
  362564,
  296539,
  17630,
  74832,
  21799,
  441410,
  218035,
    415225
  };
  vector<unsigned> tos = {
  69342,
  318532,
  231458,
  99091,
  451452,
  39126,
  500774,
  650974,
  540,
  688191,
  428281,
  424238,
  4901,
  15995,
  363980,
  277341,
  32367,
  675390,
  161448,
  62844,
  428908,
  265273,
  681963,
  323743,
  596449,
  471857,
  312463,
  9200,
  653505,
  390693,
  267322,
  11074,
  160146,
  167173,
  473907,
  423090,
  577898,
  120244,
  271237,
  126397,
  523914,
  294885,
  144227,
  393753,
  21718,
  584203,
  311946,
  274073,
  642725,
  504431,
  226486,
  395656,
  361247,
  666663,
  585763,
  518336,
  374327,
  406967,
  669495,
  421035,
  191431,
  205493,
  114628,
  10845,
  293668,
  273887,
  203561,
  9765,
  137915,
  493382,
  548061,
  420290,
  642476,
  451583,
  634610,
  589581,
  311736,
  66175,
  257197,
  463904,
  461879,
  410120,
  190545,
  389275,
  265595,
  673973,
  588801,
  500587,
  163677,
  177607,
  28044,
  492911,
  76913,
  304721,
  139911,
  621296,
  329713,
  390684,
    413971
  };

  size_t count = 20;
  size_t valid_runs = 0;

  //routing::search_stats search_stats_;
  int raptor_exececutions = 0;
  std::chrono::milliseconds raptor_time{0};
  std::chrono::milliseconds total_time{0};

  std::uint64_t n_routing_time_{0ULL};
  std::uint64_t n_footpaths_visited_{0ULL};
  std::uint64_t n_routes_visited_{0ULL};
  std::uint64_t n_earliest_arrival_updated_by_route_{0ULL};
  std::uint64_t n_earliest_arrival_updated_by_footpath_{0ULL};

  while (true) {
    //location_idx_t random_from_idx = location_idx_t{138492};//location_idx_t{location_d(rng)};
    //location_idx_t random_to_idx = location_idx_t{265273};//location_idx_t{location_d(rng)};

    //location_idx_t random_from_idx = location_idx_t{location_d(rng)};
    //location_idx_t random_to_idx = location_idx_t{location_d(rng)};
    //
    location_idx_t random_from_idx = location_idx_t{froms[count]};
    location_idx_t random_to_idx = location_idx_t{tos[count]};


    //From: 138492, To: 265273 valid journey



    auto q = routing::query{
      .start_time_ = time,
      .start_ = {{random_from_idx, 0_minutes, 0U}},
      .destination_ = {{random_to_idx, 0_minutes, 0U}},
      .prf_idx_ = profile,
      .allowed_claszes_ = routing::all_clasz_allowed(),
      .require_bike_transport_ = false,
      .require_car_transport_ = false,
      .via_stops_ = {}
    };
    q.min_connection_count_=10;
    //q.fastest_direct_ = std::chrono::minutes{60*100} ;



    using algo_state_t = routing::raptor_state;
    auto search_state = routing::search_state{};
    auto algo_state = algo_state_t{};

    routing::routing_result const r_result = routing::raptor_search_modified(tt, rtt, search_state,
      algo_state, std::move(q), direction::kForward);
    pareto_set<routing::journey> const result_journeys = *(r_result.journeys_);
    if (!result_journeys.empty()) {
    //==============================

    raptor_exececutions+=r_result.raptor_exececutions;
    raptor_time+=r_result.raptor_time;
    total_time+=r_result.search_stats_.execute_time_;

    auto rstats = r_result.algo_stats_;

    n_routing_time_ += rstats.at("n_routing_time");
    n_footpaths_visited_ += rstats.at("n_footpaths_visited");
    n_routes_visited_ += rstats.at("n_routes_visited");
    n_earliest_arrival_updated_by_route_ += rstats.at("n_earliest_arrival_updated_by_route");
    n_earliest_arrival_updated_by_footpath_ += rstats.at("n_earliest_arrival_updated_by_footpath");



    //==============================


    //EXPECT_EQ(std::string_view{fwd_journeys}, to_string(tt, results));


    //string s = to_string(tt, result_journeys);
    //std::cout << s << '\n';
    //std::cout  << '\n';
    //std::cout  << '\n';
    //std::cout  << '\n';
    //s = std::regex_replace(s, std::regex(R"(^.*\.{6,}.*\n?)", std::regex_constants::ECMAScript | std::regex_constants::multiline), "");
    //std::string filtered;
    //std::istringstream stream((s.data()));
    //std::string line;
    /*
    while (std::getline(stream, line)) {
      if (line.find("......") == std::string::npos) {
        filtered += line + '\n';
      }
    }
    */
    //std::cout << filtered << '\n';
      std::cout << '\n';
      std::cout << "YR found " << result_journeys.size() << " journeys" << '\n';
      std::cout << "From: " << random_from_idx << ", To: " << random_to_idx << '\n';
      std::cout << "execution time " << std::to_string(r_result.search_stats_.execute_time_.count()) << "ms " << '\n';
      std::cout << "raptor_exececutions" << r_result.raptor_exececutions <<'\n';
      std::cout << "raptor_time" << r_result.raptor_time.count() << "ms" <<'\n';
      std::cout << "total_time" << r_result.search_stats_.execute_time_.count() << "ms" <<'\n';






      //froms.push_back(random_from_idx);
      //tos.push_back(random_to_idx);
      valid_runs++;
    }
    count++;
    if (count>=40 || count>=tos.size() || count>=froms.size()) {
      break;
    }

  }

  std::cout  << '\n';
  std::cout  << '\n';
  std::cout  << '\n';

  std::cout  << "raptor_exececutions" << raptor_exececutions <<'\n';
  std::cout  << "raptor_time" << raptor_time.count() << "ms" <<'\n';
  std::cout  << "total_time" << total_time.count() << "ms" <<'\n';
  std::cout  << "n_routing_time_:" << n_routing_time_ <<'\n';
  std::cout  << "n_footpaths_visited_:" << n_footpaths_visited_ <<'\n';
  std::cout  << "n_routes_visited_:" << n_routes_visited_ <<'\n';
  std::cout  << "n_earliest_arrival_updated_by_route_:" << n_earliest_arrival_updated_by_route_ <<'\n';
  std::cout  << "n_earliest_arrival_updated_by_footpath_:" << n_earliest_arrival_updated_by_footpath_ <<'\n';
  std::cout  << "querys: " << valid_runs << " of " << count-20 <<'\n';

}
