#pragma once

#include "nigiri/routing/raptor/raptor.h"
#include "nigiri/routing/search_YR.h"
#include "nigiri/timetable.h"

namespace nigiri::routing {

routing_result raptor_search_modified(
    timetable const& tt,
    rt_timetable const* rtt,
    search_state& s_state,
    raptor_state& r_state,
    query q,
    direction search_dir,
    std::optional<std::chrono::seconds> timeout = std::nullopt);

}  // namespace nigiri::routing
