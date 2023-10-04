#include "safety_shield/human_reach.h"

namespace safety_shield {

HumanReach::HumanReach(int n_joints_meas,
      std::map<std::string, int> joint_names,
      std::map<std::string, reach_lib::jointPair>& body_link_joints, 
      const std::map<std::string, double>& thickness, 
      std::vector<double>& max_v, 
      std::vector<double>& max_a,
      std::vector<std::string>& extremity_base_names, 
      std::vector<std::string>& extremity_end_names, 
      std::vector<double>& extremity_length,
      std::vector<double>& extremity_thickness,
      double measurement_error_pos, 
      double measurement_error_vel, 
      double delay):
  n_joints_meas_(n_joints_meas),
  body_link_joints_(body_link_joints),
  measurement_error_pos_(measurement_error_pos),
  measurement_error_vel_(measurement_error_vel),
  delay_(delay)
{
  reach_lib::System system(measurement_error_pos, measurement_error_vel, delay);
  human_v_ = reach_lib::ArticulatedVel(system, body_link_joints, thickness, max_v);
  human_a_ = reach_lib::ArticulatedAccel(system, body_link_joints, thickness, max_a);
  // Create extremity map
  std::map<std::string, reach_lib::jointPair> extremity_body_segment_map;
  std::vector<double> extremity_max_v;
  for (int i = 0; i < extremity_base_names.size(); i++) {
    extremity_body_segment_map[extremity_base_names[i]] = reach_lib::jointPair(joint_names.at(extremity_base_names[i]), joint_names.at(extremity_end_names[i]));
    extremity_max_v.push_back(
      std::max(max_v.at(joint_names.at(extremity_base_names[i])), max_v.at(joint_names.at(extremity_end_names[i]))));
  }
  assert(extremity_base_names.size() == extremity_end_names.size());
  human_p_ = reach_lib::ArticulatedPos(system, extremity_body_segment_map, extremity_thickness, extremity_max_v, extremity_length);

  for (int i = 0; i < n_joints_meas; i++) {
    joint_pos_.push_back(reach_lib::Point(0.0, 0.0, 0.0));
    joint_vel_.push_back(reach_lib::Point(0.0, 0.0, 0.0));
  }
}

void HumanReach::reset() {
  last_meas_timestep_ = -1;
  for (int i = 0; i < n_joints_meas_; i++) {
    joint_pos_.push_back(reach_lib::Point(0.0, 0.0, 0.0));
    joint_vel_.push_back(reach_lib::Point(0.0, 0.0, 0.0));
  }
}

void HumanReach::measurement(const std::vector<reach_lib::Point>& human_joint_pos, double time) {
  try {
    if (last_meas_timestep_ != -1) {
      double dt = time - last_meas_timestep_;
      if (dt < 1e-7) {
        spdlog::warn("HumanReach::measurement: dt is too small. dt = {}", dt);
        joint_pos_ = human_joint_pos;
        last_meas_timestep_ = time;
        return;
      }
      for (int i = 0; i < human_joint_pos.size(); i++) {
        // If more than 1 measurement, calculate velocity
        joint_vel_[i] = (human_joint_pos[i] - joint_pos_[i]) * (1 / dt);
      }
      has_second_meas_ = true;
    }
    joint_pos_ = human_joint_pos;
    last_meas_timestep_ = time;
    // ROS_INFO_STREAM("Human Mocap measurement received. Timestamp of meas was " << last_meas_timestep);
  } catch (const std::exception& exc) {
    spdlog::error("Exception in HumanReach::measurement: {}", exc.what());
  }
}

void HumanReach::humanReachabilityAnalysis(double t_command, double t_brake) {
  try {
    // Time between reach command msg and last measurement plus the t_brake time.
    double t_reach = t_command - last_meas_timestep_ + t_brake;
    // Calculate reachable set
    human_p_.update(0.0, t_reach, joint_pos_, joint_vel_);
    human_v_.update(0.0, t_reach, joint_pos_, joint_vel_);
    human_a_.update(0.0, t_reach, joint_pos_, joint_vel_);
  } catch (const std::exception& exc) {
    spdlog::error("Exception in HumanReach::humanReachabilityAnalysis: {}", exc.what());
  }
}

/// update human model for each time step and collect capsules in a list
std::vector<std::vector<std::vector<reach_lib::Capsule>>> HumanReach::improvedHumanReachabilityAnalysis(double t_command, double current_motion_time, double goal_motion_time, double sample_time) {
  std::vector<std::vector<std::vector<reach_lib::Capsule>>> list;
  // Time between reach command msg and last measurement plus the t_brake time.
  // TODO: t_reach und time_steps richtig berechnen
  double t_reach_human = t_command - last_meas_timestep_ + goal_motion_time;
  double t_reach_robot = goal_motion_time - current_motion_time;
  int time_steps = ceil(t_reach_robot / sample_time);
  double increment = t_reach_human / time_steps;
  double begin = 0;
  double end = increment;
  for(int i = 0; i < time_steps; i++) {
    // TODO: funktioniert das so mit dem updaten der Modelle, bezogen auf begin und end?
    human_p_.update(begin, end, joint_pos_, joint_vel_);
    human_v_.update(begin, end, joint_pos_, joint_vel_);
    human_a_.update(begin, end, joint_pos_, joint_vel_);
    list.push_back(getAllCapsules());
    begin += increment;
    end += increment;
  }
  return list;
}

}  // namespace safety_shield
