﻿
#include "ksp_plugin/flight_plan.hpp"

#include "integrators/embedded_explicit_runge_kutta_nyström_integrator.hpp"
#include "testing_utilities/make_not_null.hpp"

namespace principia {

using integrators::DormandElMikkawyPrince1986RKN434FM;

namespace ksp_plugin {

FlightPlan::FlightPlan(not_null<DiscreteTrajectory<Barycentric>*> const root,
    Instant const& initial_time,
    Instant const& final_time,
    Mass const& initial_mass,
    not_null<Ephemeris<Barycentric>*> const ephemeris,
                       Ephemeris<Barycentric>::AdaptiveStepParameters const&
                           adaptive_step_parameters)
    : initial_time_(initial_time),
      final_time_(final_time),
      initial_mass_(initial_mass),
      ephemeris_(ephemeris),
      adaptive_step_parameters_(adaptive_step_parameters) {
  CHECK(final_time_ >= initial_time_);
  auto it = root->LowerBound(initial_time_);
  if (it.time() != initial_time_) {
    --it;
    initial_time_ = it.time();
  }

  // Create a fork for the first coasting trajectory.
  segments_.emplace_back(root->NewForkWithoutCopy(it.time()));
  CoastLastSegment(final_time_);
}

FlightPlan::~FlightPlan() {
  // |segments_| is empty for a mock object.
  if (!segments_.empty()) {
    // Deleting the first fork deletes everything.
    DiscreteTrajectory<Barycentric>* trajectory = segments_.front();
    CHECK(!trajectory->is_root());
    trajectory->parent()->DeleteFork(&trajectory);
  }
}

Instant FlightPlan::initial_time() const {
  return initial_time_;
}

Instant FlightPlan::final_time() const {
  return final_time_;
}

int FlightPlan::number_of_manœuvres() const {
  return manœuvres_.size();
}

NavigationManœuvre const& FlightPlan::GetManœuvre(int const index) const {
  CHECK_LE(0, index);
  CHECK_LT(index, number_of_manœuvres());
  return manœuvres_[index];
}

bool FlightPlan::Append(Burn burn) {
  auto manœuvre =
      MakeNavigationManœuvre(
          std::move(burn),
          manœuvres_.empty() ? initial_mass_ : manœuvres_.back().final_mass());
  if (manœuvre.FitsBetween(start_of_last_coast(), final_time_) &&
      !manœuvre.IsSingular()) {
    DiscreteTrajectory<Barycentric>* recomputed_last_coast =
        CoastIfReachesManœuvreInitialTime(last_coast(), manœuvre);
    if (recomputed_last_coast != nullptr) {
      ReplaceLastSegment(recomputed_last_coast);
      Append(std::move(manœuvre));
      return true;
    }
  }
  return false;
}

void FlightPlan::RemoveLast() {
  CHECK(!manœuvres_.empty());
  manœuvres_.pop_back();
  PopLastSegment();  // Last coast.
  PopLastSegment();  // Last burn.
  ResetLastSegment();
  CoastLastSegment(final_time_);
}

bool FlightPlan::ReplaceLast(Burn burn) {
  CHECK(!manœuvres_.empty());
  auto manœuvre =
      MakeNavigationManœuvre(std::move(burn), manœuvres_.back().initial_mass());
  if (manœuvre.FitsBetween(start_of_penultimate_coast(), final_time_) &&
      !manœuvre.IsSingular()) {
    DiscreteTrajectory<Barycentric>* recomputed_penultimate_coast =
        CoastIfReachesManœuvreInitialTime(penultimate_coast(), manœuvre);
    if (recomputed_penultimate_coast != nullptr) {
      manœuvres_.pop_back();
      PopLastSegment();  // Last coast.
      PopLastSegment();  // Last burn.
      ReplaceLastSegment(recomputed_penultimate_coast);
      Append(std::move(manœuvre));
      return true;
    }
  }
  return false;
}

bool FlightPlan::SetFinalTime(Instant const& final_time) {
  if (start_of_last_coast() > final_time) {
    return false;
  } else {
    final_time_ = final_time;
    ResetLastSegment();
    CoastLastSegment(final_time_);
    return true;
  }
}

Ephemeris<Barycentric>::AdaptiveStepParameters const&
FlightPlan::adaptive_step_parameters() const {
  return adaptive_step_parameters_;
}

bool FlightPlan::SetAdaptiveStepParameters(
    Ephemeris<Barycentric>::AdaptiveStepParameters const&
        adaptive_step_parameters) {
  auto const original_adaptive_step_parameters = adaptive_step_parameters_;
  adaptive_step_parameters_ = adaptive_step_parameters;
  if (RecomputeSegments()) {
    return true;
  } else {
    // If the recomputation fails, leave this place as clean as we found it.
    adaptive_step_parameters_ = original_adaptive_step_parameters;
    CHECK(RecomputeSegments());
    return false;
  }
}

int FlightPlan::number_of_segments() const {
  return segments_.size();
}

void FlightPlan::GetSegment(
    int const index,
    not_null<DiscreteTrajectory<Barycentric>::Iterator*> begin,
    not_null<DiscreteTrajectory<Barycentric>::Iterator*> end) const {
  CHECK_LE(0, index);
  CHECK_LT(index, number_of_segments());
  *begin = segments_[index]->Fork();
  *end = segments_[index]->End();
}

void FlightPlan::GetAllSegments(
    not_null<DiscreteTrajectory<Barycentric>::Iterator*> begin,
    not_null<DiscreteTrajectory<Barycentric>::Iterator*> end) const {
  *begin = segments_.back()->Find(segments_.front()->Fork().time());
  *end = segments_.back()->End();
  CHECK(*begin != *end);
}

void FlightPlan::WriteToMessage(
    not_null<serialization::FlightPlan*> const message) const {
  initial_mass_.WriteToMessage(message->mutable_initial_mass());
  initial_time_.WriteToMessage(message->mutable_initial_time());
  final_time_.WriteToMessage(message->mutable_final_time());
  adaptive_step_parameters_.WriteToMessage(
      message->mutable_adaptive_step_parameters());
  for (auto const& manœuvre : manœuvres_) {
    manœuvre.WriteToMessage(message->add_manoeuvre());
  }
}

std::unique_ptr<FlightPlan> FlightPlan::ReadFromMessage(
    serialization::FlightPlan const& message,
    not_null<DiscreteTrajectory<Barycentric>*> const root,
    not_null<Ephemeris<Barycentric>*> const ephemeris) {
  bool const is_pre_буняковский = message.segment_size() > 0;

  std::unique_ptr<Ephemeris<Barycentric>::AdaptiveStepParameters>
      adaptive_step_parameters;
  if (is_pre_буняковский) {
    adaptive_step_parameters =
        std::make_unique<Ephemeris<Barycentric>::AdaptiveStepParameters>(
            AdaptiveStepSizeIntegrator<
                Ephemeris<Barycentric>::NewtonianMotionEquation>::
                    ReadFromMessage(message.integrator()),
            /*max_steps=*/1000,
            Length::ReadFromMessage(message.length_integration_tolerance()),
            Speed::ReadFromMessage(message.speed_integration_tolerance()));
  } else {
    CHECK(message.has_adaptive_step_parameters());
    adaptive_step_parameters =
        std::make_unique<Ephemeris<Barycentric>::AdaptiveStepParameters>(
            Ephemeris<Barycentric>::AdaptiveStepParameters::ReadFromMessage(
                message.adaptive_step_parameters()));
  }

  auto flight_plan = std::make_unique<FlightPlan>(
      root,
      Instant::ReadFromMessage(message.initial_time()),
      Instant::ReadFromMessage(message.final_time()),
      Mass::ReadFromMessage(message.initial_mass()),
      ephemeris,
      *adaptive_step_parameters);

  if (is_pre_буняковский) {
    // The constructor has forked a segment.  Remove it.
    flight_plan->PopLastSegment();
    for (auto const& segment : message.segment()) {
      flight_plan->segments_.emplace_back(
          DiscreteTrajectory<Barycentric>::ReadPointerFromMessage(
              segment, root));
    }
    for (int i = 0; i < message.manoeuvre_size(); ++i) {
      auto const& manoeuvre = message.manoeuvre(i);
      flight_plan->manœuvres_.push_back(
          NavigationManœuvre::ReadFromMessage(manoeuvre, ephemeris));
      flight_plan->manœuvres_[i].set_coasting_trajectory(
          flight_plan->segments_[2 * i]);
    }

    // We may end up here with a flight plan that has too many anomalous
    // segments because of past bugs.  The best we can do is to ignore it.
    if (!flight_plan->RecomputeSegments()) {
      flight_plan.reset();
    }
  } else {
    for (int i = 0; i < message.manoeuvre_size(); ++i) {
      auto const& manoeuvre = message.manoeuvre(i);
      flight_plan->manœuvres_.push_back(
          NavigationManœuvre::ReadFromMessage(manoeuvre, ephemeris));
    }
    // We need to forcefully prolong, otherwise we might exceed the ephemeris
    // step limit while recomputing the segments and fail the check.
    flight_plan->ephemeris_->Prolong(flight_plan->start_of_last_coast());
    CHECK(flight_plan->RecomputeSegments()) << message.DebugString();
  }

  return std::move(flight_plan);
}

FlightPlan::FlightPlan()
    : ephemeris_(testing_utilities::make_not_null<Ephemeris<Barycentric>*>()),
      adaptive_step_parameters_(
          DormandElMikkawyPrince1986RKN434FM<Position<Barycentric>>(),
          /*max_steps=*/1,
          /*length_integration_tolerance=*/1 * Metre,
          /*speed_integration_tolerance=*/1 * Metre / Second) {}

void FlightPlan::Append(NavigationManœuvre manœuvre) {
  manœuvres_.emplace_back(std::move(manœuvre));
  {
    // Hide the moved-from |manœuvre|.
    NavigationManœuvre& manœuvre = manœuvres_.back();
    CHECK_EQ(manœuvre.initial_time(), segments_.back()->last().time());
    manœuvre.set_coasting_trajectory(segments_.back());
    AddSegment();
    BurnLastSegment(manœuvre);
    AddSegment();
    CoastLastSegment(final_time_);
  }
}

bool FlightPlan::RecomputeSegments() {
  // It is important that the segments be destroyed in (reverse chronological)
  // order of the forks.
  while (segments_.size() > 1) {
    PopLastSegment();
  }
  ResetLastSegment();
  for (auto& manœuvre : manœuvres_) {
    CoastLastSegment(manœuvre.initial_time());
    manœuvre.set_coasting_trajectory(segments_.back());
    AddSegment();
    BurnLastSegment(manœuvre);
    AddSegment();
  }
  CoastLastSegment(final_time_);
  return anomalous_segments_ <= 2;
}

void FlightPlan::BurnLastSegment(NavigationManœuvre const& manœuvre) {
  if (anomalous_segments_ > 0) {
    return;
  } else if (manœuvre.initial_time() < manœuvre.final_time()) {
    bool const reached_final_time =
        ephemeris_->FlowWithAdaptiveStep(segments_.back(),
                                         manœuvre.IntrinsicAcceleration(),
                                         manœuvre.final_time(),
                                         adaptive_step_parameters_,
                                         max_ephemeris_steps_per_frame);
    if (!reached_final_time) {
      anomalous_segments_ = 1;
    }
  }
}

void FlightPlan::CoastLastSegment(Instant const& final_time) {
  if (anomalous_segments_ > 0) {
    return;
  } else {
    bool const reached_final_time =
          ephemeris_->FlowWithAdaptiveStep(
                          segments_.back(),
                          Ephemeris<Barycentric>::kNoIntrinsicAcceleration,
                          final_time,
                          adaptive_step_parameters_,
                          max_ephemeris_steps_per_frame);
    if (!reached_final_time) {
      anomalous_segments_ = 1;
    }
  }
}

void FlightPlan::ReplaceLastSegment(
    not_null<DiscreteTrajectory<Barycentric>*> const segment) {
  CHECK_EQ(segment->parent(), segments_.back()->parent());
  CHECK_EQ(segment->Fork().time(), segments_.back()->Fork().time());
  PopLastSegment();
  // |segment| must not be anomalous, so it cannot not follow an anomalous
  // segment.
  CHECK_EQ(0, anomalous_segments_);
  segments_.emplace_back(segment);
}

void FlightPlan::AddSegment() {
  segments_.emplace_back(segments_.back()->NewForkAtLast());
  if (anomalous_segments_ > 0) {
    ++anomalous_segments_;
  }
}

void FlightPlan::ResetLastSegment() {
  segments_.back()->ForgetAfter(segments_.back()->Fork().time());
  if (anomalous_segments_ == 1) {
    // If there was one anomalous segment, it was the last one, which was
    // anomalous because it ended early.  It is no longer anomalous.
    anomalous_segments_ = 0;
  }
}

void FlightPlan::PopLastSegment() {
  DiscreteTrajectory<Barycentric>* trajectory = segments_.back();
  CHECK(!trajectory->is_root());
  trajectory->parent()->DeleteFork(&trajectory);
  segments_.pop_back();
  if (anomalous_segments_ > 0) {
    --anomalous_segments_;
  }
}

DiscreteTrajectory<Barycentric>* FlightPlan::CoastIfReachesManœuvreInitialTime(
    DiscreteTrajectory<Barycentric>& coast,
    NavigationManœuvre const& manœuvre) {
  DiscreteTrajectory<Barycentric>* recomputed_coast =
      coast.parent()->NewForkWithoutCopy(coast.Fork().time());
  bool const reached_manœuvre_initial_time =
      ephemeris_->FlowWithAdaptiveStep(
          recomputed_coast,
          Ephemeris<Barycentric>::kNoIntrinsicAcceleration,
          manœuvre.initial_time(),
          adaptive_step_parameters_,
          max_ephemeris_steps_per_frame);
  if (!reached_manœuvre_initial_time) {
    recomputed_coast->parent()->DeleteFork(&recomputed_coast);
  }
  return recomputed_coast;
}

Instant FlightPlan::start_of_last_coast() const {
  return manœuvres_.empty() ? initial_time_ : manœuvres_.back().final_time();
}

Instant FlightPlan::start_of_penultimate_coast() const {
  return manœuvres_.size() == 1
             ? initial_time_
             : manœuvres_[manœuvres_.size() - 2].final_time();
}

DiscreteTrajectory<Barycentric>& FlightPlan::last_coast() {
  return *segments_.back();
}

DiscreteTrajectory<Barycentric>& FlightPlan::penultimate_coast() {
  // The penultimate coast is the antepenultimate segment.
  return *segments_[segments_.size() - 3];
}

}  // namespace ksp_plugin
}  // namespace principia
