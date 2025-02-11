/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2019, PickNik Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the PickNik nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/* Author: Michael Lautman */

#include <moveit/robot_model/robot_model.h>
#include <moveit/robot_state/robot_state.h>
#include <moveit/robot_state/cartesian_interpolator.h>
#include <moveit/utils/robot_model_test_utils.h>
#include <moveit/utils/eigen_test_utils.h>

#include <rclcpp/node.hpp>

using namespace moveit::core;

class SimpleRobot : public testing::Test
{
protected:
  void SetUp() override
  {
    RobotModelBuilder builder("simple", "a");
    builder.addChain("a->b", "continuous");
    builder.addChain("b->c", "prismatic");
    builder.addGroupChain("a", "c", "group");
    robot_model_ = builder.build();
  }

  void TearDown() override
  {
  }

protected:
  RobotModelConstPtr robot_model_;

  static std::size_t generateTestTraj(std::vector<std::shared_ptr<RobotState>>& traj,
                                      const RobotModelConstPtr& robot_model_);
};

std::size_t SimpleRobot::generateTestTraj(std::vector<std::shared_ptr<RobotState>>& traj,
                                          const RobotModelConstPtr& robot_model_)
{
  traj.clear();

  auto robot_state = std::make_shared<RobotState>(robot_model_);
  robot_state->setToDefaultValues();
  double ja, jc;

  // 3 waypoints with default joints
  for (std::size_t traj_ix = 0; traj_ix < 3; ++traj_ix)
  {
    traj.push_back(std::make_shared<RobotState>(*robot_state));
  }

  ja = robot_state->getVariablePosition("a-b-joint");  // revolute joint
  jc = robot_state->getVariablePosition("b-c-joint");  // prismatic joint

  // 4th waypoint with a small jump of 0.01 in revolute joint and prismatic joint. This should not be considered a jump
  ja = ja - 0.01;
  robot_state->setVariablePosition("a-b-joint", ja);
  jc = jc - 0.01;
  robot_state->setVariablePosition("b-c-joint", jc);
  traj.push_back(std::make_shared<RobotState>(*robot_state));

  // 5th waypoint with a large jump of 1.01 in first revolute joint
  ja = ja + 1.01;
  robot_state->setVariablePosition("a-b-joint", ja);
  traj.push_back(std::make_shared<RobotState>(*robot_state));

  // 6th waypoint with a large jump of 1.01 in first prismatic joint
  jc = jc + 1.01;
  robot_state->setVariablePosition("b-c-joint", jc);
  traj.push_back(std::make_shared<RobotState>(*robot_state));

  // 7th waypoint with no jump
  traj.push_back(std::make_shared<RobotState>(*robot_state));

  return traj.size();
}

TEST_F(SimpleRobot, testGenerateTrajectory)
{
  std::vector<std::shared_ptr<RobotState>> traj;

  // The full trajectory should be of length 7
  const std::size_t expected_full_traj_len = 7;

  // Generate a test trajectory
  std::size_t full_traj_len = generateTestTraj(traj, robot_model_);

  // Test the generateTestTraj still generates a trajectory of length 7
  EXPECT_EQ(full_traj_len, expected_full_traj_len);  // full traj should be 7 waypoints long
}

TEST_F(SimpleRobot, checkAbsoluteJointSpaceJump)
{
  const JointModelGroup* joint_model_group = robot_model_->getJointModelGroup("group");
  std::vector<std::shared_ptr<RobotState>> traj;

  // A revolute joint jumps 1.01 at the 5th waypoint and a prismatic joint jumps 1.01 at the 6th waypoint
  const std::size_t expected_revolute_jump_traj_len = 4;
  const std::size_t expected_prismatic_jump_traj_len = 5;

  // Pre-compute expected results for tests
  std::size_t full_traj_len = generateTestTraj(traj, robot_model_);
  const double expected_revolute_jump_fraction =
      static_cast<double>(expected_revolute_jump_traj_len) / static_cast<double>(full_traj_len);
  const double expected_prismatic_jump_fraction =
      static_cast<double>(expected_prismatic_jump_traj_len) / static_cast<double>(full_traj_len);

  // Container for results
  double fraction;

  // Direct call of absolute version
  fraction = CartesianInterpolator::checkAbsoluteJointSpaceJump(joint_model_group, traj, 1.0, 1.0);
  EXPECT_EQ(expected_revolute_jump_traj_len, traj.size());  // traj should be cut
  EXPECT_NEAR(expected_revolute_jump_fraction, fraction, 0.01);

  // Indirect call using checkJointSpaceJumps
  generateTestTraj(traj, robot_model_);
  fraction = CartesianInterpolator::checkJointSpaceJump(joint_model_group, traj, JumpThreshold(1.0, 1.0));
  EXPECT_EQ(expected_revolute_jump_traj_len, traj.size());  // traj should be cut before the revolute jump
  EXPECT_NEAR(expected_revolute_jump_fraction, fraction, 0.01);

  // Test revolute joints
  generateTestTraj(traj, robot_model_);
  fraction = CartesianInterpolator::checkJointSpaceJump(joint_model_group, traj, JumpThreshold(1.0, 0.0));
  EXPECT_EQ(expected_revolute_jump_traj_len, traj.size());  // traj should be cut before the revolute jump
  EXPECT_NEAR(expected_revolute_jump_fraction, fraction, 0.01);

  // Test prismatic joints
  generateTestTraj(traj, robot_model_);
  fraction = CartesianInterpolator::checkJointSpaceJump(joint_model_group, traj, JumpThreshold(0.0, 1.0));
  EXPECT_EQ(expected_prismatic_jump_traj_len, traj.size());  // traj should be cut before the prismatic jump
  EXPECT_NEAR(expected_prismatic_jump_fraction, fraction, 0.01);

  // Ignore all absolute jumps
  generateTestTraj(traj, robot_model_);
  fraction = CartesianInterpolator::checkJointSpaceJump(joint_model_group, traj, JumpThreshold(0.0, 0.0));
  EXPECT_EQ(full_traj_len, traj.size());  // traj should not be cut
  EXPECT_NEAR(1.0, fraction, 0.01);
}

TEST_F(SimpleRobot, checkRelativeJointSpaceJump)
{
  const JointModelGroup* joint_model_group = robot_model_->getJointModelGroup("group");
  std::vector<std::shared_ptr<RobotState>> traj;

  // The first large jump of 1.01 occurs at the 5th waypoint so the test should trim the trajectory to length 4
  const std::size_t expected_relative_jump_traj_len = 4;

  // Pre-compute expected results for tests
  std::size_t full_traj_len = generateTestTraj(traj, robot_model_);
  const double expected_relative_jump_fraction =
      static_cast<double>(expected_relative_jump_traj_len) / static_cast<double>(full_traj_len);

  // Container for results
  double fraction;

  // Direct call of relative version: 1.01 > 2.97 * (0.01 * 2 + 1.01 * 2)/6.
  fraction = CartesianInterpolator::checkRelativeJointSpaceJump(joint_model_group, traj, 2.97);
  EXPECT_EQ(expected_relative_jump_traj_len, traj.size());  // traj should be cut before the first jump of 1.01
  EXPECT_NEAR(expected_relative_jump_fraction, fraction, 0.01);

  // Indirect call of relative version using checkJointSpaceJumps
  generateTestTraj(traj, robot_model_);
  fraction = CartesianInterpolator::checkJointSpaceJump(joint_model_group, traj, JumpThreshold(2.97));
  EXPECT_EQ(expected_relative_jump_traj_len, traj.size());  // traj should be cut before the first jump of 1.01
  EXPECT_NEAR(expected_relative_jump_fraction, fraction, 0.01);

  // Trajectory should not be cut: 1.01 < 2.98 * (0.01 * 2 + 1.01 * 2)/6.
  generateTestTraj(traj, robot_model_);
  fraction = CartesianInterpolator::checkJointSpaceJump(joint_model_group, traj, JumpThreshold(2.98));
  EXPECT_EQ(full_traj_len, traj.size());  // traj should not be cut
  EXPECT_NEAR(1.0, fraction, 0.01);
}

// TODO - The tests below fail since no kinematic plugins are found. Move the tests to IK plugin package.
// class PandaRobot : public testing::Test
// {
// protected:
//   static void SetUpTestSuite()  // setup resources shared between tests
//   {
//     robot_model_ = loadTestingRobotModel("panda");
//     jmg = robot_model_->getJointModelGroup("panda_arm");
//     link = robot_model_->getLinkModel("panda_link8");
//     ASSERT_TRUE(link);
//     node = rclcpp::Node::make_shared("test_cartesian_interpolator");
//     loadIKPluginForGroup(node, jmg, "panda_link0", link->getName());
//   }
//
//   static void TearDownTestSuite()
//   {
//     robot_model_.reset();
//   }
//
//   void SetUp() override
//   {
//     start_state = std::make_shared<RobotState>(robot_model_);
//     ASSERT_TRUE(start_state->setToDefaultValues(jmg, "ready"));
//     start_pose = start_state->getGlobalLinkTransform(link);
//   }
//
//   double computeCartesianPath(std::vector<std::shared_ptr<RobotState>>& result, const Eigen::Vector3d& translation,
//                               bool global)
//   {
//     return CartesianInterpolator::computeCartesianPath(start_state.get(), jmg, result, link, translation, global,
//                                                        MaxEEFStep(0.1), JumpThreshold(), GroupStateValidityCallbackFn(),
//                                                        kinematics::KinematicsQueryOptions());
//   }
//
//   double computeCartesianPath(std::vector<std::shared_ptr<RobotState>>& result, const Eigen::Isometry3d& target,
//                               bool global, const Eigen::Isometry3d& offset = Eigen::Isometry3d::Identity())
//   {
//     return CartesianInterpolator::computeCartesianPath(start_state.get(), jmg, result, link, target, global,
//                                                        MaxEEFStep(0.1), JumpThreshold(), GroupStateValidityCallbackFn(),
//                                                        kinematics::KinematicsQueryOptions(),
//                                                        kinematics::KinematicsBase::IKCostFn(), offset);
//   }
//
// protected:
//   static RobotModelPtr robot_model_;
//   static JointModelGroup* jmg_;
//   static const LinkModel* link_;
//   static rclcpp::Node::SharedPtr node;
//
//   const double prec_ = 1e-8;
//   RobotStatePtr start_state_;
//   Eigen::Isometry3d start_pose_;
//   std::vector<std::shared_ptr<RobotState>> result_;
// };
//
// TEST_F(PandaRobot, testVectorGlobal)
// {
//   Eigen::Vector3d translation(0.2, 0, 0);                                   // move by 0.2 along world's x axis
//   ASSERT_DOUBLE_EQ(computeCartesianPath(result, translation, true), 0.2);  // moved full distance of 0.2
//   // first pose of trajectory should be identical to start_pose
//   EXPECT_EIGEN_EQ(result.front()->getGlobalLinkTransform(link), start_pose);
//   // last pose of trajectory should have same orientation, and offset of 0.2 along world's x-axis
//   EXPECT_EIGEN_NEAR(result.back()->getGlobalLinkTransform(link).linear(), start_pose.linear(), prec);
//   EXPECT_EIGEN_NEAR(result.back()->getGlobalLinkTransform(link).translation(),
//                     start_pose.translation() + translation, prec);
// }
// TEST_F(PandaRobot, testVectorLocal)
// {
//   Eigen::Vector3d translation(0.2, 0, 0);                                    // move by 0.2 along link's x axis
//   ASSERT_DOUBLE_EQ(computeCartesianPath(result, translation, false), 0.2);  // moved full distance of 0.2
//   // first pose of trajectory should be identical to start_pose
//   EXPECT_EIGEN_EQ(result.front()->getGlobalLinkTransform(link), start_pose);
//   // last pose of trajectory should have same orientation, and offset of 0.2 along link's x-axis
//   EXPECT_EIGEN_NEAR(result.back()->getGlobalLinkTransform(link).linear(), start_pose.linear(), prec);
//   EXPECT_EIGEN_NEAR(result.back()->getGlobalLinkTransform(link).translation(), start_pose * translation, prec);
// }

// TEST_F(PandaRobot, testTranslationGlobal)
// {
//   Eigen::Isometry3d goal = start_pose;
//   goal.translation().x() += 0.2;  // move by 0.2 along world's x-axis

//   ASSERT_DOUBLE_EQ(computeCartesianPath(result, goal, true), 1.0);  // 100% of distance generated
//   // first pose of trajectory should be identical to start_pose
//   EXPECT_EIGEN_EQ(result.front()->getGlobalLinkTransform(link), start_pose);
//   // last pose of trajectory should have same orientation, but offset of 0.2 along world's x-axis
//   EXPECT_EIGEN_NEAR(result.back()->getGlobalLinkTransform(link).linear(), start_pose.linear(), prec);
//   EXPECT_EIGEN_NEAR(result.back()->getGlobalLinkTransform(link).translation(), goal.translation(), prec);
// }
// TEST_F(PandaRobot, testTranslationLocal)
// {
//   Eigen::Isometry3d offset(Eigen::Translation3d(0.2, 0, 0));            // move along link's x-axis
//   ASSERT_DOUBLE_EQ(computeCartesianPath(result, offset, false), 1.0);  // 100% of distance generated
//   // first pose of trajectory should be identical to start_pose
//   EXPECT_EIGEN_EQ(result.front()->getGlobalLinkTransform(link), start_pose);
//   // last pose of trajectory should have same orientation, but offset of 0.2 along link's x-axis
//   EXPECT_EIGEN_NEAR(result.back()->getGlobalLinkTransform(link).linear(), start_pose.linear(), prec);
//   EXPECT_EIGEN_NEAR(result.back()->getGlobalLinkTransform(link).translation(), start_pose * offset.translation(),
//                     prec);
// }

// TEST_F(PandaRobot, testRotationLocal)
// {
//   // 45° rotation about links's x-axis
//   Eigen::Isometry3d rot(Eigen::AngleAxisd(M_PI / 4, Eigen::Vector3d::UnitX()));
//   Eigen::Isometry3d goal = start_pose * rot;

//   ASSERT_DOUBLE_EQ(computeCartesianPath(result, rot, false), 1.0);  // 100% of distance generated
//   // first pose of trajectory should be identical to start_pose
//   EXPECT_EIGEN_EQ(result.front()->getGlobalLinkTransform(link), start_pose);
//   // last pose of trajectory should have same position,
//   EXPECT_EIGEN_NEAR(result.back()->getGlobalLinkTransform(link).translation(), start_pose.translation(), prec);
//   EXPECT_EIGEN_NEAR(result.back()->getGlobalLinkTransform(link), goal, prec);
// }
// TEST_F(PandaRobot, testRotationGlobal)
// {
//   // 45° rotation about links's x-axis
//   Eigen::Isometry3d rot(Eigen::AngleAxisd(M_PI / 4, Eigen::Vector3d::UnitX()));
//   Eigen::Isometry3d goal = start_pose * rot;

//   ASSERT_DOUBLE_EQ(computeCartesianPath(result, goal, true), 1.0);  // 100% of distance generated
//   // first pose of trajectory should be identical to start_pose
//   EXPECT_EIGEN_NEAR(result.front()->getGlobalLinkTransform(link), start_pose, prec);
//   // last pose of trajectory should have same position,
//   EXPECT_EIGEN_NEAR(result.back()->getGlobalLinkTransform(link).translation(), start_pose.translation(), prec);
//   EXPECT_EIGEN_NEAR(result.back()->getGlobalLinkTransform(link), goal, prec);
// }
// TEST_F(PandaRobot, testRotationOffset)
// {
//   // define offset to virtual center frame
//   Eigen::Isometry3d offset = Eigen::Translation3d(0, 0, 0.2) * Eigen::AngleAxisd(-M_PI / 4, Eigen::Vector3d::UnitZ());
//   // 45° rotation about center's x-axis
//   Eigen::Isometry3d rot(Eigen::AngleAxisd(M_PI / 4, Eigen::Vector3d::UnitX()));
//   Eigen::Isometry3d goal = start_pose * offset * rot;

//   ASSERT_DOUBLE_EQ(computeCartesianPath(result, goal, true, offset), 1.0);  // 100% of distance generated
//   // first pose of trajectory should be identical to start_pose
//   EXPECT_EIGEN_NEAR(result.front()->getGlobalLinkTransform(link), start_pose, prec);

//   // All waypoints of trajectory should have same position in virtual frame
//   for (const auto& waypoint : result)
//     EXPECT_EIGEN_NEAR((waypoint->getGlobalLinkTransform(link) * offset).translation(),
//                       (start_pose * offset).translation(), prec);
//   // goal should be reached by virtual frame
//   EXPECT_EIGEN_NEAR(result.back()->getGlobalLinkTransform(link) * offset, goal, prec);
// }

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);
  return RUN_ALL_TESTS();
}
