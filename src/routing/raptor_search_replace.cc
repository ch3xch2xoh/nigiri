#include "nigiri/routing/raptor_search_modified.h"
#include "nigiri/routing/raptor_search.h"

#include <string>
#include <string_view>
#include <utility>

#include "date/date.h"

#include "fmt/format.h"
#include "fmt/ranges.h"

#include "utl/overloaded.h"
#include "utl/to_vec.h"
#include "utl/verify.h"

#include "nigiri/get_otel_tracer.h"
#include "nigiri/routing/query.h"

namespace nigiri::routing {

namespace {

std::string_view location_match_mode_str(location_match_mode const mode) {
  using namespace std::literals;
  switch (mode) {
    case location_match_mode::kExact: return "exact"sv;
    case location_match_mode::kOnlyChildren: return "only_children"sv;
    case location_match_mode::kEquivalent: return "equivalent"sv;
    case location_match_mode::kIntermodal: return "intermodal"sv;
  }
  std::unreachable();
}

}  // namespace


routing_result raptor_search_replace(
    timetable const& tt,
    rt_timetable const* rtt,
    search_state& s_state,
    [[maybe_unused]]raptor_state& r_state,
    query q,
    direction const search_dir,
    std::optional<std::chrono::seconds> const timeout) {
  auto span = get_otel_tracer()->StartSpan("raptor_search");
  auto scope = opentelemetry::trace::Scope{span};
  if (span->IsRecording()) {
    std::visit(utl::overloaded{
                   [&](interval<unixtime_t> const& interval) {
                     span->SetAttribute("nigiri.query.start_time_interval.from",
                                        date::format("%FT%RZ", interval.from_));
                     span->SetAttribute("nigiri.query.start_time_interval.to",
                                        date::format("%FT%RZ", interval.to_));
                   },
                   [&](unixtime_t const& t) {
                     span->SetAttribute("nigiri.query.start_time",
                                        date::format("%FT%RZ", t));
                   }},
               q.start_time_);
    span->SetAttribute("nigiri.query.start_match_mode",
                       location_match_mode_str(q.start_match_mode_));
    span->SetAttribute("nigiri.query.destination_match_mode",
                       location_match_mode_str(q.dest_match_mode_));
    span->SetAttribute("nigiri.query.use_start_footpaths",
                       q.use_start_footpaths_);
    span->SetAttribute("nigiri.query.start_count", q.start_.size());
    span->SetAttribute("nigiri.query.destination_count", q.destination_.size());
    span->SetAttribute("nigiri.query.td_start_count", q.td_start_.size());
    span->SetAttribute("nigiri.query.td_destination_count", q.td_dest_.size());
    span->SetAttribute("nigiri.query.max_start_offset",
                       q.max_start_offset_.count());
    span->SetAttribute("nigiri.query.max_transfers", q.max_transfers_);
    span->SetAttribute("nigiri.query.min_connection_count",
                       q.min_connection_count_);
    span->SetAttribute("nigiri.query.extend_interval_earlier",
                       q.extend_interval_earlier_);
    span->SetAttribute("nigiri.query.extend_interval_later",
                       q.extend_interval_later_);
    span->SetAttribute("nigiri.query.prf_idx", q.prf_idx_);
    span->SetAttribute("nigiri.query.allowed_classes", q.allowed_claszes_);
    span->SetAttribute("nigiri.query.require_bike_transport",
                       q.require_bike_transport_);
    span->SetAttribute("nigiri.query.transfer_time_settings.default",
                       q.transfer_time_settings_.default_);
    span->SetAttribute("nigiri.query.via_stops_count", q.via_stops_.size());
    span->SetAttribute(
        "nigiri.query.search_direction",
        search_dir == direction::kForward ? "forward" : "backward");
    if (timeout) {
      span->SetAttribute("nigiri.query.timeout", timeout.value().count());
    }
  }

  //=== changed and simplified code
  q.sanitize(tt);

  //if (r_state.n_locations_==756){trace("lol")} //to prevent clang tidy from compiling this with unused param
  //using algo_t = raptor<direction::kForward, false, 0, search_mode::kOneToOne>;
  return search_YR<direction::kForward>{tt,      rtt,          s_state,
    //r_state,
    std::move(q), timeout}
  .execute();

  //===
}

}  // namespace nigiri::routing
