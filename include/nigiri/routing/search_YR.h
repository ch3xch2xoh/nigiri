#pragma once

#include "search.h" //for out-commented definitions

#include "nigiri/for_each_meta.h"
#include "nigiri/get_otel_tracer.h"
#include "nigiri/logging.h"
#include "nigiri/routing/dijkstra.h"
#include "nigiri/routing/direct.h"
#include "nigiri/routing/get_fastest_direct.h"
#include "nigiri/routing/interval_estimate.h"
#include "nigiri/routing/journey.h"
#include "nigiri/routing/limits.h"
#include "nigiri/routing/pareto_set.h"
#include "nigiri/routing/query.h"
#include "nigiri/routing/raptor/debug.h"
#include "nigiri/routing/start_times.h"
#include "nigiri/rt/rt_timetable.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

#include "fmt/format.h"
//#include "q_journeys.h"
#include "utl/enumerate.h"
#include "utl/equal_ranges_linear.h"
#include "utl/erase_if.h"
#include "utl/timing.h"
//#include <vector>



namespace nigiri::routing {
struct journey_comparator {
  bool operator()(journey const& a, journey const& b) const {
    if (a.dest_time_ != b.dest_time_) {
      return a.dest_time_ < b.dest_time_;
    }

    // Distinguish journeys with identical arrival times.
    return std::lexicographical_compare(
            a.legs_.begin(), a.legs_.end(),
            b.legs_.begin(), b.legs_.end()
        );
  }
};



struct journey_sets {
  std::vector<std::set<journey, journey_comparator>> sets_;

  void add(journey const& j) {
    assert(!j.legs_.empty()); //have to reconstruct journey before this
    unsigned transfers = j.transfers_;
    if (sets_.size() <= transfers) {
      sets_.resize(transfers + 1);
    }

    sets_[transfers].insert(j);
  }

  pareto_set<journey> pop_global_pareto_set() {
    pareto_set<journey> global_set;

    for (unsigned i = 0; i < sets_.size(); ++i) {
      if (sets_[i].empty()) {
        continue;
      }
      // std::set<journey, journey_comparator>::iterator j = sets_[i].begin();
      journey const* j_p = &(*(sets_[i].begin()));
      if (!global_set.is_dominated(*j_p)) {
        global_set.add(static_cast<journey>(*j_p));
        sets_[i].erase(*j_p);
      }
    }
    return global_set;
  }

  //can return journey without legs...
  journey pop_journey_of_length(unsigned transfers) {
    if (transfers<sets_.size() && !sets_[transfers].empty()) {
      journey const* j_p = &(*(sets_[transfers].begin()));
      for (unsigned i = 0; i < transfers; ++i) {
        if (sets_[i].begin()->dominates(*j_p)) {
          return journey{};
        }
      }
      journey j=*j_p;
      sets_[transfers].erase(*j_p);
      return j;
    }
    return journey{};
  }

  void add_locale_pareto_set(const pareto_set<journey>& lps) {
    for(const journey& j : lps.els_) {
      add(j);
    }
  }
};


struct trie_node {
  std::map<journey::leg, trie_node> children;
  bool undiscovered = true;

  trie_node& get_or_add(journey::leg const& l) {
    return children[l];
  }
};

template <direction SearchDir>
struct search_YR {
  using Algo = raptor<SearchDir, false, 0, search_mode::kOneToOne>;
  using algo_state_t = typename Algo::algo_state_t;
  using algo_stats_t = typename Algo::algo_stats_t;
  static constexpr auto const kFwd = (SearchDir == direction::kForward);
  static constexpr auto const kBwd = (SearchDir == direction::kBackward);

  Algo init(clasz_mask_t const allowed_claszes,
            bool const require_bikes_allowed,
            bool const require_cars_allowed,
            transfer_time_settings& tts,
            algo_state_t& algo_state) {
    auto span = get_otel_tracer()->StartSpan("search::init");
    auto scope = opentelemetry::trace::Scope{span};

    stats_.fastest_direct_ =
        static_cast<std::uint64_t>(fastest_direct_.count());

    utl::verify(q_.via_stops_.size() <= kMaxVias,
                "too many via stops: {}, limit: {}", q_.via_stops_.size(),
                kMaxVias);

    tts.factor_ = std::max(tts.factor_, 1.0F);
    tts.min_transfer_time_ = std::max(tts.min_transfer_time_, 0_minutes);
    tts.additional_time_ = std::max(tts.additional_time_, 0_minutes);
    tts.default_ = tts.factor_ == 1.0F  //
                   && tts.min_transfer_time_ == 0_minutes  //
                   && tts.additional_time_ == 0_minutes;

    collect_destinations(tt_, q_.destination_, q_.dest_match_mode_,
                         state_.is_destination_, state_.dist_to_dest_);

    for (auto const [i, via] : utl::enumerate(q_.via_stops_)) {
      collect_via_destinations(tt_, via.location_, state_.is_via_[i]);
    }

    if constexpr (Algo::kUseLowerBounds) {
      auto lb_span = get_otel_tracer()->StartSpan("lower bounds");
      auto lb_scope = opentelemetry::trace::Scope{lb_span};
      UTL_START_TIMING(lb);
      dijkstra(
          tt_, q_,
          (kFwd ? tt_.fwd_search_lb_graph_[q_.prf_idx_]
                : tt_.bwd_search_lb_graph_[q_.prf_idx_]),
          (rtt_ == nullptr ? nullptr
                           : &(kFwd ? rtt_->fwd_search_lb_graph_has_edges_
                                    : rtt_->bwd_search_lb_graph_has_edges_)),
          (rtt_ == nullptr ? nullptr
                           : &(kFwd ? rtt_->fwd_search_lb_graph_
                                    : rtt_->bwd_search_lb_graph_)),
          state_.travel_time_lower_bound_);
      UTL_STOP_TIMING(lb);
      stats_.lb_time_ = static_cast<std::uint64_t>(UTL_TIMING_MS(lb));

#if defined(NIGIRI_TRACING)
      for (auto const& o : q_.start_) {
        trace_upd("start {}: {}\n", loc{tt_, o.target()}, o.duration());
      }
      for (auto const& o : q_.destination_) {
        trace_upd("dest {}: {}\n", loc{tt_, o.target()}, o.duration());
      }
      for (auto const [l, lb] :
           utl::enumerate(state_.travel_time_lower_bound_)) {
        if (lb != std::numeric_limits<std::decay_t<decltype(lb)>>::max()) {
          trace_upd("lb {}: {}\n", loc{tt_, location_idx_t{l}}, lb);
        }
           }
#endif
    }

    return Algo{
      tt_,
      rtt_,
      algo_state,
      state_.is_destination_,
      state_.is_via_,
      state_.dist_to_dest_,
      q_.td_dest_,
      state_.travel_time_lower_bound_,
      q_.via_stops_,
      day_idx_t{
        std::chrono::duration_cast<date::days>(
            std::chrono::round<std::chrono::days>(
                search_interval_.from_ +
                ((search_interval_.to_ - search_interval_.from_) / 2)) -
            tt_.internal_interval().from_)
            .count()},
    allowed_claszes,
    require_bikes_allowed,
    require_cars_allowed,
    q_.prf_idx_ == 2U,
    tts};
  }

  search_YR(timetable const& tt,
         rt_timetable const* rtt,
         search_state& s,
         //algo_state_t& algo_state_unused,
         query q,
         std::optional<std::chrono::seconds> timeout = std::nullopt)
      : tt_{tt},
        rtt_{rtt},
        state_{s},
        q_{std::move(q)},
        //*
        search_interval_{std::visit(
            utl::overloaded{[](interval<unixtime_t> const start_interval) {
                              return start_interval;
                            },
                            [](unixtime_t const start_time) {
                              return interval<unixtime_t>{start_time,
                                                          start_time};
                            }},
            q_.start_time_)},
        //*/
        fastest_direct_{get_fastest_direct(tt_, q_, SearchDir)},
        //algo_{},
        timeout_(timeout),

        //newly added
        total_algo_stats_(algo_stats_t{})
  {


    utl::sort(q_.start_);
    utl::sort(q_.destination_);
    q_.sanitize(tt);

    std::cout << "Hello World!" << '\n';


    //start_time_t start_time = q_.start_time_;
    //offset start = q_.start_.front();
    //offset destination = q_.destination_.front();


    //offset = {tt.locations_.location_id_to_idx_.at({from, src}), 0_minutes,0U};

    //algo_stats_t total_algo_stats = algo_stats_t{};

    //algo_state_t algo_state = algo_state_t{};
    //Algo algo = init(q_.allowed_claszes_, q_.require_bike_transport_,  q_.require_car_transport_, q_.transfer_time_settings_, algo_state);

    //destination are set via q_ in is_destination bit array

  }

  routing_result execute() {
    auto span = get_otel_tracer()->StartSpan("search_YR::execute");
    auto scope = opentelemetry::trace::Scope{span};

    //state_.results_.clear();

    if (start_dest_overlap()) {
      return {.journeys_ = &state_.results_, .interval_ = search_interval_, .search_stats_ = stats_,
              .algo_stats_ = total_algo_stats_.to_map()};
    }


    auto const processing_start_time = std::chrono::steady_clock::now();


    unixtime_t const start_time = std::visit(
    utl::overloaded{
        [](unixtime_t const t) {
            return t;
        },
        [](interval<unixtime_t> const& i) {
            return i.from_;
        }
    },
    q_.start_time_);
    location_idx_t source_idx = q_.start_.front().target_;

    pareto_set<journey> original_pareto_set;

    auto const worst_time_at_dest =  start_time + (kFwd ? 1 : -1) *
      (std::min(fastest_direct_, q_.max_travel_time_) + duration_t{1});

    {
      algo_state_t algo_state = algo_state_t{};
      Algo algo = init(q_.allowed_claszes_, q_.require_bike_transport_,  q_.require_car_transport_, q_.transfer_time_settings_, algo_state);


      algo.next_start_time();
      algo.add_start(source_idx, start_time);

      //trace("Hello World!");


      algo.execute(start_time, q_.max_transfers_, worst_time_at_dest, q_.prf_idx_, original_pareto_set);
      total_algo_stats_ = total_algo_stats_ + algo.get_stats();

      //Journey reconstruction
      for (auto& j : original_pareto_set) {
        if (j.legs_.empty() && !j.error_) {
          try {
            algo.reconstruct(q_, j);
          } catch (std::exception const& e) {
            j.error_ = true;
            log(log_lvl::error, "search", "reconstruct failed: {}", e.what());
          }
        }
      }
    }

    //=============== Original Pareto Set is computed
    //setup final set
    pareto_set<journey> set_A;
    journey_sets set_B;
    trie_node tree_root;
    uint32_t prev_route_idx;

    // initialization

    set_B.add_locale_pareto_set(original_pareto_set);

    //cycle start
    while(true) {
      pareto_set<journey> current_pareto_set = set_B.pop_global_pareto_set();

      bool empty_set = current_pareto_set.empty();
      if (empty_set) {
        break;
      }
      /*
      if (timeout_ && (std::chrono::steady_clock::now() - processing_start_time) >= *timeout_){
        break;
      }
      */
      /*
      if (100 <= set_A.size()) {
        break;
      }
      */
      for(const journey& j: current_pareto_set) {
        set_A.add_not_optimal(j);
        trie_node* current_node = &tree_root;
        for(size_t i=0; i < j.legs_.size() ; i++) {
          journey::leg const&  l = j.legs_[i];
          if (std::holds_alternative<journey::run_enter_exit>(l.uses_)) {

            //advance node
            if (current_node->children.contains(l)) {
              continue;
            }
            current_node->children.try_emplace(l, trie_node{});

            current_node = &(current_node->children.at(l));
          }
        }
      }

      for(const journey& j: current_pareto_set) {
        trie_node* current_node = &tree_root;
        trie_node* prev_node = current_node;

        vector<uint32_t> whitelist_routes{};
        for(size_t i=0; i < j.legs_.size() ; i++) {
          journey::leg const&  l = j.legs_[i];
          if (std::holds_alternative<journey::run_enter_exit>(l.uses_)) {
            auto const& r = std::get<journey::run_enter_exit>(l.uses_);

            //advance node
            if (current_node->children.contains(l)) {
              if (current_node->children.at(l).undiscovered) {
                current_node->children.at(l).undiscovered = true;
              }
              else {
                continue;
              }
            }
            else {
              //this shouldn't happen?
            }

            pareto_set<journey> local_pareto_set;

            algo_state_t algo_state2 = algo_state_t{};
            Algo algo2 = init(
              q_.allowed_claszes_,
              q_.require_bike_transport_,
              q_.require_car_transport_,
              q_.transfer_time_settings_,
              algo_state2);

            algo2.next_start_time();
            algo2.add_start(source_idx, start_time);

            //mark route journey
            if(!whitelist_routes.empty()) {
              algo2.set_blocked_routes(whitelist_routes, true);
              unsigned max_transfers = whitelist_routes.size()-1;
              algo2.execute(start_time, max_transfers, worst_time_at_dest, q_.prf_idx_, local_pareto_set);

              std::cout << "root path with max transfers = " << max_transfers << '\n';
              std::cout << "local pareto set has after white: " << local_pareto_set.els_.size() << '\n';
            }

            //to be blocked
            vector<uint32_t> br{};
            uint32_t route_idx = tt_.transport_route_[r.r_.t_.t_idx_].v_;
            //br.push_back(route_idx);
            if (current_node==&tree_root){
              for(const auto& c : current_node->children) {
                if (std::holds_alternative<journey::run_enter_exit>(c.first.uses_)) {
                  auto const& c_r = std::get<journey::run_enter_exit>(c.first.uses_);
                  uint32_t c_route_idx = tt_.transport_route_[c_r.r_.t_.t_idx_].v_;
                  br.push_back(c_route_idx);
                }
              }
            }
            else {
              for(const auto& c : prev_node->children) {
                if (std::holds_alternative<journey::run_enter_exit>(c.first.uses_)) {
                  auto const& c_r = std::get<journey::run_enter_exit>(c.first.uses_);
                  uint32_t c_route_idx = tt_.transport_route_[c_r.r_.t_.t_idx_].v_;
                  if (c_route_idx == prev_route_idx) {
                    for(const auto& cc : c.second.children) {
                      if (std::holds_alternative<journey::run_enter_exit>(cc.first.uses_)) {
                        auto const& cc_r = std::get<journey::run_enter_exit>(cc.first.uses_);
                        uint32_t cc_route_idx = tt_.transport_route_[cc_r.r_.t_.t_idx_].v_;
                        br.push_back(cc_route_idx);
                      }
                    }
                  }
                }
              }
            }

            /* This is insufficient since the prev leg can be different while being the same node

            */
            //now find all journey with the same root path up to i-2 inclusive(last run_enter_exit) and block their i.th path
            //this makes the 2 lines above redundant
            //this means journeys in set_A with legs equal to the legs in j
            //filter all paths starting in i-2 going down to the root(since they are less likely to match)

            algo2.set_blocked_routes(br, false);

            auto next_starting_round = whitelist_routes.size() + 1;
            algo2.execute(start_time, q_.max_transfers_, worst_time_at_dest, q_.prf_idx_, local_pareto_set, next_starting_round);
            total_algo_stats_ = total_algo_stats_ + algo2.get_stats();
            std::cout << "local pareto set has after black: " << local_pareto_set.els_.size() << '\n';



            //Journey reconstruction
            for (auto& candidate_j : local_pareto_set) {
              if (candidate_j.legs_.empty() && !candidate_j.error_) {
                try {
                  algo2.reconstruct(q_, candidate_j);
                  std::cout << "RECONSTRUCTING SUCCESSFULL" << '\n';
                  set_B.add(candidate_j);
                  //original_pareto_set.add_not_optimal(candidate_j);
                } catch (std::exception const& e) {
                  std::cout << "ERROR WHILE RECONSTRUCTING" << '\n';
                  candidate_j.error_ = true;
                  log(log_lvl::error, "search", "reconstruct failed: {}", e.what());
                }
              }
            }

            prev_node = current_node;
            current_node = &(current_node->children.at(l));
            prev_route_idx=route_idx;
            whitelist_routes.push_back(route_idx);
          }
        }
      }
    }


    //algo.reset_arrivals(); ???
    /*
      utl::sort(state_.results_, [](journey const& a, journey const& b) {
        return std::tuple{a.start_time_, a.transfers_} <
               std::tuple{b.start_time_, b.transfers_};
      });
    */

    for (const auto& x: set_A) {
      state_.results_.add_not_optimal(x);
    }

    stats_.execute_time_ =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            (std::chrono::steady_clock::now() - processing_start_time));

    return {.journeys_ = &state_.results_,
            .interval_ = search_interval_,
            .search_stats_ = stats_,
            .algo_stats_ = total_algo_stats_.to_map()};
  }

private:
  bool is_ontrip() const {
    return holds_alternative<unixtime_t>(q_.start_time_);
  }

  bool is_pretrip() const { return !is_ontrip(); }

  bool start_dest_overlap() const {
    if (q_.start_match_mode_ == location_match_mode::kIntermodal ||
        q_.dest_match_mode_ == location_match_mode::kIntermodal) {
      return false;
        }

    auto const overlaps_start = [&](location_idx_t const x) {
      return utl::any_of(q_.start_, [&](offset const& o) {
        bool overlaps = false;
        for_each_meta(
            tt_, q_.start_match_mode_, o.target(),
            [&](location_idx_t const eq) { overlaps = overlaps || eq == x; });
        return overlaps;
      });
    };

    return utl::any_of(q_.destination_, [&](offset const& o) {
      auto overlaps = false;
      for_each_meta(tt_, q_.dest_match_mode_, o.target(),
                    [&](location_idx_t const eq) {
                      overlaps = overlaps || overlaps_start(eq);
                    });
      return overlaps;
    });
  }

  unsigned n_results_in_interval() const {
    if (holds_alternative<interval<unixtime_t>>(q_.start_time_)) {
      auto count = utl::count_if(state_.results_, [&](journey const& j) {
        return search_interval_.contains(j.start_time_);
      });
      return static_cast<unsigned>(count);
    } else {
      return static_cast<unsigned>(state_.results_.size());
    }
  }

  bool max_interval_reached() const {
    auto const can_search_earlier =
        q_.extend_interval_earlier_ &&
        search_interval_.from_ != tt_.external_interval().from_;
    auto const can_search_later =
        q_.extend_interval_later_ &&
        search_interval_.to_ != tt_.external_interval().to_;
    return !can_search_earlier && !can_search_later;
  }

  void add_start_labels(start_time_t const& start_interval,
                        bool const add_ontrip) {
    state_.starts_.reserve(500'000);
    get_starts(SearchDir, tt_, rtt_, start_interval, q_.start_, q_.td_start_,
               q_.via_stops_, q_.max_start_offset_, q_.start_match_mode_,
               q_.use_start_footpaths_, state_.starts_, add_ontrip, q_.prf_idx_,
               q_.transfer_time_settings_);
    std::sort(
        begin(state_.starts_), end(state_.starts_),
        [&](start const& a, start const& b) { return kFwd ? b < a : a < b; });
  }

  void remove_ontrip_results() {
    utl::erase_if(state_.results_, [&](journey const& j) {
      return !search_interval_.contains(j.start_time_);
    });
  }

  timetable const& tt_;
  rt_timetable const* rtt_;
  search_state& state_;
  query q_;
  interval<unixtime_t> search_interval_;
  search_stats stats_;
  duration_t fastest_direct_;
  //Algo algo_;
  std::optional<std::chrono::seconds> timeout_;

  //newly added
  algo_stats_t total_algo_stats_;

};
}  // namespace nigiri::routing
